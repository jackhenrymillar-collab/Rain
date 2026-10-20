#include "RainEngine.h"

#include "beads/dsp/processor.h"
#include "stmlib/utils/gate_flags.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rain::dsp
{

namespace
{
    // Memory layout of the module: 768 kB of sample memory in two blocks, plus the reverb.
    constexpr std::size_t kBlock1Size = 512 * 1024;
    constexpr std::size_t kBlock2Size = 256 * 1024;
    constexpr std::size_t kReverbSize = 32768;

    beads::Processor::Media mediaFor (Quality q)
    {
        switch (q)
        {
            case Quality::digital:   return beads::Processor::MEDIA_DIGITAL;
            case Quality::vintage:   return beads::Processor::MEDIA_VINTAGE_DIGITAL;
            case Quality::cleanTape: return beads::Processor::MEDIA_CLEAN_TAPE;
            case Quality::dirtyTape: return beads::Processor::MEDIA_DIRTY_TAPE;
            case Quality::studio:    return beads::Processor::MEDIA_STUDIO;
        }
        return beads::Processor::MEDIA_DIGITAL;
    }

    // Beads' mix pots are read with a little headroom so fully clockwise really is 1.
    float mixPot (float v) { return std::clamp (v * 1.005f, 0.0f, 1.0f); }

    // Headroom inside Beads (see RainEngine.h).
    constexpr float kHeadroom = 0.5f;

    constexpr float kBeadsRates[] = { 24000.0f, 32000.0f, 48000.0f };

    int rateIndex (float rate)
    {
        return rate < 28000.0f ? 0 : rate < 40000.0f ? 1 : 2;
    }

    // The module's pot filter: ONE_POLE with 0.05 at its 4 kHz block rate (about 5 ms).
    // Grain controls use it: they only shape grains as they start, so they can be quick.
    constexpr float kKnobSmoothing = 0.05f;
    // Mix controls change the sound that is already playing, so they glide for longer
    // (about 20 ms) to stay free of zipper noise when automated or changed by a preset.
    constexpr float kMixSmoothing = 0.0125f;

    // Correlated randomness: how fast the random drifts for TIME, SIZE, SHAPE and PITCH
    // wander (a new random level every few seconds, eased between), and the share of
    // each grain's random value that is fresh per grain.
    constexpr float kDriftRatesHz[] = { 0.15f, 0.25f, 0.25f, 0.2f };
    constexpr float kGrainJitter = 0.2f;
}

RainEngine::RainEngine() = default;
RainEngine::~RainEngine() = default;

float RainEngine::attenuverterLaw (float knob) noexcept
{
    const float v = 0.5f * std::clamp (knob, -1.0f, 1.0f);
    return (2.0f / 0.47f) * v * std::max (std::fabs (v) - 0.03f, 0.0f);
}

float RainEngine::bufferSeconds (Quality q) noexcept
{
    // Beads' 768 kB of sample memory, as stereo frames at each quality's resolution,
    // rate and decimation (see beads::Processor).
    switch (q)
    {
        case Quality::digital:   return 192000.0f / 48000.0f;        // 16-bit
        case Quality::vintage:   return 256000.0f / 32000.0f;        // 12-bit
        case Quality::cleanTape: return 256000.0f * 2.0f / 48000.0f; // 12-bit, half rate
        case Quality::dirtyTape: return 384000.0f * 2.0f / 48000.0f; // 8-bit, half rate
        case Quality::studio:    return static_cast<float> (beads::kStudioDelayFrames) / 48000.0f; // float
    }
    return 4.0f;
}

namespace
{
    constexpr float kForwardSize = 0.3333333f; // Beads' SIZE where grains turn forward
    constexpr float kMaxGrainSize = 0.975f;    // just short of Beads' delay mode
    constexpr float kMaxTriggerRate = 130.81278f;
}

float RainEngine::beadsSize (float size) noexcept
{
    return kForwardSize + std::clamp (size, 0.0f, 1.0f) * (kMaxGrainSize - kForwardSize);
}

float RainEngine::grainSeconds (float b) noexcept
{
    // beads::GranularPlayer::ComputeGrainDuration, forward half.
    const float clamped = 1.0f - 1.5f * (std::max (b, kForwardSize) - kForwardSize);
    const float shortness = std::min (1.0f, 1.025f * clamped);
    return 4.0f * std::exp2 (shortness * -9.0f);
}

float RainEngine::beadsSizeForSeconds (float seconds) noexcept
{
    const float shortness = std::clamp (std::log2 (4.0f / std::max (seconds, 1.0e-4f)) / 9.0f, 0.0f, 1.0f);
    const float b = kForwardSize + (1.0f - shortness / 1.025f) / 1.5f;
    return std::clamp (b, kForwardSize, kMaxGrainSize);
}

float RainEngine::overlapDensity (float d, float grainLength) noexcept
{
    const float rate = grainRate (d, 0.0f);
    if (rate <= 0.0f)
        return d;
    // Regular grains overlap twice over; random ones need a little more to leave no gaps.
    const bool regular = d < 0.5f;
    const float needed = std::min ((regular ? 2.0f : 3.0f) / std::max (grainLength, 1.0e-3f), 0.5f * kMaxTriggerRate);
    if (rate >= needed)
        return d;
    // Invert grainRate on the same side of noon.
    return regular ? std::clamp (-std::log2 (needed / kMaxTriggerRate) / 16.0f, 0.0f, 0.48f)
                   : std::clamp (1.0f + std::log2 (needed * 0.883f * 2.0f / kMaxTriggerRate) / 16.0f, 0.52f, 1.0f);
}

float RainEngine::grainRate (float d, float clockHz) noexcept
{
    if (clockHz > 0.0f)
    {
        const int divider = syncDivider (d);
        if (divider > 0)
            return clockHz / static_cast<float> (divider);
        const float p = std::clamp ((d - 0.52f) * 2.15f, 0.0f, 1.0f);
        return clockHz * p * p;
    }
    // beads::TriggerGenerator: 16 octaves either side of noon. Random triggers
    // average 0.883 periods apart (its approximated exponential intervals).
    if (d < 0.48f)
        return kMaxTriggerRate * std::exp2 (-16.0f * d);
    if (d > 0.52f)
        return 0.5f * kMaxTriggerRate * std::exp2 (-16.0f * (1.0f - d)) / 0.883f;
    return 0.0f;
}

void RainEngine::setTransport (double newBpm, double ppqPosition, bool playing) noexcept
{
    if (newBpm > 1.0 && newBpm < 1000.0)
        bpm = newBpm;
    if (playing)
        beatPos = ppqPosition;
}

void RainEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    fs = sampleRate;

    // A host re-preparing (new block size, an offline bounce) keeps what was recorded.
    const bool keepRecording = processor != nullptr;
    if (! processor)
    {
        block1.assign (kBlock1Size, 0);
        block2.assign (kBlock2Size, 0);
        reverbBuffer.assign (kReverbSize, 0);
        studioBuffer.assign (beads::kStudioBufferSize, 0);
        studioReverb.assign (beads::kStudioReverbSize, 0.0f);
        carry.assign (kCarryFrames, beads::FloatFrame { 0.0f, 0.0f });
        carryOut.assign (kCarryFrames, beads::FloatFrame { 0.0f, 0.0f });
        processor = std::make_unique<beads::Processor>();
        std::uint8_t* buffers[3] = { block1.data(), block1.data() + kBlock1Size / 2, block2.data() };
        std::uint8_t* studio[3] = { studioBuffer.data(), studioBuffer.data() + beads::kStudioBankSize,
                                    studioBuffer.data() + 2 * beads::kStudioBankSize };
        processor->Init (buffers, reverbBuffer.data(), studio, studioReverb.data());
    }

    for (int i = 0; i < kNumRates; ++i)
    {
        toBeadsAt[static_cast<size_t> (i)].prepare (fs, kBeadsRates[i]);
        fromBeadsAt[static_cast<size_t> (i)].prepare (kBeadsRates[i], fs);
        gainFromBeadsAt[static_cast<size_t> (i)].prepare (kBeadsRates[i], fs);
    }
    subsonic.prepare (fs);
    limiter.prepare (fs, 0.977f); // -0.2 dBFS
    smoothedValid = false;

    fifo.assign (kFifoSize, OutFrame {});
    dryDelay.assign (kDryDelaySize, beads::FloatFrame { 0.0f, 0.0f });
    dryWrite = 0;
    bypassStep = static_cast<float> (1.0 / (0.02 * fs));
    bypassMix = params.bypass ? 1.0f : 0.0f;
    outputGain.prepare (fs, 20.0f);
    outputGain.snapTo (std::pow (10.0f, params.outputGainDb / 20.0f));
    prepared = true;
    lastDryGain = 0.0f;
    qualityPending = false;
    qualityFade = 1.0f;
    qualityFadeStep = static_cast<float> (1.0 / (0.008 * fs)); // 8 ms each way
    switching.stage = SwitchStage::idle;
    startSwitch (params.quality, keepRecording);
    while (! advanceSwitch (std::numeric_limits<long long>::max())) {} // all at once: not on the audio thread

    // Give the panel sensible values before the first block runs.
    RainParams initial = params;
    applyMotion (initial, 0.0f);
}

