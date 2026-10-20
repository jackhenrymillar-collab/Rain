#pragma once

#include "Parameters.h"
#include "Presets.h"
#include "dsp/RainEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace rain
{

class RainProcessor final : public juce::AudioProcessor
{
public:
    RainProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    // Hosts see the factory presets as programs, plus one last entry that stands for
    // whichever user preset is loaded (so the program number never claims "Init").
    int getNumPrograms() override { return presets.numFactory() + 1; }
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    PresetManager& getPresets() noexcept { return presets; }
    // The host's bypass switch is Rain's own (the moon), so both stay in step.
    juce::AudioProcessorParameter* getBypassParameter() const override { return state.getParameter (ParamID::bypass); }
    const dsp::EngineMonitor& getMonitor() const noexcept { return engine.monitor(); }

private:
    // Written into saved sessions. 2: grains only (the mode parameter is gone).
    static constexpr int kStateVersion = 2;
    dsp::RainParams readParams() const noexcept;
    void processMonoOut (juce::AudioBuffer<float>&, int numSamples);

    juce::AudioProcessorValueTreeState state;
    PresetManager presets { state };
    dsp::RainEngine engine;
    std::vector<float> monoRight; // the right channel for mono tracks, sized in prepareToPlay

    // Cached raw parameter pointers: lock-free reads on the audio thread.
    std::atomic<float>* time = nullptr;
    std::atomic<float>* size = nullptr;
    std::atomic<float>* shape = nullptr;
    std::atomic<float>* pitch = nullptr;
    std::atomic<float>* timeRandom = nullptr;
    std::atomic<float>* sizeRandom = nullptr;
    std::atomic<float>* shapeRandom = nullptr;
    std::atomic<float>* pitchRandom = nullptr;
    std::atomic<float>* density = nullptr;
    std::atomic<float>* feedback = nullptr;
    std::atomic<float>* dryWet = nullptr;
    std::atomic<float>* reverb = nullptr;
    std::atomic<float>* freeze = nullptr;
    std::atomic<float>* seed = nullptr;
    std::atomic<float>* seedLock = nullptr;
    std::atomic<float>* quality = nullptr;
    std::atomic<float>* autoGain = nullptr;
    std::atomic<float>* inputGain = nullptr;
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* sync = nullptr;
    std::atomic<float>* syncRate = nullptr;
    std::atomic<float>* scale = nullptr;
    std::atomic<float>* overlap = nullptr;
    std::atomic<float>* reverse = nullptr;
    std::atomic<float>* soften = nullptr;
    std::atomic<float>* randomPan = nullptr;
    std::atomic<float>* formant = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::array<std::atomic<float>*, 2> modRate {}, modDepth {}, modTarget {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RainProcessor)
};

} // namespace rain
