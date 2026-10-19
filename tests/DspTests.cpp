#include "dsp/Blocks.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace rain::dsp;
using Catch::Approx;

TEST_CASE ("Delay line returns the sample pushed d samples ago")
{
    DelayLine d;
    d.prepare (16);
    for (int i = 1; i <= 10; ++i)
        d.push (static_cast<float> (i));
    CHECK (d.read (1) == 10.0f);
    CHECK (d.read (4) == 7.0f);
}

TEST_CASE ("Lagrange read is exact on integer delays and interpolates a ramp exactly")
{
    DelayLine d;
    d.prepare (32);
    for (int i = 0; i < 20; ++i)
        d.push (static_cast<float> (i));
    CHECK (d.readFractional (5.0f) == Approx (d.read (5)));
    // A linear ramp is reproduced exactly by cubic interpolation.
    CHECK (d.readFractional (5.25f) == Approx (19.0f - 4.25f).margin (1e-4));
}

TEST_CASE ("Allpass preserves energy")
{
    Allpass ap;
    ap.prepare (64);
    std::vector<float> out (20000);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = ap.process (i == 0 ? 1.0f : 0.0f, 37, 0.7f);
    double e = 0.0;
    for (float v : out)
        e += static_cast<double> (v) * v;
    CHECK (e == Approx (1.0).margin (1e-4));
}

TEST_CASE ("Soft limiter is transparent below threshold and bounded above")
{
    CHECK (softLimit (0.5f) == 0.5f);
    CHECK (softLimit (-0.89f) == -0.89f);
    CHECK (softLimit (10.0f) <= 1.0f);
    CHECK (softLimit (-10.0f) >= -1.0f);
    CHECK (softLimit (0.95f) < 0.95f);
}

TEST_CASE ("Smoother converges to its target")
{
    Smoother s;
    s.prepare (48000.0, 10.0f);
    s.snapTo (0.0f);
    s.setTarget (1.0f);
    for (int i = 0; i < 48000; ++i)
        s.next();
    CHECK (s.getCurrent() == 1.0f);
}

TEST_CASE ("Biquad low cut removes DC")
{
    Biquad hp;
    hp.setHighpass (100.0f, 48000.0);
    float y = 0.0f;
    for (int i = 0; i < 48000; ++i)
        y = hp.process (1.0f);
    CHECK (std::abs (y) < 1e-4f);
}

#include "dsp/Resampler.h"

#include <cmath>
#include <vector>

TEST_CASE ("Resampler passes samples through exactly at 1:1, two samples late")
{
    Resampler r;
    r.setRates (48000.0, 48000.0);
    std::vector<float> out;
    for (int i = 1; i <= 10; ++i)
        r.push (beads::FloatFrame { static_cast<float> (i), -static_cast<float> (i) },
                [&] (const beads::FloatFrame& f) { out.push_back (f.l); });
    REQUIRE (out.size() == 10);
    CHECK (out[2] == 1.0f);
    CHECK (out[9] == 8.0f);
}

TEST_CASE ("Resampler round trip keeps a 1 kHz tone's level")
{
    Resampler down, up;
    down.setRates (44100.0, 24000.0);
    up.setRates (24000.0, 44100.0);
    std::vector<float> out;
    const int n = 44100;
    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * std::sin (2.0f * 3.14159265f * 1000.0f * static_cast<float> (i) / 44100.0f);
        down.push (beads::FloatFrame { x, x }, [&] (const beads::FloatFrame& f) {
            up.push (f, [&] (const beads::FloatFrame& g) { out.push_back (g.l); });
        });
    }
    CHECK (std::abs (static_cast<int> (out.size()) - n) <= 2);
    double e = 0.0;
    for (std::size_t i = 4410; i < out.size() - 10; ++i)
        e += static_cast<double> (out[i]) * out[i];
    const double rms = std::sqrt (e / static_cast<double> (out.size() - 4420));
    CHECK (rms == Approx (0.5 / std::sqrt (2.0)).epsilon (0.02));
}
