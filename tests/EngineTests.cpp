#include "TestHelpers.h"
#include "dsp/RainEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>

using namespace rain::dsp;
using namespace rain::test;

namespace
{
    constexpr Quality kAllQualities[] = { Quality::digital, Quality::vintage, Quality::cleanTape, Quality::dirtyTape };

    std::vector<float> noise (std::size_t n, unsigned seed, float amp = 0.5f)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> d (-amp, amp);
        std::vector<float> v (n);
        for (auto& x : v) x = d (rng);
        return v;
    }

    std::vector<float> sine (std::size_t n, double fs, float hz, float amp)
    {
        std::vector<float> v (n);
        for (std::size_t i = 0; i < n; ++i)
            v[i] = amp * static_cast<float> (std::sin (2.0 * 3.141592653589793 * hz * static_cast<double> (i) / fs));
        return v;
    }

    // Runs the engine over whole buffers in blocks.
    void run (RainEngine& e, std::vector<float>& l, std::vector<float>& r, int block = 256)
    {
        for (std::size_t i = 0; i < l.size(); i += static_cast<std::size_t> (block))
        {
            const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (block), l.size() - i));
            e.process (l.data() + i, r.data() + i, n);
        }
    }

    double rms (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        return std::sqrt (energy (x.data() + from, to - from) / static_cast<double> (to - from));
    }

    RainParams wetGrains()
    {
        RainParams p;
        p.dryWet = 1.0f;
        p.reverb = 0.0f;
        p.feedback = 0.0f;
        p.density = 0.85f; // random grains, fairly dense
        p.size = 0.1f;
        p.time = 0.3f;
        return p;
    }
}

TEST_CASE ("Beads runs at its own rate for each quality")
{
    RainEngine e;
    RainParams p;
    e.prepare (44100.0, 512);
    const float expected[] = { 48000.0f, 32000.0f, 48000.0f, 24000.0f };
    for (int q = 0; q < 4; ++q)
    {
        p.quality = kAllQualities[q];
        e.setParams (p);
        CHECK (e.internalRate() == expected[q]);
        CHECK (e.latencySamples() > 0);
        CHECK (e.latencySamples() < 441); // under 10 ms
    }
}

TEST_CASE ("At 0% wet with no reverb, the dry signal comes through at unity gain")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        RainEngine e;
        RainParams p;
        p.dryWet = 0.0f;
        p.reverb = 0.0f;
        p.quality = Quality::digital;
        e.setParams (p);
        e.prepare (fs, 512);

        const auto n = static_cast<std::size_t> (fs * 2.0);
        auto l = sine (n, fs, 440.0f, 0.1f), r = l;
        const auto in = l;
        run (e, l, r);
        INFO ("sample rate " << fs);
        const double gainDb = 20.0 * std::log10 (rms (l, n / 2, n) / rms (in, n / 2, n));
        CHECK (std::abs (gainDb) < 0.3);
    }
}

TEST_CASE ("Silence in gives silence out in every quality")
{
    for (auto q : kAllQualities)
    {
        RainEngine e;
        RainParams p = wetGrains();
        p.reverb = 0.5f;
        p.quality = q;
        e.setParams (p);
        e.prepare (48000.0, 256);
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        run (e, l, r);
        CHECK (allFinite (l));
        CHECK (rms (l, 0, l.size()) < 1.0e-3);
    }
}

TEST_CASE ("Grains play back the input when density is up")
{
    RainEngine e;
    e.setParams (wetGrains());
    e.prepare (48000.0, 256);
    auto l = noise (48000 * 3, 1), r = noise (48000 * 3, 2);
    run (e, l, r);
    CHECK (rms (l, 48000 * 2, 48000 * 3) > 0.01);
}

TEST_CASE ("Freeze holds the buffer after the input stops")
{
    auto tailLevel = [] (bool freeze) {
        RainEngine e;
        RainParams p = wetGrains();
        e.setParams (p);
        e.prepare (48000.0, 256);

        auto l = noise (48000 * 2, 3), r = noise (48000 * 2, 4);
        run (e, l, r);

        p.freeze = freeze;
        e.setParams (p);
        std::vector<float> sl (48000 * 7, 0.0f), sr (48000 * 7, 0.0f);
        run (e, sl, sr);
        return rms (sl, 48000 * 6, 48000 * 7);
    };

    CHECK (tailLevel (true) > 0.01);
    CHECK (tailLevel (false) < 1.0e-3);
}

