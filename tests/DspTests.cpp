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

namespace
{
    // Fits a*sin + b*cos at f and returns fitted power over residual power, in dB.
    double thdPlusNoiseDb (const std::vector<float>& y, double fs, double f, std::size_t from, std::size_t to)
    {
        double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
        for (std::size_t i = from; i < to; ++i)
        {
            const double s = std::sin (2.0 * 3.141592653589793 * f * static_cast<double> (i) / fs);
            const double c = std::cos (2.0 * 3.141592653589793 * f * static_cast<double> (i) / fs);
            ss += s * s; sc += s * c; cc += c * c; ys += y[i] * s; yc += y[i] * c;
        }
        const double det = ss * cc - sc * sc;
        const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
        double sig = 0, res = 0;
        for (std::size_t i = from; i < to; ++i)
        {
            const double fit = a * std::sin (2.0 * 3.141592653589793 * f * static_cast<double> (i) / fs)
                             + b * std::cos (2.0 * 3.141592653589793 * f * static_cast<double> (i) / fs);
            sig += fit * fit;
            res += (y[i] - fit) * (y[i] - fit);
        }
        return 10.0 * std::log10 (sig / std::max (res, 1e-30));
    }

    std::vector<float> convert (double from, double to, const std::vector<float>& x)
    {
        Resampler r;
        r.prepare (from, to);
        std::vector<float> out;
        for (float v : x)
            r.push (beads::FloatFrame { v, v }, [&] (const beads::FloatFrame& f) { out.push_back (f.l); });
        return out;
    }

    std::vector<float> tone (double fs, double f, std::size_t n, float amp = 0.5f)
    {
        std::vector<float> x (n);
        for (std::size_t i = 0; i < n; ++i)
            x[i] = amp * static_cast<float> (std::sin (2.0 * 3.141592653589793 * f * static_cast<double> (i) / fs));
        return x;
    }
}

TEST_CASE ("Resampler passes samples through exactly and without delay at 1:1")
{
    Resampler r;
    r.prepare (48000.0, 48000.0);
    std::vector<float> out;
    for (int i = 1; i <= 10; ++i)
        r.push (beads::FloatFrame { static_cast<float> (i), -static_cast<float> (i) },
                [&] (const beads::FloatFrame& f) { out.push_back (f.l); });
    REQUIRE (out.size() == 10);
    CHECK (out[0] == 1.0f);
    CHECK (out[9] == 10.0f);
}

TEST_CASE ("Resampler is clean across the audio band (44.1 kHz to 48 kHz and back)")
{
    for (double f : { 100.0, 1000.0, 6000.0, 15000.0 })
    {
        const auto there = convert (44100.0, 48000.0, tone (44100.0, f, 88200));
        const auto back = convert (48000.0, 44100.0, there);
        INFO ("tone " << f << " Hz");
        CHECK (thdPlusNoiseDb (there, 48000.0, f, 4800, there.size() - 200) > 90.0);
        CHECK (thdPlusNoiseDb (back, 44100.0, f, 4410, back.size() - 200) > 85.0);
    }
}

TEST_CASE ("Resampler keeps the level and rejects aliases when converting down")
{
    // 1 kHz keeps its level through 44.1 kHz -> 24 kHz -> 44.1 kHz.
    const auto down = convert (44100.0, 24000.0, tone (44100.0, 1000.0, 44100));
    const auto up = convert (24000.0, 44100.0, down);
    double e = 0.0;
    for (std::size_t i = 4410; i < up.size() - 100; ++i)
        e += static_cast<double> (up[i]) * up[i];
    CHECK (std::sqrt (e / static_cast<double> (up.size() - 4510)) == Approx (0.5 / std::sqrt (2.0)).epsilon (0.01));

    // 15 kHz is above 24 kHz's Nyquist and must not fold back down.
    const auto alias = convert (44100.0, 24000.0, tone (44100.0, 15000.0, 44100));
    double a = 0.0;
    for (std::size_t i = 2400; i < alias.size() - 100; ++i)
        a += static_cast<double> (alias[i]) * alias[i];
    const double aliasDb = 20.0 * std::log10 (std::sqrt (a / static_cast<double> (alias.size() - 2500)) / (0.5 / std::sqrt (2.0)));
    CHECK (aliasDb < -85.0);
}

#include "dsp/OutputStage.h"

TEST_CASE ("Limiter never exceeds its ceiling and leaves quiet audio alone")
{
    Limiter lim;
    lim.prepare (48000.0, 0.977f);
    float maxOut = 0.0f;
    float maxQuietError = 0.0f;
    std::vector<float> history;
    for (int i = 0; i < 96000; ++i)
    {
        // Quiet for a second, then bursts up to +12 dB over full scale.
        const float level = i < 48000 ? 0.5f : (i / 1000) % 2 ? 4.0f : 0.3f;
        const float x = level * std::sin (static_cast<float> (i) * 0.05f);
        history.push_back (x);
        float l = x, r = -x;
        lim.process (l, r);
        maxOut = std::max (maxOut, std::fabs (l));
        if (i < 48000 && i >= lim.latency())
            maxQuietError = std::max (maxQuietError, std::fabs (l - history[static_cast<std::size_t> (i - lim.latency())]));
    }
    CHECK (maxOut <= 0.977f);
    CHECK (maxQuietError < 1.0e-6f);
}

TEST_CASE ("Sub-sonic filter removes DC and keeps the low end")
{
    SubsonicFilter hp;
    hp.prepare (48000.0);
    float y = 0.0f;
    for (int i = 0; i < 48000; ++i)
        y = hp.process (1.0f, 0);
    CHECK (std::fabs (y) < 1.0e-3f);

    hp.reset();
    double in = 0.0, out = 0.0;
    for (int i = 0; i < 96000; ++i)
    {
        const float x = std::sin (2.0f * 3.14159265f * 40.0f * static_cast<float> (i) / 48000.0f);
        const float v = hp.process (x, 1);
        if (i > 48000)
        {
            in += static_cast<double> (x) * x;
            out += static_cast<double> (v) * v;
        }
    }
    CHECK (10.0 * std::log10 (out / in) > -0.5); // 40 Hz is down less than half a dB
}
