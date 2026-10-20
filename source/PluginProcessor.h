#pragma once

#include "Parameters.h"
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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

private:
    void handleAsyncUpdate() override;
    dsp::RainParams readParams() const noexcept;

    juce::AudioProcessorValueTreeState state;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RainProcessor)
};

} // namespace rain