TEST_CASE ("No blow-ups under random automation, in every quality and at several rates")
{
    std::mt19937 rng (42);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);

    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        RainEngine e;
        e.prepare (fs, 512);
        float peak = 0.0f;
        bool finite = true;
        const int blocks = static_cast<int> (fs * 8.0 / 512.0);
        for (int b = 0; b < blocks; ++b)
        {
            if (b % 20 == 0)
            {
                RainParams p;
                p.time = u (rng); p.size = u (rng); p.shape = u (rng); p.density = u (rng);
                p.pitchSemis = 48.0f * u (rng) - 24.0f;
                p.timeRandom = 2.0f * u (rng) - 1.0f; p.sizeRandom = 2.0f * u (rng) - 1.0f;
                p.shapeRandom = 2.0f * u (rng) - 1.0f; p.pitchRandom = 2.0f * u (rng) - 1.0f;
                p.feedback = u (rng); p.dryWet = u (rng); p.reverb = u (rng);
                p.freeze = u (rng) > 0.8f; p.seed = u (rng) > 0.5f;
                p.quality = kAllQualities[static_cast<int> (u (rng) * 3.999f)];
                p.autoGain = u (rng) > 0.5f; p.inputGainDb = 32.0f * u (rng);
                p.outputGainDb = 12.0f;
                e.setParams (p);
            }
            auto l = noise (512, static_cast<unsigned> (b), 2.0f), r = noise (512, static_cast<unsigned> (b + 7), 2.0f);
            e.process (l.data(), r.data(), 512);
            finite = finite && allFinite (l) && allFinite (r);
            for (int i = 0; i < 512; ++i)
                peak = std::max ({ peak, std::abs (l[static_cast<std::size_t> (i)]), std::abs (r[static_cast<std::size_t> (i)]) });
        }
        INFO ("sample rate " << fs);
        CHECK (finite);
        CHECK (peak <= 1.0f);
    }
}

TEST_CASE ("Attenuverter law has a dead zone and reaches full scale")
{
    CHECK (RainEngine::attenuverterLaw (0.0f) == 0.0f);
    CHECK (RainEngine::attenuverterLaw (0.05f) == 0.0f);
    CHECK (RainEngine::attenuverterLaw (1.0f) == Catch::Approx (1.0f));
    CHECK (RainEngine::attenuverterLaw (-1.0f) == Catch::Approx (-1.0f));
}

TEST_CASE ("Hot input stays below full scale and the low end stays clean")
{
    // Bass at full scale through a dense, fully wet cloud: the output must stay under
    // the limiter's ceiling, and almost nothing above 2 kHz may appear (that would be
    // clipping or clicks; the input itself has nothing up there).
    const double fs = 44100.0;
    RainEngine e;
    RainParams p;
    p.dryWet = 1.0f;
    p.reverb = 0.0f;
    p.size = 0.49f;
    p.shape = 0.6f;
    p.density = 0.9f;
    e.setParams (p);
    e.prepare (fs, 256);

    const auto n = static_cast<std::size_t> (fs * 6.0);
    std::vector<float> l (n), r (n);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / fs;
        l[i] = r[i] = static_cast<float> (0.65 * std::sin (2.0 * 3.141592653589793 * 55.0 * t)
                                        + 0.35 * std::sin (2.0 * 3.141592653589793 * 82.4 * t));
    }
    run (e, l, r);
    REQUIRE (allFinite (l));

    Biquad hp[3];
    for (auto& f : hp)
        f.setHighpass (2000.0f, fs);
    double high = 0.0, total = 0.0;
    float peak = 0.0f;
    for (std::size_t i = 0; i < n; ++i)
    {
        const float h = hp[2].process (hp[1].process (hp[0].process (l[i])));
        if (i > n / 3)
        {
            high += static_cast<double> (h) * h;
            total += static_cast<double> (l[i]) * l[i];
            peak = std::max (peak, std::fabs (l[i]));
        }
    }
    CHECK (peak <= 0.977f);
    CHECK (10.0 * std::log10 (high / total) < -60.0);
}

TEST_CASE ("Bypass passes the dry input through, lined up with the latency")
{
    const double fs = 48000.0;
    RainEngine e;
    RainParams p;
    p.bypass = true;
    p.dryWet = 1.0f;
    e.setParams (p);
    e.prepare (fs, 256);
    const int lat = e.latencySamples();
    const auto n = static_cast<std::size_t> (fs);
    std::vector<float> inL (n), l (n), r (n);
    for (std::size_t i = 0; i < n; ++i)
        inL[i] = l[i] = r[i] = 0.5f * static_cast<float> (std::sin (2.0 * 3.141592653589793 * 440.0 * static_cast<double> (i) / fs));
    for (std::size_t i = 0; i < n; i += 256)
        e.process (l.data() + i, r.data() + i, static_cast<int> (std::min<std::size_t> (256, n - i)));
    float worst = 0.0f;
    for (std::size_t i = static_cast<std::size_t> (lat) + 1000; i < n; ++i)
        worst = std::max (worst, std::abs (l[i] - inL[i - static_cast<std::size_t> (lat)]));
    CHECK (worst < 1.0e-6f);
}

TEST_CASE ("Bypass fades in and out without clicks")
{
    const double fs = 48000.0;
    RainEngine e;
    RainParams p;
    p.dryWet = 1.0f;
    e.setParams (p);
    e.prepare (fs, 256);
    const auto n = static_cast<std::size_t> (fs * 2.0);
    std::vector<float> l (n), r (n);
    for (std::size_t i = 0; i < n; ++i)
        l[i] = r[i] = 0.3f * static_cast<float> (std::sin (2.0 * 3.141592653589793 * 220.0 * static_cast<double> (i) / fs));
    for (std::size_t i = 0; i < n; i += 256)
    {
        p.bypass = (i / 256) % 40 >= 20; // toggle every ~0.1 s
        e.setParams (p);
        e.process (l.data() + i, r.data() + i, static_cast<int> (std::min<std::size_t> (256, n - i)));
    }
    REQUIRE (allFinite (l));
    float biggestStep = 0.0f;
    for (std::size_t i = 1; i < n; ++i)
        biggestStep = std::max (biggestStep, std::abs (l[i] - l[i - 1]));
    CHECK (biggestStep < 0.2f);
}
