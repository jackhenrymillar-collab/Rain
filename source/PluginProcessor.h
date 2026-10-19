#pragma once

#include "Parameters.h"
#include "Presets.h"
#include "dsp/RainEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace rain
{

class RainProcessor final : public juce::AudioProcessor,
                            private juce::AsyncUpdater
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

    // Hosts see the factory presets as programs.
    int getNumPrograms() override { return presets.numFactory(); }
    int getCurrentProgram() override { return juce::jmax (0, presets.currentFactoryIndex()); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return presets.factoryName (index); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    PresetManager& getPresets() noexcept { return presets; }
    const dsp::EngineMonitor& getMonitor() const noexcept { return engine.monitor(); }

private:
    void handleAsyncUpdate() override;
    dsp::RainParams readParams() const noexcept;

    juce::AudioProcessorValueTreeState state;
    PresetManager presets { state };
    dsp::RainEngine engine;
    std::atomic<int> engineLatency { 0 };

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
    std::atomic<float>* mode = nullptr;
    std::atomic<float>* sync = nullptr;
    std::atomic<float>* syncRate = nullptr;
    std::atomic<float>* scale = nullptr;
    std::array<std::atomic<float>*, 2> modRate {}, modDepth {}, modTarget {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RainProcessor)
};

} // namespace rain