void RainEngine::reset()
{
    if (! prepared)
        return;
    // Hosts reset on transport stop or when suspending processing; like the module,
    // Rain keeps its recording (frozen or not) and only clears the reverb tail.
    std::fill (reverbBuffer.begin(), reverbBuffer.end(), std::uint16_t { 0 });
    std::fill (studioReverb.begin(), studioReverb.end(), 0.0f);
    outputGain.snapTo (outputGain.getTarget());
    subsonic.reset();
    limiter.reset();
    std::fill (dryDelay.begin(), dryDelay.end(), beads::FloatFrame { 0.0f, 0.0f });
    bypassMix = params.bypass ? 1.0f : 0.0f;
    smoothedValid = false;
    wave.reset();
    drift.reset();
    // Beads keeps its buffer; only the host-side converters and FIFO start afresh.
    if (switching.stage == SwitchStage::idle)
        configureHost();
}

void RainEngine::configureHost()
{
    // The host-side half of a quality: converters, FIFO and latency for Beads' rate.
    beadsRate = processor->sample_rate();
    beadsBlock = std::clamp (static_cast<int> (beadsRate / 4000.0f), 1, kMaxBeadsBlock);

    const auto r = static_cast<size_t> (rateIndex (beadsRate));
    toBeads = &toBeadsAt[r];
    fromBeads = &fromBeadsAt[r];
    gainFromBeads = &gainFromBeadsAt[r];
    toBeads->reset();
    fromBeads->reset();
    gainFromBeads->resetTo (beads::FloatFrame { lastDryGain, 0.0f }); // the dry level carries on

    const auto clipping = configured == Quality::dirtyTape ? InputGain::SoftClipping::on
                        : configured == Quality::cleanTape ? InputGain::SoftClipping::dynamic
                                                           : InputGain::SoftClipping::off;
    inputGain.init (beadsRate, clipping, true);
    mon.bufferSeconds.store (bufferSeconds (configured), std::memory_order_relaxed);

    beadsFill = 0;

    // Prime the output FIFO so a full Beads block can always be produced in time, plus
    // padding so every quality has the same latency: the host never sees it change and
    // the dry signal never jumps when quality does.
    const auto latencyAt = [this] (std::size_t rate) {
        const double hostPerBeads = fs / static_cast<double> (kBeadsRates[rate]);
        const int block = std::clamp (static_cast<int> (kBeadsRates[rate] / 4000.0f), 1, kMaxBeadsBlock);
        const int prime = static_cast<int> (std::ceil (block * hostPerBeads)) + 4;
        return prime + static_cast<int> (std::lround (toBeadsAt[rate].delay() + fromBeadsAt[rate].delay() * hostPerBeads));
    };
    int longest = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t> (kNumRates); ++i)
        longest = std::max (longest, latencyAt (i));
    const double hostPerBeads = fs / static_cast<double> (beadsRate);
    const int prime = static_cast<int> (std::ceil (beadsBlock * hostPerBeads)) + 4 + (longest - latencyAt (r));
    fifoRead = fifoWrite = 0;
    // Prime with the dry gain in use, so the dry signal carries on through a quality change.
    for (int i = 0; i < prime; ++i)
        pushOutput (OutFrame { {}, lastDryGain });

    latency = longest + limiter.latency();
}

