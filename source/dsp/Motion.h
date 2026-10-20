#pragma once

// Rain's additions on top of Beads: the two modulators, scale quantising and
// tempo helpers. Plain C++, no allocation, unit tested in tests/MotionTests.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace rain::dsp
{

// Small, fast random numbers (xorshift32). Deterministic for a given seed.
class Rng
{
public:
    explicit Rng (std::uint32_t seed = 0x9e3779b9u) noexcept : state (seed == 0 ? 1u : seed) {}

    float uniform() noexcept // 0 to 1
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float> (state >> 8) * (1.0f / 16777216.0f);
    }

    float centred() noexcept { return 2.0f * uniform() - 1.0f; } // -1 to 1

private:
    std::uint32_t state;
};

// What a modulator moves. In front-panel order; saved in presets, so append only.
enum class ModTarget
{
    off, time, size, shape, pitch, density, feedback, dryWet, reverb
};

inline constexpr int kNumModTargets = 9;

// One of the two "weather" modulators.
//   wave:  a sine LFO.
//   drift: smooth random movement, a new random level every cycle, eased between.
// Output is -1 to +1.
class Modulator
{
public:
    enum class Kind { wave, drift };

    explicit Modulator (Kind k, std::uint32_t seed = 1) noexcept : kind (k), rng (seed)
    {
        from = 0.0f;
        to = rng.centred();
    }

    void reset() noexcept
    {
        phase = 0.0f;
        from = 0.0f;
        to = rng.centred();
        value = 0.0f;
    }

    // Advances by dt seconds at rate Hz and returns the new value.
    float advance (float rateHz, float dt) noexcept
    {
        phase += std::clamp (rateHz, 0.0f, 50.0f) * dt;
        if (kind == Kind::wave)
        {
            phase -= std::floor (phase);
            value = std::sin (2.0f * std::numbers::pi_v<float> * phase);
        }
        else
        {
            while (phase >= 1.0f)
            {
                phase -= 1.0f;
                from = to;
                to = rng.centred();
            }
            // Cosine ease between successive random levels: smooth, no corners.
            const float t = 0.5f - 0.5f * std::cos (std::numbers::pi_v<float> * phase);
            value = from + (to - from) * t;
        }
        return value;
    }

    float current() const noexcept { return value; }

private:
    Kind kind;
    Rng rng;
    float phase = 0.0f, from = 0.0f, to = 0.0f, value = 0.0f;
};

// Snaps a pitch in semitones to the nearest note of a scale. The scale's root is
// the unshifted input (0 semitones), since Rain transposes rather than plays notes.
// In front-panel order; saved in presets, so append only.
enum class Scale
{
    off, chromatic, major, minor, pentatonic, minorPentatonic, fifths, octaves
};

inline constexpr int kNumScales = 8;

inline float quantisePitch (float semis, Scale scale) noexcept
{
    if (scale == Scale::off)
        return semis;
    if (scale == Scale::chromatic)
        return std::round (semis);

    // Bit n set = n semitones above the root is in the scale.
    std::uint16_t mask = 0;
    switch (scale)
    {
        case Scale::major:           mask = 0b101010110101; break; // 0 2 4 5 7 9 11
        case Scale::minor:           mask = 0b010110101101; break; // 0 2 3 5 7 8 10
        case Scale::pentatonic:      mask = 0b001010010101; break; // 0 2 4 7 9
        case Scale::minorPentatonic: mask = 0b010010101001; break; // 0 3 5 7 10
        case Scale::fifths:          mask = 0b000010000001; break; // 0 7
        case Scale::octaves:         mask = 0b000000000001; break; // 0
        case Scale::off:
        case Scale::chromatic:       break;
    }

    // Search outwards from the nearest semitone for the closest scale note.
    const float nearest = std::round (semis);
    float best = nearest;
    float bestDistance = 1.0e9f;
    for (int offset = -12; offset <= 12; ++offset)
    {
        const float candidate = nearest + static_cast<float> (offset);
        const int pc = ((static_cast<int> (candidate) % 12) + 12) % 12;
        if ((mask >> pc) & 1)
        {
            const float d = std::fabs (candidate - semis);
            if (d < bestDistance)
            {
                bestDistance = d;
                best = candidate;
            }
        }
    }
    return best;
}

