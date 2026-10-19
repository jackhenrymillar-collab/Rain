#pragma once

#include "beads/dsp/frame.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace rain::dsp
{

// Streaming stereo sample-rate converter: band-limited (windowed-sinc, Kaiser)
// interpolation from a polyphase table, the standard high-quality technique.
// Stopband rejection is about 100 dB, the passband is flat to 0.43 of the lower of
// the two rates (19 kHz at 44.1 kHz) and the kernel does its own anti-aliasing, so
// no extra filters are needed around it.
//
// Push input frames one at a time; each push emits zero or more output frames.
// At a 1:1 ratio it passes samples straight through with no delay.
// prepare() allocates; push() does not.
class Resampler
{
public:
    void prepare (double inputRate, double outputRate)
    {
        step = inputRate / outputRate;
        bypass = std::abs (step - 1.0) < 1.0e-9;
        if (bypass)
        {
            taps = 0;
            reset();
            return;
        }

        // Cut off below the lower Nyquist; scale the kernel length with the ratio so
        // the transition band (in Hz) stays the same when converting down.
        const double r = std::min (1.0, outputRate / inputRate);
        const double cutoff = 0.43 * r; // cycles per input sample
        taps = static_cast<int> (std::ceil (96.0 / r / 2.0)) * 2;
        phases = r < 0.75 ? 256 : 512;

        constexpr double beta = 10.0; // Kaiser window, about 100 dB stopband
        const double half = taps / 2;
        table.assign (static_cast<std::size_t> ((phases + 1) * taps), 0.0f);
        for (int p = 0; p <= phases; ++p)
        {
            const double frac = static_cast<double> (p) / phases;
            for (int m = 0; m < taps; ++m)
            {
                const double t = (half - 1.0 + frac) - m; // distance from tap m
                const double x = t / half;
                const double window = std::abs (x) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - x * x)) / besselI0 (beta);
                const double arg = 2.0 * cutoff * t;
                const double sinc = std::abs (arg) < 1.0e-12 ? 1.0 : std::sin (kPi * arg) / (kPi * arg);
                table[static_cast<std::size_t> (p * taps + m)] = static_cast<float> (2.0 * cutoff * sinc * window);
            }
        }
        history.assign (static_cast<std::size_t> (2 * taps), beads::FloatFrame { 0.0f, 0.0f });
        reset();
    }

    void reset() noexcept
    {
        pos = 0.0;
        write = 0;
        std::fill (history.begin(), history.end(), beads::FloatFrame { 0.0f, 0.0f });
    }

    // Delay in input samples.
    double delay() const noexcept { return bypass ? 0.0 : taps / 2; }

    template <typename Emit>
    void push (const beads::FloatFrame& x, Emit&& emit)
    {
        if (bypass)
        {
            emit (x);
            return;
        }

        // The window history[write .. write + taps - 1] runs oldest to newest.
        history[static_cast<std::size_t> (write)] = x;
        history[static_cast<std::size_t> (write + taps)] = x;
        write = (write + 1) % taps;
        const beads::FloatFrame* window = history.data() + write;

        while (pos < 1.0)
        {
            const double scaled = pos * phases;
            const int p = std::min (static_cast<int> (scaled), phases - 1);
            const float a = static_cast<float> (scaled - p);
            const float* c0 = table.data() + p * taps;
            const float* c1 = c0 + taps;
            float l = 0.0f, r = 0.0f;
            for (int m = 0; m < taps; ++m)
            {
                const float c = c0[m] + a * (c1[m] - c0[m]);
                l += c * window[m].l;
                r += c * window[m].r;
            }
            emit (beads::FloatFrame { l, r });
            pos += step;
        }
        pos -= 1.0;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1.0e-12 * sum)
                break;
        }
        return sum;
    }

    double step = 1.0;
    double pos = 0.0;
    bool bypass = true;
    int taps = 0, phases = 0, write = 0;
    std::vector<float> table;
    std::vector<beads::FloatFrame> history;
};

} // namespace rain::dsp
