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
    inline constexpr auto mode        = "mode";
    inline constexpr auto sync        = "sync";
    inline constexpr auto syncRate    = "syncrate";
    inline constexpr auto scale       = "scale";
    inline constexpr auto mod1Rate    = "mod1rate";
    inline constexpr auto mod1Depth   = "mod1depth";
    inline constexpr auto mod1Target  = "mod1target";
    inline constexpr auto mod2Rate    = "mod2rate";
    inline constexpr auto mod2Depth   = "mod2depth";
    inline constexpr auto mod2Target  = "mod2target";
    inline constexpr auto overlap     = "overlap";
    inline constexpr auto reverse     = "reverse";
    inline constexpr auto soften      = "soften";
    inline constexpr auto randomPan   = "randompan";
}

// In front-panel order. Must match dsp::Quality.
juce::StringArray qualityNames();
juce::StringArray modeNames();      // dsp::Mode
juce::StringArray syncRateNames();  // dsp::kSyncDivisionBeats
juce::StringArray scaleNames();     // dsp::Scale
juce::StringArray modTargetNames(); // dsp::ModTarget

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace rain
