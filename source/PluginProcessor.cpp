#include "PluginProcessor.h"

namespace rain
{

RainProcessor::RainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "Rain", createParameterLayout())
{
    time        = state.getRawParameterValue (ParamID::time);
    size        = state.getRawParameterValue (ParamID::size);
    shape       = state.getRawParameterValue (ParamID::shape);
    pitch       = state.getRawParameterValue (ParamID::pitch);
    timeRandom  = state.getRawParameterValue (ParamID::timeRandom);
    sizeRandom  = state.getRawParameterValue (ParamID::sizeRandom);
    shapeRandom = state.getRawParameterValue (ParamID::shapeRandom);
    pitchRandom = state.getRawParameterValue (ParamID::pitchRandom);
    density     = state.getRawParameterValue (ParamID::density);
    feedback    = state.getRawParameterValue (ParamID::feedback);
    dryWet      = state.getRawParameterValue (ParamID::dryWet);
    reverb      = state.getRawParameterValue (ParamID::reverb);
    freeze      = state.getRawParameterValue (ParamID::freeze);
    seed        = state.getRawParameterValue (ParamID::seed);
    seedLock    = state.getRawParameterValue (ParamID::seedLock);
    quality     = state.getRawParameterValue (ParamID::quality);
    autoGain    = state.getRawParameterValue (ParamID::autoGain);
    inputGain   = state.getRawParameterValue (ParamID::inputGain);
    outputGain  = state.getRawParameterValue (ParamID::outputGain);
}

bool RainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono-in/stereo-out and stereo/stereo.
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void RainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    engineLatency = engine.latencySamples();
    setLatencySamples (engineLatency);
}

void RainProcessor::reset()
{
    engine.reset();
}

dsp::RainParams RainProcessor::readParams() const noexcept
{
    dsp::RainParams p;
    p.time = time->load() * 0.01f;
    p.size = size->load() * 0.01f;
    p.shape = shape->load() * 0.01f;
    p.pitchSemis = pitch->load();
    p.timeRandom = timeRandom->load() * 0.01f;
    p.sizeRandom = sizeRandom->load() * 0.01f;
    p.shapeRandom = shapeRandom->load() * 0.01f;
    p.pitchRandom = pitchRandom->load() * 0.01f;
    p.density = density->load() * 0.01f;
    p.feedback = feedback->load() * 0.01f;
    p.dryWet = dryWet->load() * 0.01f;
    p.reverb = reverb->load() * 0.01f;
    p.freeze = freeze->load() > 0.5f;
    p.seed = seed->load() > 0.5f;
    p.seedLock = seedLock->load() > 0.5f;
    p.quality = static_cast<dsp::Quality> (juce::jlimit (0, 3, static_cast<int> (quality->load())));
    p.autoGain = autoGain->load() > 0.5f;
    p.inputGainDb = inputGain->load();
    p.outputGainDb = outputGain->load();
    return p;
}

void RainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    // Mono input: duplicate into the right channel.
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    engine.setParams (readParams());
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);

    // Changing quality changes Beads' internal rate, and with it the latency.
    if (engine.latencySamples() != engineLatency.load())
    {
        engineLatency = engine.latencySamples();
        triggerAsyncUpdate();
    }
}

void RainProcessor::handleAsyncUpdate()
{
    setLatencySamples (engineLatency.load());
}

juce::AudioProcessorEditor* RainProcessor::createEditor()
{
    // Phase 1 uses the generic editor. The night-sky UI comes later.
    return new juce::GenericAudioProcessorEditor (*this);
}

void RainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void RainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace rain

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new rain::RainProcessor();
}
