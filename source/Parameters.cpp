#include "Parameters.h"

namespace rain
{

juce::StringArray qualityNames()
{
    return { "Digital", "Vintage", "Clean Tape", "Dirty Tape" };
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
    percent (ParamID::size, "Size", 50.0f);
    percent (ParamID::shape, "Shape", 50.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::pitch, v }, "Pitch", Range { -24.0f, 24.0f, 0.01f }, 0.0f, withLabel ("st", 1)));
    bipolar (ParamID::timeRandom, "Time Random");
    bipolar (ParamID::sizeRandom, "Size Random");
    bipolar (ParamID::shapeRandom, "Shape Random");
    bipolar (ParamID::pitchRandom, "Pitch Random");
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamID::density, v }, "Density", Range { 0.0f, 100.0f, 0.1f }, 70.0f, densityText()));
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

    return layout;
}

} // namespace rain