// Changing quality. Each quality has its own buffer format and rate (and four of
// them share the same memory), so the recording is copied out, resampled to the new
// rate (low-passed first when that is lower), and written into the new buffer. That
// is up to 400,000 frames of work, so it runs a slice per audio callback while the
// grains are faded out; Beads pauses meanwhile and the dry signal carries on.
void RainEngine::startSwitch (Quality target, bool keepRecording)
{
    auto& sw = switching;
    sw.target = target;
    sw.pos = 0;
    sw.frames = keepRecording ? std::min (processor->history_length(), static_cast<int> (carry.size())) : 0;
    sw.fromRate = processor->history_rate();
    sw.stage = sw.frames > 4 ? SwitchStage::exporting : SwitchStage::configuring;
}

bool RainEngine::advanceSwitch (long long budget)
{
    auto& sw = switching;
    const long long fullBudget = budget;
    const auto slice = [&] (int total) {
        const int n = static_cast<int> (std::min<long long> (budget, total - sw.pos));
        budget -= n;
        return n;
    };
    while (sw.stage != SwitchStage::idle && budget > 0)
    {
        switch (sw.stage)
        {
            case SwitchStage::exporting:
            {
                const int n = slice (sw.frames);
                processor->ExportHistory (carry.data() + sw.pos, sw.pos, n);
                sw.pos += n;
                if (sw.pos < sw.frames)
                    break;
                // Plan the resampling: the new quality's storage rate.
                sw.toRate = beads::Processor::HistoryRateFor (mediaFor (sw.target));
                sw.step = static_cast<double> (sw.fromRate) / sw.toRate; // old frames per new frame
                sw.out = std::min (beads::Processor::HistoryCapacityFor (mediaFor (sw.target)),
                                   static_cast<int> (static_cast<double> (sw.frames - 3) / sw.step));
                sw.pos = 0;
                sw.pass = 0;
                sw.x1 = sw.x2 = sw.y1 = sw.y2 = {};
                sw.stage = sw.step > 1.0001 ? SwitchStage::filtering : SwitchStage::resampling;
                if (sw.out <= 0)
                    sw.stage = SwitchStage::configuring;
                break;
            }
            case SwitchStage::filtering:
            {
                // Two passes of a Butterworth low-pass at 45% of the new rate (24 dB/octave),
                // oldest to newest (carry[] is newest first).
                const double w = 2.0 * 3.141592653589793 * 0.45 * sw.toRate / sw.fromRate;
                const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * 0.7071067811865476);
                const double a0 = 1.0 + alpha;
                const float b0 = static_cast<float> ((1.0 - cw) * 0.5 / a0), b1 = static_cast<float> ((1.0 - cw) / a0);
                const float a1 = static_cast<float> (-2.0 * cw / a0), a2 = static_cast<float> ((1.0 - alpha) / a0);
                const int n = slice (sw.frames);
                for (int k = 0; k < n; ++k)
                {
                    auto& x = carry[static_cast<std::size_t> (sw.frames - 1 - (sw.pos + k))];
                    beads::FloatFrame y;
                    y.l = b0 * x.l + b1 * sw.x1.l + b0 * sw.x2.l - a1 * sw.y1.l - a2 * sw.y2.l;
                    y.r = b0 * x.r + b1 * sw.x1.r + b0 * sw.x2.r - a1 * sw.y1.r - a2 * sw.y2.r;
                    sw.x2 = sw.x1; sw.x1 = x; sw.y2 = sw.y1; sw.y1 = y;
                    x = y;
                }
                sw.pos += n;
                if (sw.pos < sw.frames)
                    break;
                sw.pos = 0;
                sw.x1 = sw.x2 = sw.y1 = sw.y2 = {};
                if (++sw.pass == 2)
                    sw.stage = SwitchStage::resampling;
                break;
            }
            case SwitchStage::resampling:
            {
                // New frame j (newest first) reads old position j * step; carryOut is
                // written oldest first, ready to import.
                const int frames = sw.frames;
                const auto at = [&] (int i) { return carry[static_cast<std::size_t> (std::clamp (i, 0, frames - 1))]; };
                const int n = slice (sw.out);
                for (int k = 0; k < n; ++k)
                {
                    const int j = sw.pos + k;
                    const double where = static_cast<double> (j) * sw.step;
                    const int i = static_cast<int> (where);
                    const float t = static_cast<float> (where - i);
                    const auto xm = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
                    const auto hermite = [t] (float a, float b, float c, float d) {
                        const float c0 = b, c1 = 0.5f * (c - a), c2 = a - 2.5f * b + 2.0f * c - 0.5f * d;
                        const float c3 = 0.5f * (d - a) + 1.5f * (b - c);
                        return ((c3 * t + c2) * t + c1) * t + c0;
                    };
                    carryOut[static_cast<std::size_t> (sw.out - 1 - j)] = { hermite (xm.l, x0.l, x1.l, x2.l), hermite (xm.r, x0.r, x1.r, x2.r) };
                }
                sw.pos += n;
                if (sw.pos >= sw.out)
                    sw.stage = SwitchStage::configuring;
                break;
            }
            case SwitchStage::configuring:
            {
                // Clearing the new quality's buffer is the biggest single step (about
                // 0.2 ms for Studio), so it gets a callback to itself.
                if (budget < fullBudget)
                    return false;
                budget = 0;
                configured = sw.target;
                processor->Configure (mediaFor (sw.target), false);
                sw.pos = 0;
                if (sw.frames > 4 && sw.out > 0)
                {
                    processor->BeginImport();
                    sw.stage = SwitchStage::importing;
                }
                else
                {
                    sw.stage = SwitchStage::idle;
                }
                break;
            }
            case SwitchStage::importing:
            {
                const int n = slice (sw.out);
                processor->ImportHistory (carryOut.data() + sw.pos, n);
                sw.pos += n;
                if (sw.pos >= sw.out)
                    sw.stage = SwitchStage::idle;
                break;
            }
            case SwitchStage::idle:
                break;
        }
    }
    if (sw.stage != SwitchStage::idle)
        return false;
    configureHost();
    return true;
}

