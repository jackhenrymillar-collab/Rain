#include "Parameters.h"

#include "dsp/Motion.h"

namespace rain
{

juce::StringArray qualityNames()
{
    return { "Digital", "Vintage", "Clean Tape", "Dirty Tape" };
}

juce::StringArray modeNames()
{
    return { "Grains", "Delay" };
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
    return { "Off", "Time", "Size", "Shape", "Pitch", "Density", "Feedback", "Dry/Wet", "Reverb" };
}

namespace
{
    using Range = juce::NormalisableRange<float>;

    juce::AudioParameterFloatAttributes withLabel (const juce::String& label, int decimals = 0)
    {
        return juce::AudioParameterFloatAttributes()
            .withLabel (label)
            .withStringFromValueFunction ([decimals] (float v, int) { return juce::String (v, decimals); });
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
                     + juce::String (std::abs (d) * 2.0f, 0) + "%";
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
    percent (ParamID::size, "Size", 65.0f);
    percent (ParamID::shape, "Shape", 60.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::pitch, v }, "Pitch", Range { -24.0f, 24.0f, 0.01f }, 0.0f, withLabel ("st", 1)));
    bipolar (ParamID::timeRandom, "Time Random");
    bipolar (ParamID::sizeRandom, "Size Random");
    bipolar (ParamID::shapeRandom, "Shape Random");
    bipolar (ParamID::pitchRandom, "Pitch Random");
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::density, v }, "Density", Range { 0.0f, 100.0f, 0.1f }, 82.0f, densityText()));
    percent (ParamID::feedback, "Feedback", 0.0f);
    percent (ParamID::dryWet, "Dry/Wet", 50.0f);
    percent (ParamID::reverb, "Reverb", 30.0f);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::freeze, v }, "Freeze", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::seed, v }, "Seed", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::seedLock, v }, "Seed Lock", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::quality, v }, "Quality", qualityNames(), 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::autoGain, v }, "Auto Gain", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::inputGain, v }, "Input Gain", Range { 0.0f, 32.0f, 0.1f }, 0.0f, withLabel ("dB", 1)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::outputGain, v }, "Output Gain", Range { -24.0f, 12.0f, 0.1f }, 2.5f, withLabel ("dB", 1)));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::mode, v }, "Mode", modeNames(), 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamID::sync, v }, "Tempo Sync", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::syncRate, v }, "Sync Rate", syncRateNames(), 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamID::scale, v }, "Scale", scaleNames(), 0));

    // Modulator rates: 0.01 to 10 Hz, skewed so slow rates get most of the knob.
    auto rateRange = Range { 0.01f, 10.0f, 0.0f, 0.3f };
    auto rateText = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float hz, int) {
            if (hz < 1.0f)
                return String (1.0f / hz, hz < 0.1f ? 0 : 1) + " s";
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
