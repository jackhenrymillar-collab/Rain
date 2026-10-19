#include "RainEngine.h"

#include "beads/dsp/processor.h"
#include "stmlib/utils/gate_flags.h"

#include <algorithm>
#include <cmath>

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

    // The module's pot filter: ONE_POLE with 0.05 at its 4 kHz block rate.
    constexpr float kKnobSmoothing = 0.05f;
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
    }
    return 4.0f;
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

    if (! processor)
    {
        block1.assign (kBlock1Size, 0);
        block2.assign (kBlock2Size, 0);
        reverbBuffer.assign (kReverbSize, 0);
        processor = std::make_unique<beads::Processor>();
        std::uint8_t* buffers[3] = { block1.data(), block1.data() + kBlock1Size / 2, block2.data() };
        processor->Init (buffers, reverbBuffer.data());
    }

    for (int i = 0; i < kNumRates; ++i)
    {
        toBeadsAt[static_cast<size_t> (i)].prepare (fs, kBeadsRates[i]);
        fromBeadsAt[static_cast<size_t> (i)].prepare (kBeadsRates[i], fs);
    }
    subsonic.prepare (fs);
    limiter.prepare (fs, 0.977f); // -0.2 dBFS
    smoothedValid = false;

    fifo.assign (kFifoSize, beads::FloatFrame { 0.0f, 0.0f });
    outputGain.prepare (fs, 20.0f);
    outputGain.snapTo (std::pow (10.0f, params.outputGainDb / 20.0f));
    prepared = true;
    configure (params.quality);

    // Give the panel sensible values before the first block runs.
    RainParams initial = params;
    applyMotion (initial, 0.0f);
}

void RainEngine::reset()
{
    if (! prepared)
        return;
    std::fill (block1.begin(), block1.end(), std::uint8_t { 0 });
    std::fill (block2.begin(), block2.end(), std::uint8_t { 0 });
    std::fill (reverbBuffer.begin(), reverbBuffer.end(), std::uint16_t { 0 });
    outputGain.snapTo (outputGain.getTarget());
    subsonic.reset();
    limiter.reset();
    smoothedValid = false;
    wave.reset();
    drift.reset();
    for (auto& l : mon.levels)
        l.store (0.0f, std::memory_order_relaxed);
    configure (configured);
}

void RainEngine::configure (Quality q)
{
    configured = q;
    processor->Configure (mediaFor (q), false);

    beadsRate = processor->sample_rate();
    beadsBlock = std::clamp (static_cast<int> (beadsRate / 4000.0f), 1, kMaxBeadsBlock);

    const auto r = static_cast<size_t> (rateIndex (beadsRate));
    toBeads = &toBeadsAt[r];
    fromBeads = &fromBeadsAt[r];
    toBeads->reset();
    fromBeads->reset();

    const auto clipping = q == Quality::dirtyTape ? InputGain::SoftClipping::on
                        : q == Quality::cleanTape ? InputGain::SoftClipping::dynamic
                                                  : InputGain::SoftClipping::off;
    inputGain.init (beadsRate, clipping, true);
    mon.bufferSeconds.store (bufferSeconds (q), std::memory_order_relaxed);

    beadsFill = 0;

    // Prime the output FIFO so a full Beads block can always be produced in time.
    const double hostPerBeads = fs / static_cast<double> (beadsRate);
    const int prime = static_cast<int> (std::ceil (beadsBlock * hostPerBeads)) + 4;
    fifoRead = fifoWrite = 0;
    for (int i = 0; i < prime; ++i)
        pushOutput (beads::FloatFrame { 0.0f, 0.0f });

    latency = prime + static_cast<int> (std::lround (toBeads->delay() + fromBeads->delay() * hostPerBeads)) + limiter.latency();
}

void RainEngine::setParams (const RainParams& p)
{
    params = p;
    outputGain.setTarget (std::pow (10.0f, p.outputGainDb / 20.0f));
    if (prepared && p.quality != configured)
        configure (p.quality);
}

