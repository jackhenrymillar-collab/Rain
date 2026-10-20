#pragma once

#include "Blocks.h"
#include "InputGain.h"
#include "Motion.h"
#include "OutputStage.h"
#include "Resampler.h"

#include <array>
#include <atomic>
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
    dirtyTape, // 24 kHz, 8-bit mu-law, ~16 s, wow and flutter
    studio     // Rain: 48 kHz, 32-bit float buffer and reverb, 8 s, cleanest grains
};

struct ModSettings
{
    float rateHz = 0.25f;
    float depth = 0.0f; // -1 to +1
    ModTarget target = ModTarget::off;
};

struct RainParams
{
    // Knob positions, 0 to 1, exactly as Beads reads its pots.
    float time = 0.5f;
    float size = 0.5f;
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

    // Tempo sync: a clock from the host's tempo is patched into Beads' SEED input,
    // so DENSITY divides it (left of noon) or picks pulses at random (right of noon),
    // and TIME snaps to whole clock steps. In delay mode the clock sets the delay.
    bool sync = false;
    int syncDivision = 2; // index into kSyncDivisionBeats (1/16)

    // Snaps grain pitch, including PITCH RANDOM and modulation, to a scale. Random
    // pitches favour the root, fifth and third, so clouds lean consonant.
    Scale scale = Scale::off;

    // Overlap: grains keep their SIZE, and sparse grains come more often so they
    // always overlap and the cloud has no gaps (free-running only).
    bool overlap = false;
    float reverse = 0.0f; // chance each grain plays backwards, 0 to 1
    float soften = 0.0f;  // rounds off the grains' highs and lows, 0 to 1
    float smudge = 0.0f;  // 0 to 1: softens each grain's transients and edges
    float randomPan = 0.0f; // 0 to 1: each grain lands at a random place in the stereo field
    // Formant: pitched grains keep their tone colour (voices don't go chipmunk or
    // monster), by repeating pitch-period slices instead of resampling.
    bool formant = false;
    // Smooth random: the RANDOM knobs follow a slowly wandering random value with a
    // little per-grain variation, instead of a fresh random value every grain (the
    // module's way, and the default).
    bool smoothRandom = false;

    // Bypass: the output crossfades (20 ms) to the dry input, delayed by the reported
    // latency so it lines up. Rain keeps recording underneath, so it comes back warm.
    bool bypass = false;

    // Modulator 1 is a sine wave, modulator 2 is smooth random drift.
    std::array<ModSettings, 2> mods {};
};

// Live values for the panel, written by the audio thread and read by the editor.
struct EngineMonitor
{
    static constexpr int kLevelRate = 128;    // level readings per second
    static constexpr int kNumLevels = 2048;   // 16 s, the longest buffer

    // Effective knob positions after modulation and snapping, 0 to 1, indexed by
    // ModTarget (pitch as a PITCH knob position through Beads' knob law).
    std::array<std::atomic<float>, kNumModTargets> effective {};
    std::array<std::atomic<float>, 2> modValue {};

    // Peak input level written into the buffer, one reading per 1/kLevelRate s.
    // Frozen while FREEZE is on, like the buffer itself.
    std::array<std::atomic<float>, kNumLevels> levels {};
    std::atomic<int> levelWrite { 0 };

    // Peak levels for the panel's meters since the panel last read them (it resets
    // them to 0): the input as it reaches the buffer, after INPUT gain, and the
    // plugin's output.
    mutable std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };

    // Grain length Beads is asked for (before SIZE RANDOM), in seconds.
    std::atomic<float> grainSeconds { 0.15f };
    std::atomic<float> spawnDensity { 0.5f }; // DENSITY as Beads gets it (Overlap may raise the rate)
    // The slow random drifts behind TIME, SIZE, SHAPE and PITCH RANDOM (-1 to +1).
    std::array<std::atomic<float>, 4> randomDrift {};
    std::atomic<float> randomJitter { 1.0f }; // per-grain share of the randomness (1 = module)

    std::atomic<float> bufferSeconds { 4.0f };
    std::atomic<float> bpm { 120.0f };
    std::atomic<double> beat { 0.0 }; // clock position in quarter notes
};