void RainEngine::setParams (const RainParams& p)
{
    params = p;
    outputGain.setTarget (std::pow (10.0f, p.outputGainDb / 20.0f));
    // A new quality takes effect once the grains have faded out (see process()), so
    // the switch is a quick dip rather than a click.
    qualityPending = prepared && p.quality != configured;
}

void RainEngine::pushOutput (const OutFrame& f)
{
    const std::size_t next = (fifoWrite + 1) & (kFifoSize - 1);
    if (next == fifoRead)
        return; // full; cannot happen in steady state
    fifo[fifoWrite] = f;
    fifoWrite = next;
}

void RainEngine::applyMotion (RainParams& p, float dt)
{
    // The two modulators.
    float* targets[kNumModTargets] = { nullptr, &p.time, &p.size, &p.shape, &p.pitchSemis,
                                       &p.density, &p.feedback, &p.dryWet, &p.reverb };
    const float values[2] = { wave.advance (p.mods[0].rateHz, dt), drift.advance (p.mods[1].rateHz, dt) };
    for (std::size_t m = 0; m < 2; ++m)
    {
        mon.modValue[m].store (values[m], std::memory_order_relaxed);
        const auto t = static_cast<int> (p.mods[m].target);
        if (t <= 0 || t >= kNumModTargets)
            continue;
        // Full depth sweeps half the knob's range either way; for pitch, an octave.
        const float span = p.mods[m].target == ModTarget::pitch ? 12.0f : 0.5f;
        *targets[t] += std::clamp (p.mods[m].depth, -1.0f, 1.0f) * values[m] * span;
    }
    for (int t = 1; t < kNumModTargets; ++t)
        if (t != static_cast<int> (ModTarget::pitch))
            *targets[t] = std::clamp (*targets[t], 0.0f, 1.0f);
    p.pitchSemis = std::clamp (p.pitchSemis, -24.0f, 24.0f);

    // Synced grains read from whole clock steps back in the buffer.
    if (p.sync)
    {
        const auto idx = static_cast<std::size_t> (std::clamp (p.syncDivision, 0, static_cast<int> (kSyncDivisionBeats.size()) - 1));
        const float stepSeconds = kSyncDivisionBeats[idx] * 60.0f / static_cast<float> (bpm);
        const float buffer = bufferSeconds (configured);
        const float steps = std::round (p.time * buffer / stepSeconds);
        p.time = std::clamp (steps * stepSeconds / buffer, 0.0f, 1.0f);
    }

    for (int t = 1; t < kNumModTargets; ++t)
    {
        const float v = t == static_cast<int> (ModTarget::pitch) ? 0.5f * (detunePosition (p.pitchSemis) + 1.0f) : *targets[t];
        mon.effective[static_cast<std::size_t> (t)].store (v, std::memory_order_relaxed);
    }
}

