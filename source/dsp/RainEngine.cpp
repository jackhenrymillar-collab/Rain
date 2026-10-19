#include "RainEngine.h"

#include "beads/dsp/processor.h"

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

    float lowpass (Biquad (&f)[2], float x) { return f[1].process (f[0].process (x)); }
}

RainEngine::RainEngine() = default;
RainEngine::~RainEngine() = default;

float RainEngine::attenuverterLaw (float knob) noexcept
{
    const float v = 0.5f * std::clamp (knob, -1.0f, 1.0f);
    return (2.0f / 0.47f) * v * std::max (std::fabs (v) - 0.03f, 0.0f);
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

    fifo.assign (kFifoSize, beads::FloatFrame { 0.0f, 0.0f });
    outputGain.prepare (fs, 20.0f);
    outputGain.snapTo (std::pow (10.0f, params.outputGainDb / 20.0f));
    prepared = true;
    configure (params.quality);
}

void RainEngine::reset()
{
    if (! prepared)
        return;
    std::fill (block1.begin(), block1.end(), std::uint8_t { 0 });
    std::fill (block2.begin(), block2.end(), std::uint8_t { 0 });
    std::fill (reverbBuffer.begin(), reverbBuffer.end(), std::uint16_t { 0 });
    outputGain.snapTo (outputGain.getTarget());
    configure (configured);
}

void RainEngine::configure (Quality q)
{
    configured = q;
    processor->Configure (mediaFor (q), false);

    beadsRate = processor->sample_rate();
    beadsBlock = std::clamp (static_cast<int> (beadsRate / 4000.0f), 1, kMaxBeadsBlock);

    toBeads.setRates (fs, beadsRate);
    fromBeads.setRates (beadsRate, fs);

    // Band-limit around the rate conversion: before going down to Beads' rate, and
    // after coming back up from it.
    const auto br = static_cast<double> (beadsRate);
    lpIn = br < fs;
    lpOutPre = fs < br;
    lpOutPost = fs > br;
    for (int ch = 0; ch < 2; ++ch)
        for (int s = 0; s < 2; ++s)
        {
            inLp[ch][s].setLowpass (0.45f * beadsRate, fs);
            outPreLp[ch][s].setLowpass (static_cast<float> (0.45 * fs), br);
            outPostLp[ch][s].setLowpass (0.45f * beadsRate, fs);
            inLp[ch][s].reset();
            outPreLp[ch][s].reset();
            outPostLp[ch][s].reset();
        }

    const auto clipping = q == Quality::dirtyTape ? InputGain::SoftClipping::on
                        : q == Quality::cleanTape ? InputGain::SoftClipping::dynamic
                                                  : InputGain::SoftClipping::off;
    inputGain.init (beadsRate, clipping, true);

    beadsFill = 0;

    // Prime the output FIFO so a full Beads block can always be produced in time.
    const double hostPerBeads = fs / br;
    const int prime = static_cast<int> (std::ceil (beadsBlock * hostPerBeads)) + 4;
    fifoRead = fifoWrite = 0;
    for (int i = 0; i < prime; ++i)
        pushOutput (beads::FloatFrame { 0.0f, 0.0f });

    latency = prime + Resampler::kDelay + static_cast<int> (std::ceil (Resampler::kDelay * hostPerBeads));
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

void RainEngine::runBeadsBlock()
{
    const auto n = static_cast<std::size_t> (beadsBlock);

    inputGain.process (params.autoGain, params.inputGainDb, beadsIn.data(), n);

    beads::Parameters p;
    auto knob = [] (float base, float random) {
        beads::RandomizedParameter r;
        r.base = base;
        r.attenuverter = attenuverterLaw (random);
        r.cv = 0.0f;
        r.has_cv = false;
        return r;
    };
    p.time = knob (std::clamp (params.time, 0.0f, 1.0f), params.timeRandom);
    p.size = knob (std::clamp (params.size, 0.0f, 1.0f), params.sizeRandom);
    p.shape = knob (std::clamp (params.shape, 0.0f, 1.0f), params.shapeRandom);
    p.pitch = knob (std::clamp (params.pitchSemis, -24.0f, 24.0f), params.pitchRandom);
    p.monophonic_mode = false;

    beads::ModulatedParameter density;
    density.base = std::clamp (params.density, 0.0f, 1.0f);
    density.cv = 0.0f;

    beads::Processor::MixParameters m;
    m.feedback = mixPot (params.feedback);
    m.dry_wet = mixPot (params.dryWet);
    m.reverb_amount = mixPot (params.reverb);
    m.output_gate = false;

    bool freeze[kMaxBeadsBlock];
    std::fill (freeze, freeze + kMaxBeadsBlock, params.freeze);

    // On the module SEED is latched ("locked") at power-up, so grains run continuously.
    // Unlocked, grains only play while SEED is held (or its gate input is high).
    const auto seed = params.seed       ? beads::SEED_BUTTON_STATE_ON
                    : params.seedLock   ? beads::SEED_BUTTON_STATE_LOCKED
                                        : beads::SEED_BUTTON_STATE_OFF;
    processor->Process (seed, nullptr, freeze, density, p, m, beadsIn.data(), beadsOut.data(), n);

    for (std::size_t i = 0; i < n; ++i)
    {
        beads::FloatFrame f = beadsOut[i];
        f.l = std::clamp (f.l, -1.0f, 1.0f);
        f.r = std::clamp (f.r, -1.0f, 1.0f);
        if (lpOutPre)
        {
            f.l = lowpass (outPreLp[0], f.l);
            f.r = lowpass (outPreLp[1], f.r);
        }
        fromBeads.push (f, [this] (beads::FloatFrame g) {
            if (lpOutPost)
            {
                g.l = lowpass (outPostLp[0], g.l);
                g.r = lowpass (outPostLp[1], g.r);
            }
            pushOutput (g);
        });
    }
}

void RainEngine::process (float* left, float* right, int numSamples)
{
    if (! prepared)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        beads::FloatFrame x { left[i], right[i] };
        if (lpIn)
        {
            x.l = lowpass (inLp[0], x.l);
            x.r = lowpass (inLp[1], x.r);
        }
        toBeads.push (x, [this] (const beads::FloatFrame& f) {
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
        left[i] = softLimit (y.l * g, 0.95f);
        right[i] = softLimit (y.r * g, 0.95f);
    }
}

} // namespace rain::dsp