// Runs the Mutable Instruments Beads signal path (third_party/mutable) inside a plugin.
// The dry signal is mixed here at the host rate, untouched (Beads still feeds it to
// its reverb), so only the grains go through Beads' rate conversion and storage.
// Beads runs at fixed rates (24, 32 or 48 kHz depending on quality), so audio is
// converted from the host rate into Beads' rate and back again, in Beads' own small
// blocks (sample rate / 4000 frames, as on the module). This adds a few samples of
// latency, reported by latencySamples(). Nothing allocates after prepare().
//
// Levels: audio enters Beads 6 dB down, like a typical Eurorack level against the
// module's +/-10 V input range, so full-scale host audio never clips inside it; the
// output is brought back up by the same amount. A sub-sonic filter and a look-ahead
// limiter (ceiling -0.2 dBFS) finish the output.
class RainEngine
{
public:
    RainEngine();
    ~RainEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const RainParams& params);

    // Host tempo and position, once per block before process(). When the host is not
    // playing the clock free-runs at the given tempo.
    void setTransport (double bpm, double ppqPosition, bool playing) noexcept;

    // In-place stereo processing; left and right must be distinct buffers.
    void process (float* left, float* right, int numSamples);

    int latencySamples() const noexcept { return latency; }
    float internalRate() const noexcept { return beadsRate; }

    // Knob law of Beads' attenuverters (-1 to +1 in, -1 to +1 out, small dead zone).
    static float attenuverterLaw (float knob) noexcept;

    // Length of Beads' recording buffer for each quality, in seconds.
    static float bufferSeconds (Quality q) noexcept;

    // Rain's SIZE runs through Beads' forward grain lengths only (reversing is its own
    // control): knob 0..1 maps onto Beads' SIZE from 9 o'clock to just short of the
    // delay-mode switch point.
    static float beadsSize (float size) noexcept;
    // Beads' grain length law for a forward SIZE value, and its inverse.
    static float grainSeconds (float beadsSize) noexcept;
    static float beadsSizeForSeconds (float seconds) noexcept;
    // Overlap: the DENSITY that keeps grains of grainLength seconds overlapping
    // (unchanged when they already do, or at noon).
    static float overlapDensity (float density, float grainLength) noexcept;
    // Average grain rate for DENSITY, free-running or on a clock of clockHz (0 = free).
    static float grainRate (float density, float clockHz) noexcept;

    const EngineMonitor& monitor() const noexcept { return mon; }

private:
    // Quality changes (see RainEngine.cpp): the host side, and the Beads side, which
    // carries the recording over in slices of work.
    void configureHost();
    void startSwitch (Quality target, bool keepRecording);
    bool advanceSwitch (long long budget); // true when finished

    enum class SwitchStage { idle, exporting, filtering, resampling, configuring, importing };
    struct Switch
    {
        SwitchStage stage = SwitchStage::idle;
        Quality target = Quality::digital;
        int frames = 0, out = 0, pos = 0, pass = 0;
        float fromRate = 48000.0f, toRate = 48000.0f;
        double step = 1.0;
        beads::FloatFrame x1 {}, x2 {}, y1 {}, y2 {};
    } switching;
    void runBeadsBlock();
    struct OutFrame
    {
        beads::FloatFrame wet; // Beads' output without the dry signal
        float dryGain;         // gain Beads gives the dry signal at this moment
    };
    void pushOutput (const OutFrame& f);

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
    std::vector<std::uint8_t> studioBuffer; // Studio quality: three 1 MB banks
    std::vector<float> studioReverb;
    // The recording, while quality changes: as it was, and resampled.
    static constexpr std::size_t kCarryFrames = 400000; // the longest buffer is 384000 frames
    std::vector<beads::FloatFrame> carry, carryOut;

    InputGain inputGain;

    // One converter pair per Beads rate (24, 32 and 48 kHz), built in prepare() so
    // switching quality on the audio thread never allocates.
    static constexpr int kNumRates = 3;
    // gainFromBeadsAt carries the dry signal's gain alongside the wet signal, so the
    // two stay aligned sample for sample.
    std::array<Resampler, kNumRates> toBeadsAt, fromBeadsAt, gainFromBeadsAt;
    Resampler* toBeads = nullptr;
    Resampler* fromBeads = nullptr;
    Resampler* gainFromBeads = nullptr;

    SubsonicFilter subsonic;
    Limiter limiter;

    // Knob smoothing, as the module's pot readers do (one-pole at the block rate).
    RainParams smoothed;
    bool smoothedValid = false;

    std::array<beads::FloatFrame, kMaxBeadsBlock> beadsIn {}, beadsOut {};
    std::array<float, kMaxBeadsBlock> beadsDryGain {};
    int beadsFill = 0;

    std::vector<OutFrame> fifo;
    float lastDryGain = 0.0f;
    bool qualityPending = false;          // a new quality is waiting for the fade-out
    float qualityFade = 1.0f, qualityFadeStep = 0.0f;
    std::size_t fifoRead = 0, fifoWrite = 0;

    // Rain's additions.
    void applyMotion (RainParams& p, float dt);
    Modulator wave { Modulator::Kind::wave, 0x1234567u }, drift { Modulator::Kind::drift, 0x7654321u };
    // Correlated randomness: a slow random drift each for TIME, SIZE, SHAPE and PITCH.
    std::array<Modulator, 4> randomDrift { Modulator { Modulator::Kind::drift, 0x13579bdu }, Modulator { Modulator::Kind::drift, 0x2468ace1u },
                                           Modulator { Modulator::Kind::drift, 0x0badf00du }, Modulator { Modulator::Kind::drift, 0x5eed1234u } };
    Rng pitchRng { 0x2468aceu };
    double bpm = 120.0, beatPos = 0.0;
    std::array<double, kMaxBeadsBlock> beadsBeat {};
    unsigned char clockFlags = 0; // previous stmlib::GateFlags
    float levelPeak = 0.0f;
    int levelCount = 0;
    EngineMonitor mon;

    Smoother outputGain;

    // Bypass: a delay line for the dry signal and the crossfade position (1 = dry).
    static constexpr std::size_t kDryDelaySize = 16384; // power of two
    std::vector<beads::FloatFrame> dryDelay;
    std::size_t dryWrite = 0;
    float bypassMix = 0.0f, bypassStep = 0.001f;
    Quality configured = Quality::digital;
    RainParams params;
};

} // namespace rain::dsp
