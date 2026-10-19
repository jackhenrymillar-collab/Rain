#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    mode        = state.getRawParameterValue (ParamID::mode);
    sync        = state.getRawParameterValue (ParamID::sync);
    syncRate    = state.getRawParameterValue (ParamID::syncRate);
    scale       = state.getRawParameterValue (ParamID::scale);
    overlap     = state.getRawParameterValue (ParamID::overlap);
    reverse     = state.getRawParameterValue (ParamID::reverse);
    soften      = state.getRawParameterValue (ParamID::soften);
    randomPan   = state.getRawParameterValue (ParamID::randomPan);
    modRate     = { state.getRawParameterValue (ParamID::mod1Rate), state.getRawParameterValue (ParamID::mod2Rate) };
    modDepth    = { state.getRawParameterValue (ParamID::mod1Depth), state.getRawParameterValue (ParamID::mod2Depth) };
    modTarget   = { state.getRawParameterValue (ParamID::mod1Target), state.getRawParameterValue (ParamID::mod2Target) };
}

void RainProcessor::setCurrentProgram (int index)
{
    // Some hosts re-send the current program when restoring a session; don't let that
    // undo edits made on top of it.
    if (index == presets.currentFactoryIndex() && presets.isModified())
        return;
    presets.loadFactory (index);
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
    p.pitchSemis = dsp::detuneSemitones (pitch->load());
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
    p.mode = mode->load() > 0.5f ? dsp::Mode::delay : dsp::Mode::grains;
    p.sync = sync->load() > 0.5f;
    p.syncDivision = juce::jlimit (0, static_cast<int> (dsp::kSyncDivisionBeats.size()) - 1, static_cast<int> (syncRate->load()));
    p.scale = static_cast<dsp::Scale> (juce::jlimit (0, dsp::kNumScales - 1, static_cast<int> (scale->load())));
    p.overlap = overlap->load() > 0.5f;
    p.reverse = reverse->load() * 0.01f;
    p.soften = soften->load() * 0.01f;
    p.randomPan = randomPan->load() * 0.01f;
    for (std::size_t m = 0; m < 2; ++m)
    {
        p.mods[m].rateHz = modRate[m]->load();
        p.mods[m].depth = modDepth[m]->load() * 0.01f;
        p.mods[m].target = static_cast<dsp::ModTarget> (juce::jlimit (0, dsp::kNumModTargets - 1, static_cast<int> (modTarget[m]->load())));
    }
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

    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            if (const auto b = pos->getBpm())
                bpm = *b;
            if (const auto q = pos->getPpqPosition())
            {
                ppq = *q;
                playing = pos->getIsPlaying();
            }
        }
    engine.setTransport (bpm, ppq, playing);

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
    return new RainEditor (*this);
}

void RainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    presets.writeToState (copy);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void RainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            const auto restored = juce::ValueTree::fromXml (*xml);
            state.replaceState (restored);

            // replaceState skips parameters whose stored value already reads the same
            // (a toggle at 0.62 and one at 1.0 are both "on"); set those exactly too.
            for (auto* param : getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                {
                    const auto child = restored.getChildWithProperty ("id", ranged->paramID);
                    if (child.isValid() && child.hasProperty ("value"))
                    {
                        const float target = ranged->convertTo0to1 (static_cast<float> (child.getProperty ("value")));
                        if (std::abs (ranged->getValue() - target) > 1.0e-6f)
                            ranged->setValueNotifyingHost (target);
                    }
                }

            presets.readFromState (restored);
        }
}

} // namespace rain

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new rain::RainProcessor();
}
