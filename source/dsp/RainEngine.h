#pragma once

#include "Blocks.h"
#include "InputGain.h"
#include "Resampler.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace beads { class Processor; }

namespace rain::dsp
{

// Rain's quality modes, in front-panel order. Each is one of Beads' four "media".
enum class Quality
{
    digital,   // 48 kHz, 16-bit, 4 s
    vintage,   // 32 kHz, 12-bit, 8 s
    cleanTape, // 24 kHz internal, 12-bit, ~10 s, soft saturation
    dirtyTape  // 24 kHz, 8-bit mu-law, ~16 s, wow and flutter
};

struct RainParams
{
    // Knob positions, 0 to 1, exactly as Beads reads its pots.
    float time = 0.5f;
    float size = 0.5f;     // above 0.99 Beads switches to its delay mode
    float shape = 0.5f;
    float density = 0.5f;  // noon = no grains, left = regular, right = random
    float pitchSemis = 0.0f; // -24 to +24

    // The four small knobs under TIME, SIZE, SHAPE and PITCH, -1 to +1.
    // On Beads these are attenuverters; with nothing patched they set randomness:
    // clockwise = new random value per grain, counter-clockwise = occasional jumps.
    float timeRandom = 0.0f, sizeRandom = 0.0f, shapeRandom = 0.0f, pitchRandom = 0.0f;

    float feedback = 0.0f; // 0 to 1
    float dryWet = 0.5f;   // 0 to 1
    float reverb = 0.0f;   // 0 to 1

    bool freeze = false;
    bool seed = false;     // momentary: held = gate high, like pressing SEED
    bool seedLock = true;  // grains run on their own, like a long press of SEED (the module's default)

    Quality quality = Quality::digital;
    bool autoGain = false;     // Beads' automatic input level
    float inputGainDb = 0.0f;  // 0 to 32 dB when autoGain is off
    float outputGainDb = 2.5f; // makes up Beads' 0.75 dry gain, so 0% wet is unity
};

// Runs the Mutable Instruments Beads signal path (third_party/mutable) inside a plugin.
// Beads runs at fixed rates (24, 32 or 48 kHz depending on quality), so audio is
// converted from the host rate into Beads' rate and back again, in Beads' own small
// blocks (sample rate / 4000 frames, as on the module). This adds a few samples of
// latency, reported by latencySamples(). Nothing allocates after prepare().
class RainEngine
{
public:
    RainEngine();
    ~RainEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const RainParams& params);

    // In-place stereo processing; left and right must be distinct buffers.
    void process (float* left, float* right, int numSamples);

    int latencySamples() const noexcept { return latency; }
    float internalRate() const noexcept { return beadsRate; }

    // Knob law of Beads' attenuverters (-1 to +1 in, -1 to +1 out, small dead zone).
    static float attenuverterLaw (float knob) noexcept;

private:
    void configure (Quality q);
    void runBeadsBlock();
    void pushOutput (const beads::FloatFrame& f);

    static constexpr int kMaxBeadsBlock = 16;
    static constexpr std::size_t kFifoSize = 8192; // power of two

    double fs = 48000.0;
    float beadsRate = 48000.0f;
    int beadsBlock = 12;
    int latency = 0;
    bool prepared = false;

    std::unique_ptr<beads::Processor> processor;
    std::vector<std::uint8_t> block1, block2;
    std::vector<std::uint16_t> reverbBuffer;

    InputGain inputGain;
    Resampler toBeads, fromBeads;
    Biquad inLp[2][2], outPreLp[2][2], outPostLp[2][2];
    bool lpIn = false, lpOutPre = false, lpOutPost = false;

    std::array<beads::FloatFrame, kMaxBeadsBlock> beadsIn {}, beadsOut {};
    int beadsFill = 0;

    std::vector<beads::FloatFrame> fifo;
    std::size_t fifoRead = 0, fifoWrite = 0;

    Smoother outputGain;
    Quality configured = Quality::digital;
    RainParams params;
};

} // namespace rain::dsp