void RainEngine::runBeadsBlock()
{
    const auto n = static_cast<std::size_t> (beadsBlock);

    inputGain.process (params.autoGain, params.inputGainDb, beadsIn.data(), n);

    // Level meter for the panel's buffer display; stands still while frozen.
    const int levelPeriod = static_cast<int> (beadsRate) / EngineMonitor::kLevelRate;
    float blockPeak = 0.0f;
    for (std::size_t i = 0; i < n; ++i)
    {
        const float peak = std::max (std::fabs (beadsIn[i].l), std::fabs (beadsIn[i].r));
        blockPeak = std::max (blockPeak, peak);
        levelPeak = std::max (levelPeak, peak);
        if (++levelCount >= levelPeriod)
        {
            if (! params.freeze)
            {
                const int w = mon.levelWrite.load (std::memory_order_relaxed);
                mon.levels[static_cast<std::size_t> (w)].store (levelPeak, std::memory_order_relaxed);
                mon.levelWrite.store ((w + 1) % EngineMonitor::kNumLevels, std::memory_order_release);
            }
            levelPeak = 0.0f;
            levelCount = 0;
        }
    }
    if (blockPeak > mon.inputPeak.load (std::memory_order_relaxed))
        mon.inputPeak.store (blockPeak, std::memory_order_relaxed);

    // Smooth the knobs like the module's pot readers, so host automation and preset
    // changes glide instead of stepping.
    if (! smoothedValid)
    {
        smoothed = params;
        smoothedValid = true;
    }
    RainParams q = params;
    for (auto [target, state] : { std::pair { &q.time, &smoothed.time }, { &q.size, &smoothed.size }, { &q.shape, &smoothed.shape },
                                  { &q.density, &smoothed.density }, { &q.pitchSemis, &smoothed.pitchSemis },
                                  { &q.timeRandom, &smoothed.timeRandom }, { &q.sizeRandom, &smoothed.sizeRandom },
                                  { &q.shapeRandom, &smoothed.shapeRandom }, { &q.pitchRandom, &smoothed.pitchRandom },
                                  { &q.reverse, &smoothed.reverse }, { &q.randomPan, &smoothed.randomPan } })
    {
        *state += kKnobSmoothing * (*target - *state);
        *target = *state;
    }
    for (auto [target, state] : { std::pair { &q.feedback, &smoothed.feedback }, { &q.dryWet, &smoothed.dryWet },
                                  { &q.reverb, &smoothed.reverb }, { &q.soften, &smoothed.soften } })
    {
        *state += kMixSmoothing * (*target - *state);
        *target = *state;
    }
    applyMotion (q, static_cast<float> (n) / beadsRate);

    beads::Parameters p;
    auto knob = [] (float base, float random) {
        beads::RandomizedParameter r;
        r.base = base;
        r.attenuverter = attenuverterLaw (random);
        r.cv = 0.0f;
        r.has_cv = false;
        return r;
    };
    // SIZE sets a forward grain length.
    const float size = beadsSize (q.size);
    mon.grainSeconds.store (grainSeconds (size), std::memory_order_relaxed);

    // Overlap keeps the grain length and fills the gaps: when grains are too sparse
    // to overlap, it raises the grain rate until they do. Synced grains stay on the
    // clock, and DENSITY at noon still means no grains.
    float beadsDensity = q.density;
    const bool clocked = q.sync && (q.seed || q.seedLock);
    if (q.overlap && ! clocked)
        beadsDensity = overlapDensity (q.density, grainSeconds (size));
    mon.spawnDensity.store (beadsDensity, std::memory_order_relaxed);

    p.time = knob (q.time, q.timeRandom);
    p.size = knob (size, q.sizeRandom);
    p.shape = knob (q.shape, q.shapeRandom);
    p.pitch = knob (q.pitchSemis, q.pitchRandom);

    // Correlated randomness: rather than a fresh random value for every grain, each
    // randomised control follows a slowly wandering random value, plus a little
    // per-grain variation (kGrainJitter of it), so textures evolve instead of jitter.
    {
        const float dt = static_cast<float> (n) / beadsRate;
        beads::RandomizedParameter* targets[] = { &p.time, &p.size, &p.shape, &p.pitch };
        for (std::size_t k = 0; k < randomDrift.size(); ++k)
        {
            const float d = randomDrift[k].advance (kDriftRatesHz[k], dt);
            targets[k]->drift = d;
            targets[k]->jitter = kGrainJitter;
            mon.randomDrift[k].store (d, std::memory_order_relaxed);
        }
    }
    p.monophonic_mode = false;
    p.reverse_probability = std::clamp (q.reverse, 0.0f, 1.0f);
    p.random_pan = std::clamp (q.randomPan, 0.0f, 1.0f);

    // Grain quality: Digital and Studio read grains with a band-limited interpolator,
    // line new grains up in phase with the one before, and glide briefly between
    // grain pitches. The lo-fi qualities keep the module's own grain reader.
    const bool clean = configured == Quality::digital || configured == Quality::studio;
    p.clean_reader = clean;
    p.phase_align = clean;
    p.pitch_glide = clean ? 0.012f : 0.0f;
    p.formant = q.formant;

    if (q.scale != Scale::off)
    {
        // To keep every grain in the scale, Rain draws the random pitch itself, the way
        // Beads would, and hands Beads the snapped result as a fixed value (a "CV" of 0).
        // Notes are drawn in proportion to how consonant they are, so the root, fifth
        // and thirds come up more often than the other scale notes.
        const float a = p.pitch.attenuverter;
        const float blend = a > 0.0f ? a : std::min (1.0f, a * pitchRng.uniform() * -3.0f);
        float note = quantisePitch (q.pitchSemis, q.scale);
        if (blend > 0.0f)
        {
            for (int attempt = 0; attempt < 6; ++attempt)
            {
                const float r = (1.0f - kGrainJitter) * p.pitch.drift + kGrainJitter * pitchRng.centred();
                note = quantisePitch (q.pitchSemis + r * 24.0f * blend, q.scale);
                if (pitchRng.uniform() < consonance (note))
                    break;
            }
        }
        p.pitch.base = note;
        p.pitch.has_cv = true;
    }

    beads::ModulatedParameter density;
    density.base = beadsDensity;
    density.cv = 0.0f;

    beads::Processor::MixParameters m;
    m.feedback = mixPot (q.feedback);
    m.dry_wet = mixPot (q.dryWet);
    m.reverb_amount = mixPot (q.reverb);
    m.output_gate = false;
    m.soften = q.soften;
    m.direct_gain = beadsDryGain.data();

    bool freeze[kMaxBeadsBlock];
    std::fill (freeze, freeze + kMaxBeadsBlock, q.freeze);

    // On the module SEED is latched ("locked") at power-up, so grains run continuously.
    // Unlocked, grains only play while SEED is held (or its gate input is high).
    auto seed = q.seed     ? beads::SEED_BUTTON_STATE_ON
              : q.seedLock ? beads::SEED_BUTTON_STATE_LOCKED
                           : beads::SEED_BUTTON_STATE_OFF;

    // Tempo sync: a square-wave clock into the SEED input. Beads only follows a clock
    // while SEED is locked, so holding SEED with lock off also counts as locked here.
    stmlib::GateFlags clock[kMaxBeadsBlock];
    const stmlib::GateFlags* clockInput = nullptr;
    if (q.sync && seed != beads::SEED_BUTTON_STATE_OFF)
    {
        const auto idx = static_cast<std::size_t> (std::clamp (q.syncDivision, 0, static_cast<int> (kSyncDivisionBeats.size()) - 1));
        const double step = kSyncDivisionBeats[idx];
        for (std::size_t i = 0; i < n; ++i)
        {
            const double pos = beadsBeat[i] / step;
            const bool high = pos - std::floor (pos) < 0.5;
            clock[i] = clockFlags = stmlib::ExtractGateFlags (clockFlags, high);
        }
        clockInput = clock;
        seed = beads::SEED_BUTTON_STATE_LOCKED;
    }

    processor->Process (seed, clockInput, freeze, density, p, m, beadsIn.data(), beadsOut.data(), n);

    for (std::size_t i = 0; i < n; ++i)
    {
        // Beads' own output stage clamps to its converter's range.
        beads::FloatFrame f = beadsOut[i];
        f.l = (std::isfinite (f.l) ? std::clamp (f.l, -1.0f, 1.0f) : 0.0f) / kHeadroom;
        f.r = (std::isfinite (f.r) ? std::clamp (f.r, -1.0f, 1.0f) : 0.0f) / kHeadroom;

        // The wet signal and the dry gain go through identical converters, so they
        // emit the same number of frames.
        // Up to 32 host frames per Beads frame: host rates up to 768 kHz.
        std::array<beads::FloatFrame, 32> wet;
        std::array<float, 32> gain;
        std::size_t numWet = 0, numGain = 0;
        fromBeads->push (f, [&] (const beads::FloatFrame& g) { if (numWet < wet.size()) wet[numWet++] = g; });
        gainFromBeads->push (beads::FloatFrame { beadsDryGain[i], 0.0f },
                             [&] (const beads::FloatFrame& g) { if (numGain < gain.size()) gain[numGain++] = g.l; });
        for (std::size_t k = 0; k < numWet; ++k)
            pushOutput ({ wet[k], k < numGain ? gain[k] : 0.0f });
    }
}

