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
    sync        = state.getRawParameterValue (ParamID::sync);
    syncRate    = state.getRawParameterValue (ParamID::syncRate);
    scale       = state.getRawParameterValue (ParamID::scale);
    overlap     = state.getRawParameterValue (ParamID::overlap);
    reverse     = state.getRawParameterValue (ParamID::reverse);
    soften      = state.getRawParameterValue (ParamID::soften);
    randomPan   = state.getRawParameterValue (ParamID::randomPan);
    formant     = state.getRawParameterValue (ParamID::formant);
    smoothRandom = state.getRawParameterValue (ParamID::smoothRandom);
    bypass      = state.getRawParameterValue (ParamID::bypass);
    modRate     = { state.getRawParameterValue (ParamID::mod1Rate), state.getRawParameterValue (ParamID::mod2Rate) };
    modDepth    = { state.getRawParameterValue (ParamID::mod1Depth), state.getRawParameterValue (ParamID::mod2Depth) };
    modTarget   = { state.getRawParameterValue (ParamID::mod1Target), state.getRawParameterValue (ParamID::mod2Target) };
}

int RainProcessor::getCurrentProgram()
{
    const int factory = presets.currentFactoryIndex();
    return factory >= 0 ? factory : presets.numFactory();
}

const juce::String RainProcessor::getProgramName (int index)
{
    return index == presets.numFactory() ? juce::String ("User preset") : presets.factoryName (index);
}

void RainProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, presets.numFactory()))
        return; // the "User preset" entry: nothing to load
    // Some hosts re-send the current program when restoring a session; don't let that
    // undo edits made on top of it.
    if (index == presets.currentFactoryIndex() && presets.isModified())
        return;
    presets.loadFactory (index);
}

bool RainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo/stereo, mono in to stereo out (the input is used on both sides) and
    // mono/mono (Rain runs in stereo and sums its output). Nothing wider.
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    return (out == stereo && (in == mono || in == stereo)) || (out == mono && in == mono);
}

void RainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    // The same in every quality, so it only changes here.
    setLatencySamples (engine.latencySamples());
    monoRight.assign (static_cast<std::size_t> (juce::jmax (512, samplesPerBlock)), 0.0f);
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
    p.quality = static_cast<dsp::Quality> (juce::jlimit (0, 4, static_cast<int> (quality->load())));
    p.autoGain = autoGain->load() > 0.5f;
    p.inputGainDb = inputGain->load();
    p.outputGainDb = outputGain->load();
    p.sync = sync->load() > 0.5f;
    p.syncDivision = juce::jlimit (0, static_cast<int> (dsp::kSyncDivisionBeats.size()) - 1, static_cast<int> (syncRate->load()));
    p.scale = static_cast<dsp::Scale> (juce::jlimit (0, dsp::kNumScales - 1, static_cast<int> (scale->load())));
    p.overlap = overlap->load() > 0.5f;
    p.reverse = reverse->load() * 0.01f;
    p.soften = soften->load() * 0.01f;
    p.randomPan = randomPan->load() * 0.01f;
    p.formant = formant->load() > 0.5f;
    p.smoothRandom = smoothRandom->load() > 0.5f;
    p.bypass = bypass->load() > 0.5f;
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

    if (buffer.getNumChannels() > 1)
        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    else
        processMonoOut (buffer, numSamples);
}

void RainProcessor::processMonoOut (juce::AudioBuffer<float>& buffer, int numSamples)
{
    // A mono track: run in stereo with the input on both sides, in pieces the size of
    // the preallocated right channel (hosts may send bigger blocks than they said),
    // then sum to mono.
    auto* left = buffer.getWritePointer (0);
    const int chunk = static_cast<int> (monoRight.size());
    for (int start = 0; start < numSamples; start += chunk)
    {
        const int n = juce::jmin (chunk, numSamples - start);
        std::copy (left + start, left + start + n, monoRight.begin());
        engine.process (left + start, monoRight.data(), n);
        for (int i = 0; i < n; ++i)
            left[start + i] = 0.5f * (left[start + i] + monoRight[static_cast<std::size_t> (i)]);
    }
}

juce::AudioProcessorEditor* RainProcessor::createEditor()
{
    return new RainEditor (*this);
}

void RainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty ("stateVersion", kStateVersion, nullptr);
    presets.writeToState (copy);
    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void RainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto restored = juce::ValueTree::fromXml (*xml);

            // Sessions from other versions: drop parameters Rain no longer has (such as
            // the old Grains/Delay mode), and give any it has gained their defaults
            // rather than leaving whatever the plugin happened to be set to.
            for (int i = restored.getNumChildren(); --i >= 0;)
            {
                const auto child = restored.getChild (i);
                if (child.hasType ("PARAM") && state.getParameter (child.getProperty ("id").toString()) == nullptr)
                    restored.removeChild (i, nullptr);
            }
            for (auto* param : getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                    if (! restored.getChildWithProperty ("id", ranged->paramID).isValid())
                        restored.appendChild (juce::ValueTree ("PARAM", { { "id", ranged->paramID },
                                                                          { "value", ranged->convertFrom0to1 (ranged->getDefaultValue()) } }),
                                              nullptr);
            restored.removeProperty ("stateVersion", nullptr);

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
