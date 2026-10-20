#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace rain
{

// Parameter IDs are part of saved sessions and presets: never rename one.
namespace ParamID
{
    inline constexpr auto time        = "time";
    inline constexpr auto size        = "size";
    inline constexpr auto shape       = "shape";
    inline constexpr auto pitch       = "pitch";
    inline constexpr auto timeRandom  = "timerandom";
    inline constexpr auto sizeRandom  = "sizerandom";
    inline constexpr auto shapeRandom = "shaperandom";
    inline constexpr auto pitchRandom = "pitchrandom";
    inline constexpr auto density     = "density";
    inline constexpr auto feedback    = "feedback";
    inline constexpr auto dryWet      = "drywet";
    inline constexpr auto reverb      = "reverb";
    inline constexpr auto freeze      = "freeze";
    inline constexpr auto seed        = "seed";
    inline constexpr auto seedLock    = "seedlock";
    inline constexpr auto quality     = "quality";
    inline constexpr auto autoGain    = "autogain";
    inline constexpr auto inputGain   = "inputgain";
    inline constexpr auto outputGain  = "outputgain";
}

// In front-panel order. Must match dsp::Quality.
juce::StringArray qualityNames();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace rain
