#pragma once

// Rain's output stage: a DC/sub-sonic blocker and a look-ahead peak limiter.
// Plain C++; prepare() allocates, process() does not.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace rain::dsp
{

// Second-order Butterworth high-pass, in double precision so it stays accurate at a
// very low corner. Removes the DC and sub-sonic thumps that windowing low notes
// into short grains produces.
class SubsonicFilter
{
public:
    void prepare (double sampleRate, double cornerHz = 12.0)
    {
        const double w = std::tan (3.14159265358979323846 * cornerHz / sampleRate);
        const double k = 1.0 / (1.0 + std::sqrt (2.0) * w + w * w);
        b0 = k;
        b1 = -2.0 * k;
        b2 = k;
        a1 = 2.0 * (w * w - 1.0) * k;
        a2 = (1.0 - std::sqrt (2.0) * w + w * w) * k;
        reset();
    }

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }

    float process (float x, int ch) noexcept
    {
        // Transposed direct form II.
        const double in = x;
        const double y = b0 * in + z1[ch];
        z1[ch] = b1 * in - a1 * y + z2[ch];
        z2[ch] = b2 * in - a2 * y;
        return static_cast<float> (y);
    }

private:
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1[2] {}, z2[2] {};
};

// Stereo-linked look-ahead peak limiter. The signal is delayed by the look-ahead
// time while the gain ramps down ahead of each peak, so peaks are caught without
// the clicks and low-end distortion of a clipper. Transparent below the ceiling.
class Limiter
{
public:
    void prepare (double sampleRate, float ceilingGain = 0.98f, float lookaheadMs = 1.5f, float releaseMs = 80.0f)
    {
        ceiling = ceilingGain;
        lookahead = std::max (1, static_cast<int> (std::lround (sampleRate * lookaheadMs * 0.001)));
        delay.assign (static_cast<std::size_t> (lookahead), 0.0f);
        delayR.assign (static_cast<std::size_t> (lookahead), 0.0f);
        // Required gains over the look-ahead window, for a running minimum.
        window.assign (static_cast<std::size_t> (lookahead), 1.0f);
        // Attack: reach the target within the look-ahead time.
        attack = 1.0f - std::exp (-4.0f / static_cast<float> (lookahead));
        release = 1.0f - std::exp (-1.0f / static_cast<float> (sampleRate * releaseMs * 0.001));
        reset();
    }

    void reset() noexcept
    {
        std::fill (delay.begin(), delay.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        std::fill (window.begin(), window.end(), 1.0f);
        index = 0;
        gain = 1.0f;
    }

    int latency() const noexcept { return lookahead; }

    void process (float& l, float& r) noexcept
    {
        const float peak = std::max (std::fabs (l), std::fabs (r));
        const float required = peak > ceiling ? ceiling / peak : 1.0f;

        const auto i = static_cast<std::size_t> (index);
        window[i] = required;
        const float dl = delay[i], dr = delayR[i];
        delay[i] = l;
        delayR[i] = r;
        index = (index + 1) % lookahead;

        // Lowest gain needed by anything in the look-ahead window.
        float target = 1.0f;
        for (const float w : window)
            target = std::min (target, w);

        gain += (target < gain ? attack : release) * (target - gain);

        // The ramp gets within a hair of the target; a final safety clamp covers it.
        l = std::clamp (dl * gain, -ceiling, ceiling);
        r = std::clamp (dr * gain, -ceiling, ceiling);
    }

private:
    float ceiling = 0.98f, attack = 0.5f, release = 0.001f, gain = 1.0f;
    int lookahead = 1, index = 0;
    std::vector<float> delay, delayR, window;
};

} // namespace rain::dsp
