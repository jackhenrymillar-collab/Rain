#pragma once

#include "beads/dsp/frame.h"

namespace rain::dsp
{

// Streaming stereo sample-rate converter using 4-point cubic Hermite interpolation.
// Push input frames one at a time; each push emits zero or more output frames.
// At a 1:1 ratio it passes samples through exactly, two samples late.
// Band-limiting is done by the caller (see RainEngine), so this stays tiny.
class Resampler
{
public:
    void setRates (double inputRate, double outputRate) noexcept
    {
        step = inputRate / outputRate;
        reset();
    }

    void reset() noexcept
    {
        pos = 0.0;
        for (auto& f : h)
            f.l = f.r = 0.0f;
    }

    template <typename Emit>
    void push (const beads::FloatFrame& x, Emit&& emit)
    {
        h[0] = h[1];
        h[1] = h[2];
        h[2] = h[3];
        h[3] = x;
        while (pos < 1.0)
        {
            const float t = static_cast<float> (pos);
            beads::FloatFrame y;
            y.l = hermite (h[0].l, h[1].l, h[2].l, h[3].l, t);
            y.r = hermite (h[0].r, h[1].r, h[2].r, h[3].r, t);
            emit (y);
            pos += step;
        }
        pos -= 1.0;
    }

    // Delay in input samples.
    static constexpr int kDelay = 2;

private:
    static float hermite (float x0, float x1, float x2, float x3, float t) noexcept
    {
        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * t + c2) * t + c1) * t + x1;
    }

    double step = 1.0;
    double pos = 0.0;
    beads::FloatFrame h[4] {};
};

} // namespace rain::dsp
