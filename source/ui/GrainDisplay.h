#pragma once

#include "Theme.h"
#include "../dsp/RainEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <vector>

namespace rain::ui
{

// The buffer and its grains, drawn live:
//   - the recording buffer as a scrolling waveform, newest audio on the right;
//   - TIME as a gold marker showing how far back grains read from;
//   - each grain drawn with the envelope SHAPE gives it, as wide as SIZE makes it;
//   - DENSITY as a lane of trigger dots: evenly spaced, scattered, or synced.
// The grains are a picture of what the knobs ask Beads to do, using its own laws
// for grain length, envelope and trigger rate, rather than a tap on each grain.
class GrainDisplay final : public juce::Component
{
public:
    GrainDisplay (juce::AudioProcessorValueTreeState&, const dsp::EngineMonitor&);

    void advance (float seconds); // call from the editor's timer
    void paint (juce::Graphics&) override;

    // Beads' grain envelope (beads/dsp/grain_envelope.h), at phase 0..1.
    static float envelope (float phase, float shape) noexcept;
    static juce::String shapeName (float shape);

    // Text for DENSITY and TIME when synced to the host tempo.
    static juce::String noteName (double beats);
    static juce::String syncDensityText (float density, int rateIndex);
    static juce::String syncTimeText (float effectiveTime, float bufferSeconds, double bpm, int rateIndex);

private:
    struct Grain
    {
        float age, life, position, seconds, shape, lane, amp;
        bool reverse;
    };

    void spawn();
    float param (const char* id) const;

    juce::AudioProcessorValueTreeState& state;
    const dsp::EngineMonitor& monitor;
    juce::Random rng { 23 };

    std::vector<Grain> grains;
    std::vector<float> triggers; // ages of recent triggers, for the density lane
    float untilNext = 0.0f;
    long long lastStep = -1;
};

} // namespace rain::ui