void RainEngine::pushOutput (const beads::FloatFrame& f)
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
    if (p.sync && p.mode == Mode::grains)
    {
        const auto idx = static_cast<std::size_t> (std::clamp (p.syncDivision, 0, static_cast<int> (kSyncDivisionBeats.size()) - 1));
        const float stepSeconds = kSyncDivisionBeats[idx] * 60.0f / static_cast<float> (bpm);
        const float buffer = bufferSeconds (configured);
        const float steps = std::round (p.time * buffer / stepSeconds);
        p.time = std::clamp (steps * stepSeconds / buffer, 0.0f, 1.0f);
    }

    for (int t = 1; t < kNumModTargets; ++t)
    {
        const float v = t == static_cast<int> (ModTarget::pitch) ? (p.pitchSemis + 24.0f) / 48.0f : *targets[t];
        mon.effective[static_cast<std::size_t> (t)].store (v, std::memory_order_relaxed);
    }
}

void RainEngine::runBeadsBlock()
{
    const auto n = static_cast<std::size_t> (beadsBlock);

    inputGain.process (params.autoGain, params.inputGainDb, beadsIn.data(), n);

    // Level meter for the panel's buffer display; stands still while frozen.
    const int levelPeriod = static_cast<int> (beadsRate) / EngineMonitor::kLevelRate;
    for (std::size_t i = 0; i < n; ++i)
    {
        levelPeak = std::max ({ levelPeak, std::fabs (beadsIn[i].l), std::fabs (beadsIn[i].r) });
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
                                  { &q.feedback, &smoothed.feedback }, { &q.dryWet, &smoothed.dryWet }, { &q.reverb, &smoothed.reverb } })
    {
        *state += kKnobSmoothing * (*target - *state);
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
    // Grains mode stays below the point where Beads flips into delay mode.
    const float size = q.mode == Mode::delay ? 1.0f : std::min (q.size, 0.975f);
    p.time = knob (q.time, q.timeRandom);
    p.size = knob (size, q.sizeRandom);
    p.shape = knob (q.shape, q.shapeRandom);
    p.pitch = knob (q.pitchSemis, q.pitchRandom);
    p.monophonic_mode = false;

    if (q.scale != Scale::off)
    {
        // To keep every grain in the scale, Rain draws the random pitch itself, the way
        // Beads would, and hands Beads the snapped result as a fixed value (a "CV" of 0).
        const float a = p.pitch.attenuverter;
        const float blend = a > 0.0f ? a : std::min (1.0f, a * pitchRng.uniform() * -3.0f);
        const float semis = q.pitchSemis + pitchRng.centred() * 24.0f * blend;
        p.pitch.base = quantisePitch (semis, q.scale);
        p.pitch.has_cv = true;
    }

    beads::ModulatedParameter density;
    density.base = q.density;
    density.cv = 0.0f;

    beads::Processor::MixParameters m;
    m.feedback = mixPot (q.feedback);
    m.dry_wet = mixPot (q.dryWet);
    m.reverb_amount = mixPot (q.reverb);
    m.output_gate = false;

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
        f.l = std::clamp (f.l, -1.0f, 1.0f) / kHeadroom;
        f.r = std::clamp (f.r, -1.0f, 1.0f) / kHeadroom;
        fromBeads->push (f, [this] (const beads::FloatFrame& g) { pushOutput (g); });
    }
}

void RainEngine::process (float* left, float* right, int numSamples)
{
    if (! prepared)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const beads::FloatFrame x { left[i] * kHeadroom, right[i] * kHeadroom };
        beatPos += bpm / (60.0 * fs);
        toBeads->push (x, [this] (const beads::FloatFrame& f) {
            beadsBeat[static_cast<std::size_t> (beadsFill)] = beatPos;
            beadsIn[static_cast<std::size_t> (beadsFill++)] = f;
            if (beadsFill == beadsBlock)
            {
                runBeadsBlock();
                beadsFill = 0;
            }
        });

        beads::FloatFrame y { 0.0f, 0.0f };
        if (fifoRead != fifoWrite)
        {
            y = fifo[fifoRead];
            fifoRead = (fifoRead + 1) & (kFifoSize - 1);
        }
        const float g = outputGain.next();
        float l = subsonic.process (y.l, 0) * g;
        float r = subsonic.process (y.r, 1) * g;
        limiter.process (l, r);
        left[i] = l;
        right[i] = r;
    }

    mon.bpm.store (static_cast<float> (bpm), std::memory_order_relaxed);
    mon.beat.store (beatPos, std::memory_order_relaxed);
}

} // namespace rain::dsp