// Tempo sync clock lengths, in quarter-note beats. In front-panel order; saved in
// presets, so append only.
inline constexpr std::array<float, 6> kSyncDivisionBeats { 1.0f, 0.5f, 0.25f, 0.125f, 1.0f / 3.0f, 1.0f / 6.0f };
inline constexpr std::array<const char*, 6> kSyncDivisionNames { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };

// When synced, Beads divides the clock by DENSITY (left of noon), exactly as the
// module does with a clock patched into SEED. This mirrors its lookup so the panel
// can name the resulting note length. Returns 0 when grains follow every pulse with
// a probability instead (right of noon) and -1 at noon's dead zone edge cases.
// How consonant a note is against the unshifted pitch, 0 to 1: root and octaves,
// then the fifth, the thirds, the fourth and sixth, and the rest. Random grain
// pitches are drawn in proportion to this, so clouds lean towards open intervals.
inline float consonance (float semis) noexcept
{
    const int pc = ((static_cast<int> (std::lround (semis)) % 12) + 12) % 12;
    switch (pc)
    {
        case 0:  return 1.0f;
        case 7:  return 0.8f;
        case 3:
        case 4:  return 0.6f;
        case 5:
        case 9:  return 0.5f;
        default: return 0.35f;
    }
}

// Beads' PITCH knob (beads/cv_reader.h, LAW_DETUNE). The knob runs from -1 to +1
// and steps through 0, 1, 3, 4, 7, 12 and 24 semitones each way: each sixth of the
// travel holds on one interval for its first third, then glides smoothly to the next.
inline float detuneSemitones (float position) noexcept
{
    constexpr float intervals[8] = { 0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 12.0f, 24.0f, 24.0f };
    const float sign = position < 0.0f ? -1.0f : 1.0f;
    const float detune = std::min (std::fabs (position), 1.0f) * 6.0f;
    const int integral = std::min (static_cast<int> (detune), 6);
    float fractional = std::max ((detune - static_cast<float> (integral) - 0.33333333f) * 1.5f, 0.0f);
    fractional = fractional * fractional * (3.0f - 2.0f * fractional); // stmlib::SmoothStep
    const float a = intervals[integral], b = intervals[integral + 1];
    return (a + (b - a) * fractional) * sign;
}

// Knob position for a pitch in semitones: the middle of an interval's hold zone, or
// the point on the glide between two intervals.
inline float detunePosition (float semis) noexcept
{
    constexpr float intervals[7] = { 0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 12.0f, 24.0f };
    const float sign = semis < 0.0f ? -1.0f : 1.0f;
    const float s = std::min (std::fabs (semis), 24.0f);
    for (int i = 0; i < 7; ++i)
        if (std::fabs (s - intervals[i]) < 1.0e-3f)
            return sign * (i == 0 ? 0.0f : std::min (1.0f, (static_cast<float> (i) + 1.0f / 6.0f) / 6.0f));
    float lo = 0.0f, hi = 1.0f; // detuneSemitones rises monotonically
    for (int k = 0; k < 40; ++k)
    {
        const float mid = 0.5f * (lo + hi);
        (detuneSemitones (mid) < s ? lo : hi) = mid;
    }
    return sign * 0.5f * (lo + hi);
}

inline int syncDivider (float density) noexcept
{
    constexpr int ratios[] = { 1, 2, 3, 4, 6, 8, 12, 16 };
    if (density >= 0.48f)
        return density > 0.52f ? 0 : 1;
    const int index = std::clamp (static_cast<int> (density * 2.15f * 8.0f), 0, 7);
    return ratios[index];
}

} // namespace rain::dsp
