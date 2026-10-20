#include "Parameters.h"

#include "dsp/Motion.h"
#include "dsp/RainEngine.h"

namespace rain
{

juce::StringArray qualityNames()
{
    return { "Digital", "Vintage", "Clean Tape", "Dirty Tape", "Studio" };
}

juce::StringArray syncRateNames()
{
    juce::StringArray names;
    for (const auto* n : dsp::kSyncDivisionNames)
        names.add (n);
    return names;
}

juce::StringArray scaleNames()
{
    return { "Off", "Chromatic", "Major", "Minor", "Pentatonic", "Minor Pent.", "Fifths", "Octaves" };
}

juce::StringArray modTargetNames()
{
    return { "Off", "Time", "Size", "Shape", "Pitch", "Density", "Feedback", "Dry/Wet", "Cloud" };
}

namespace
{
    using Range = juce::NormalisableRange<float>;

    juce::AudioParameterFloatAttributes withLabel (const juce::String& label, int decimals = 0)
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel (label)
            .withStringFromValueFunction ([decimals] (float v, int) { return decimals > 0 ? juce::String (v, decimals) : juce::String (juce::roundToInt (v)); });
    }

    // DENSITY reads like the Beads knob: noon is off, left is regular, right is random.
    juce::AudioParameterFloatAttributes densityText()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) {
                const float d = v - 50.0f;
                if (std::abs (d) < 2.0f)
                    return juce::String ("Off");
                return (d < 0.0f ? juce::String ("Regular ") : juce::String ("Random "))
                     + juce::String (juce::roundToInt (std::abs (d) * 2.0f)) + "%";
            });
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    constexpr int v = 1; // parameter version hint for hosts

    auto percent = [&] (const char* id, const char* name, float def) {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, name, Range { 0.0f, 100.0f, 0.1f }, def, withLabel ("%")));
    };
    auto bipolar = [&] (const char* id, const char* name) {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, name, Range { -100.0f, 100.0f, 0.1f }, 0.0f, withLabel ("%")));
    };

    percent (ParamID::time, "Time", 50.0f);
    // Defaults give a smooth cloud: overlapping ~140 ms grains with soft edges.
    // SIZE is a grain length, or in Overlap mode how many grains overlap.
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::size, v }, "Size", Range { 0.0f, 100.0f, 0.1f }, 49.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) {
            const float s = dsp::RainEngine::grainSeconds (dsp::RainEngine::beadsSize (x * 0.01f));
            return s < 1.0f ? String (roundToInt (s * 1000.0f)) + " ms" : String (s, 2) + " s";
        })));
    percent (ParamID::shape, "Shape", 60.0f);
    // PITCH is the knob position (-1 to +1) and reads in semitones through Beads'
    // knob law, which steps through 0, 1, 3, 4, 7, 12 and 24 semitones each way.
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::pitch, v }, "Pitch", Range { -1.0f, 1.0f, 0.0f }, 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("st")
            .withStringFromValueFunction ([] (float x, int) {
                const float st = dsp::detuneSemitones (x);
                return (st > 0.05f ? "+" : "") + String (st, std::abs (st - std::round (st)) < 0.05f ? 0 : 1);
            })
            .withValueFromStringFunction ([] (const String& t) { return dsp::detunePosition (t.getFloatValue()); })));
    bipolar (ParamID::timeRandom, "Time Random");
    bipolar (ParamID::sizeRandom, "Size Random");
    bipolar (ParamID::shapeRandom, "Shape Random");
    bipolar (ParamID::pitchRandom, "Pitch Random");
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::density, v }, "Density", Range { 0.0f, 100.0f, 0.1f }, 82.0f, densityText()));
    percent (ParamID::feedback, "Feedback", 0.0f);
    percent (ParamID::dryWet, "Dry/Wet", 50.0f);
    percent (ParamID::reverb, "Cloud", 30.0f); // Beads' reverb, named Cloud on Rain's panel
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::freeze, v }, "Freeze", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::seed, v }, "Seed", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::seedLock, v }, "Seed Lock", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::quality, v }, "Quality", qualityNames(), 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::autoGain, v }, "Auto Gain", false));
    // Input gain: 1x to 40x (0 to 32 dB) along Beads' manual gain curve, the knob
    // position to the fourth power (beads/gain_control.cc).
    {
        const float maxDb = 20.0f * std::log10 (40.0f);
        Range gainRange (0.0f, maxDb,
            [] (float, float, float p) { return 20.0f * std::log10 (1.0f + 39.0f * p * p * p * p); },
            [] (float, float, float db) { return std::pow (std::max (0.0f, (std::pow (10.0f, db / 20.0f) - 1.0f) / 39.0f), 0.25f); });
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::inputGain, v }, "Input Gain", gainRange, 0.0f, withLabel ("dB", 1)));
    }
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::outputGain, v }, "Output Gain", Range { -24.0f, 12.0f, 0.1f }, 2.5f, withLabel ("dB", 1)));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::sync, v }, "Tempo Sync", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::syncRate, v }, "Sync Rate", syncRateNames(), 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::scale, v }, "Scale", scaleNames(), 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::overlap, v }, "Overlap", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::formant, v }, "Formant", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::smoothRandom, v }, "Smooth Random", false));
    percent (ParamID::reverse, "Reverse", 0.0f);
    percent (ParamID::soften, "Soften", 25.0f);
    percent (ParamID::smudge, "Smudge", 0.0f);
    percent (ParamID::randomPan, "Random Pan", 0.0f);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::bypass, v }, "Bypass", false));

    // Modulator rates: 0.01 to 10 Hz, skewed so slow rates get most of the knob.
    auto rateRange = Range { 0.01f, 10.0f, 0.0f, 0.3f };
    auto rateText = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float hz, int) {
            if (hz < 1.0f)
                // (String's 0 decimal places means "all of them", so round explicitly.)
                return (hz < 0.1f ? String (roundToInt (1.0f / hz)) : String (1.0f / hz, 1)) + " s";
            return String (hz, 2) + " Hz";
        })
        .withValueFromStringFunction ([] (const String& t) {
            const float x = t.getFloatValue();
            return t.trim().endsWithIgnoreCase ("s") && ! t.containsIgnoreCase ("hz") && x > 0.0f ? 1.0f / x : x;
        });
    const char* rateIds[] = { ParamID::mod1Rate, ParamID::mod2Rate };
    const char* depthIds[] = { ParamID::mod1Depth, ParamID::mod2Depth };
    const char* targetIds[] = { ParamID::mod1Target, ParamID::mod2Target };
    const char* names[] = { "Wave", "Drift" };
    const float defaultRates[] = { 0.2f, 0.5f };
    const int defaultTargets[] = { 1, 4 }; // Time, Pitch
    for (int m = 0; m < 2; ++m)
    {
        const String n (names[m]);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { rateIds[m], v }, n + " Rate", rateRange, defaultRates[m], rateText));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { depthIds[m], v }, n + " Depth", Range { -100.0f, 100.0f, 0.1f }, 0.0f, withLabel ("%")));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { targetIds[m], v }, n + " Target", modTargetNames(), defaultTargets[m]));
    }

    return layout;
}

} // namespace rain
