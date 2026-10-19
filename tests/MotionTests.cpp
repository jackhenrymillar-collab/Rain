#include "TestHelpers.h"
#include "dsp/Motion.h"
#include "dsp/RainEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <random>

using namespace rain::dsp;
using namespace rain::test;

TEST_CASE ("Wave modulator is a sine at the set rate")
{
    Modulator m (Modulator::Kind::wave);
    const float dt = 0.001f;
    float peak = 0.0f, trough = 0.0f;
    for (int i = 0; i < 1000; ++i) // one second at 1 Hz
    {
        const float v = m.advance (1.0f, dt);
        peak = std::max (peak, v);
        trough = std::min (trough, v);
        if (i == 249)
            CHECK (v == Catch::Approx (1.0f).margin (0.01f));
    }
    CHECK (peak == Catch::Approx (1.0f).margin (0.01f));
    CHECK (trough == Catch::Approx (-1.0f).margin (0.01f));
}

TEST_CASE ("Drift modulator stays in range and moves smoothly")
{
    Modulator m (Modulator::Kind::drift, 42);
    float prev = m.current();
    float lo = 0.0f, hi = 0.0f;
    for (int i = 0; i < 100000; ++i)
    {
        const float v = m.advance (2.0f, 0.001f);
        REQUIRE (v >= -1.0f);
        REQUIRE (v <= 1.0f);
        // At 2 Hz and 1 ms steps the largest possible step is pi/2 * 2 * 2 * 0.001.
        REQUIRE (std::fabs (v - prev) < 0.0065f);
        prev = v;
        lo = std::min (lo, v);
        hi = std::max (hi, v);
    }
    CHECK (lo < -0.7f);
    CHECK (hi > 0.7f);
}

TEST_CASE ("Pitch quantising snaps to the nearest scale note")
{
    CHECK (quantisePitch (3.3f, Scale::off) == Catch::Approx (3.3f));
    CHECK (quantisePitch (3.3f, Scale::chromatic) == 3.0f);
    CHECK (quantisePitch (3.4f, Scale::major) == 4.0f);
    CHECK (quantisePitch (5.9f, Scale::major) == 5.0f);
    CHECK (quantisePitch (6.1f, Scale::major) == 7.0f);
    CHECK (quantisePitch (-1.2f, Scale::major) == -1.0f);  // B below the root
    CHECK (quantisePitch (-1.2f, Scale::minor) == -2.0f);
    CHECK (quantisePitch (5.0f, Scale::pentatonic) == 4.0f);
    CHECK (quantisePitch (2.0f, Scale::minorPentatonic) == 3.0f);
    CHECK (quantisePitch (10.0f, Scale::fifths) == 12.0f);
    CHECK (quantisePitch (-5.0f, Scale::fifths) == -5.0f);
    CHECK (quantisePitch (17.0f, Scale::octaves) == 12.0f);
    CHECK (quantisePitch (-19.0f, Scale::octaves) == -24.0f);

    for (float s = -24.0f; s <= 24.0f; s += 0.37f)
    {
        const float q = quantisePitch (s, Scale::pentatonic);
        const int pc = ((static_cast<int> (q) % 12) + 12) % 12;
        CHECK ((pc == 0 || pc == 2 || pc == 4 || pc == 7 || pc == 9));
        CHECK (std::fabs (q - s) <= 1.5f);
    }
}

TEST_CASE ("Sync divider follows Beads' clock division table")
{
    CHECK (syncDivider (0.0f) == 1);
    CHECK (syncDivider (0.1f) == 2);
    CHECK (syncDivider (0.46f) == 16);
    CHECK (syncDivider (0.5f) == 1);
    CHECK (syncDivider (0.7f) == 0);
}

namespace
{
    // Sine input, fully wet: what comes out is grains only.
    std::vector<float> runSynced (RainEngine& e, double fs, std::size_t n)
    {
        std::vector<float> l (n), r (n);
        for (std::size_t i = 0; i < n; ++i)
            l[i] = r[i] = 0.5f * static_cast<float> (std::sin (2.0 * 3.141592653589793 * 330.0 * static_cast<double> (i) / fs));
        for (std::size_t i = 0; i < n; i += 256)
        {
            const int len = static_cast<int> (std::min<std::size_t> (256, n - i));
            e.setTransport (120.0, 0.0, false);
            e.process (l.data() + i, r.data() + i, len);
        }
        return l;
    }
}

TEST_CASE ("Synced grains land on the host clock")
{
    const double fs = 48000.0;
    RainEngine e;
    RainParams p;
    p.dryWet = 1.0f;
    p.reverb = 0.0f;
    p.density = 0.0f;  // every clock pulse
    p.size = 0.36f;    // very short grains
    p.shape = 0.0f;
    p.time = 0.0f;
    p.sync = true;
    p.syncDivision = 2; // 1/16 at 120 bpm = 125 ms
    e.setParams (p);
    e.prepare (fs, 256);

    const auto n = static_cast<std::size_t> (fs * 6.0);
    const auto out = runSynced (e, fs, n);
    REQUIRE (allFinite (out));

    // Energy just after each clock edge vs. the quiet middle of each step.
    const auto step = static_cast<std::size_t> (fs * 0.125);
    const auto lat = static_cast<std::size_t> (e.latencySamples());
    double onBeat = 0.0, offBeat = 0.0;
    for (std::size_t k = 8; k < 44; ++k)
    {
        const std::size_t edge = k * step + lat;
        onBeat += energy (out.data() + edge, static_cast<std::size_t> (fs * 0.03));
        offBeat += energy (out.data() + edge + static_cast<std::size_t> (fs * 0.06), static_cast<std::size_t> (fs * 0.03));
    }
    INFO ("on " << onBeat << " off " << offBeat);
    CHECK (onBeat > 0.01);
    CHECK (onBeat > 20.0 * offBeat);
}

TEST_CASE ("Modulators move their target and report it to the panel")
{
    RainEngine e;
    RainParams p;
    p.size = 0.5f;
    p.mods[0] = { 2.0f, 1.0f, ModTarget::size };
    e.setParams (p);
    e.prepare (48000.0, 256);

    std::vector<float> l (256), r (256);
    float lo = 1.0f, hi = 0.0f;
    for (int b = 0; b < 400; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e.process (l.data(), r.data(), 256);
        const float s = e.monitor().effective[static_cast<std::size_t> (ModTarget::size)].load();
        lo = std::min (lo, s);
        hi = std::max (hi, s);
    }
    CHECK (lo < 0.05f);
    CHECK (hi > 0.95f);
}

TEST_CASE ("Synced TIME snaps to whole clock steps")
{
    RainEngine e;
    RainParams p;
    p.time = 0.33f;
    p.sync = true;
    p.syncDivision = 0; // quarter notes: 0.5 s of a 4 s buffer
    e.setParams (p);
    e.prepare (48000.0, 256);
    std::vector<float> l (256), r (256);
    e.setTransport (120.0, 0.0, false);
    e.process (l.data(), r.data(), 256);
    CHECK (e.monitor().effective[static_cast<std::size_t> (ModTarget::time)].load() == Catch::Approx (0.375f));
}