void RainEngine::process (float* left, float* right, int numSamples)
{
    if (! prepared)
        return;
    // A quality change, once the grains have faded out: a slice of the work per callback.
    if (switching.stage == SwitchStage::idle && qualityPending && qualityFade <= 0.0f)
        startSwitch (params.quality, true);
    const bool beadsPaused = switching.stage != SwitchStage::idle;
    if (beadsPaused && advanceSwitch (std::max<long long> (4096, 256LL * numSamples)))
        qualityPending = params.quality != configured;

    const float bypassTarget = params.bypass ? 1.0f : 0.0f;
    float outPeak = 0.0f;
    const auto dryLag = static_cast<std::size_t> (std::clamp (latency, 0, static_cast<int> (kDryDelaySize) - 1));
    // The dry signal joins the wet before the output limiter, so it lags by less.
    const auto mixLag = static_cast<std::size_t> (std::clamp (latency - limiter.latency(), 0, static_cast<int> (kDryDelaySize) - 1));
    for (int i = 0; i < numSamples; ++i)
    {
        dryDelay[dryWrite] = { left[i], right[i] };
        const beads::FloatFrame dry = dryDelay[(dryWrite - dryLag) & (kDryDelaySize - 1)];
        const beads::FloatFrame dryMix = dryDelay[(dryWrite - mixLag) & (kDryDelaySize - 1)];
        dryWrite = (dryWrite + 1) & (kDryDelaySize - 1);

        // A stray NaN or infinity from the host would stick in Beads' buffers and filters.
        const auto finite = [] (float v) { return std::isfinite (v) ? v : 0.0f; };
        const beads::FloatFrame x { finite (left[i]) * kHeadroom, finite (right[i]) * kHeadroom };
        beatPos += bpm / (60.0 * fs);
        if (! beadsPaused)
        toBeads->push (x, [this] (const beads::FloatFrame& f) {
            beadsBeat[static_cast<std::size_t> (beadsFill)] = beatPos;
            beadsIn[static_cast<std::size_t> (beadsFill++)] = f;
            if (beadsFill == beadsBlock)
            {
                runBeadsBlock();
                beadsFill = 0;
            }
        });

        OutFrame y { {}, lastDryGain };
        if (! beadsPaused && fifoRead != fifoWrite)
        {
            y = fifo[fifoRead];
            fifoRead = (fifoRead + 1) & (kFifoSize - 1);
            lastDryGain = y.dryGain;
        }
        const float g = outputGain.next();
        // Grains (and reverb) from Beads, plus the untouched dry input at the gain
        // Beads would have given it.
        qualityFade = qualityPending ? std::max (0.0f, qualityFade - qualityFadeStep)
                                     : std::min (1.0f, qualityFade + qualityFadeStep);
        float l = (subsonic.process (y.wet.l * qualityFade, 0) + y.dryGain * dryMix.l) * g;
        float r = (subsonic.process (y.wet.r * qualityFade, 1) + y.dryGain * dryMix.r) * g;
        limiter.process (l, r);
        if (bypassMix != bypassTarget)
            bypassMix = bypassTarget > bypassMix ? std::min (bypassTarget, bypassMix + bypassStep)
                                                 : std::max (bypassTarget, bypassMix - bypassStep);
        left[i] = l + bypassMix * (dry.l - l);
        right[i] = r + bypassMix * (dry.r - r);
        outPeak = std::max ({ outPeak, std::fabs (left[i]), std::fabs (right[i]) });
    }
    if (outPeak > mon.outputPeak.load (std::memory_order_relaxed))
        mon.outputPeak.store (outPeak, std::memory_order_relaxed);

    mon.bpm.store (static_cast<float> (bpm), std::memory_order_relaxed);
    mon.beat.store (beatPos, std::memory_order_relaxed);
}

} // namespace rain::dsp
