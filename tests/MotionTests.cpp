#include "TestHelpers.h"
#include "dsp/Motion.h"
#include "dsp/RainEngine.h"

#include "beads/dsp/randomized_parameter.h"

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
    p.size = 0.04f;    // very short grains
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

TEST_CASE ("SIZE runs through forward grain lengths and inverts cleanly")
{
    CHECK (RainEngine::grainSeconds (RainEngine::beadsSize (0.0f)) == Catch::Approx (4.0f / 512.0f).epsilon (0.01));
    CHECK (RainEngine::grainSeconds (RainEngine::beadsSize (1.0f)) > 3.0f);
    for (float s : { 0.01f, 0.05f, 0.2f, 1.0f })
        CHECK (RainEngine::grainSeconds (RainEngine::beadsSizeForSeconds (s)) == Catch::Approx (s).epsilon (0.01));
}

TEST_CASE ("Overlap keeps the grain size and raises the grain rate to fill gaps")
{
    auto run = [] (bool overlap, float size, float density) {
        RainEngine e;
        RainParams p;
        p.overlap = overlap;
        p.size = size;
        p.density = density;
        e.setParams (p);
        e.prepare (48000.0, 256);
        std::vector<float> l (256, 0.0f), r (256, 0.0f);
        for (int b = 0; b < 40; ++b)
            e.process (l.data(), r.data(), 256);
        return std::pair { e.monitor().grainSeconds.load(), e.monitor().spawnDensity.load() };
    };

    for (float density : { 0.3f, 0.8f })
    {
        const auto [lengthOff, densityOff] = run (false, 0.4f, density);
        const auto [lengthOn, densityOn] = run (true, 0.4f, density);
        CHECK (lengthOn == Catch::Approx (lengthOff)); // SIZE is untouched
        CHECK (densityOff == Catch::Approx (density));
        // Sparse grains come often enough to overlap (2x regular; 5x random, since
        // random triggers bunch up and leave gaps at lower counts).
        const float rate = RainEngine::grainRate (densityOn, 0.0f);
        CHECK (rate * lengthOn == Catch::Approx (density < 0.5f ? 2.0f : 5.0f).epsilon (0.02));
    }

    // Already dense grains, and DENSITY at noon, are left alone.
    CHECK (run (true, 0.4f, 0.02f).second == Catch::Approx (0.02f));
    CHECK (run (true, 0.4f, 0.5f).second == Catch::Approx (0.5f));

    // On a clock, grains fire on the tempo: 1/16 at 120 bpm, every pulse.
    CHECK (RainEngine::grainRate (0.0f, 8.0f) == Catch::Approx (8.0f));
    CHECK (RainEngine::grainRate (0.2f, 8.0f) == Catch::Approx (2.0f)); // every 4th pulse
}

namespace
{
    // Fraction of rising sample-to-sample steps in a signal.
    double risingShare (const std::vector<float>& x, std::size_t from)
    {
        std::size_t up = 0, down = 0;
        for (std::size_t i = from + 1; i < x.size(); ++i)
        {
            const float d = x[i] - x[i - 1];
            if (d > 1.0e-6f) ++up;
            else if (d < -1.0e-6f) ++down;
        }
        return static_cast<double> (up) / static_cast<double> (std::max<std::size_t> (1, up + down));
    }

    std::vector<float> runSaw (float reverse)
    {
        const double fs = 48000.0;
        RainEngine e;
        RainParams p;
        p.dryWet = 1.0f;
        p.reverb = 0.0f;
        p.soften = 0.0f;
        p.density = 0.25f; // regular grains
        p.size = 0.5f;
        p.shape = 0.0f;    // flat-topped grains
        p.time = 0.2f;
        p.reverse = reverse;
        e.setParams (p);
        e.prepare (fs, 256);
        const auto n = static_cast<std::size_t> (fs * 8.0);
        std::vector<float> l (n), r (n);
        for (std::size_t i = 0; i < n; ++i) // rising 200 Hz sawtooth
        {
            const double ph = std::fmod (200.0 * static_cast<double> (i) / fs, 1.0);
            l[i] = r[i] = static_cast<float> (0.4 * (2.0 * ph - 1.0));
        }
        for (std::size_t i = 0; i < n; i += 256)
            e.process (l.data() + i, r.data() + i, static_cast<int> (std::min<std::size_t> (256, n - i)));
        return l;
    }
}

TEST_CASE ("REVERSE plays grains backwards")
{
    // A rising sawtooth played backwards falls.
    const auto forward = runSaw (0.0f);
    const auto backward = runSaw (1.0f);
    const auto half = runSaw (0.5f);
    REQUIRE (allFinite (backward));
    const std::size_t from = 48000;
    INFO ("forward " << risingShare (forward, from) << " backward " << risingShare (backward, from) << " half " << risingShare (half, from));
    CHECK (risingShare (forward, from) > 0.8);
    CHECK (risingShare (backward, from) < 0.2);
    // About half the grains each way (a statistical check, so the bounds are loose).
    CHECK (risingShare (half, from) > 0.2);
    CHECK (risingShare (half, from) < 0.8);
}

namespace
{
    double wetLevel (float freqHz, float soften)
    {
        const double fs = 48000.0;
        RainEngine e;
        RainParams p;
        p.dryWet = 1.0f;
        p.reverb = 0.0f;
        p.density = 0.2f; // regular, dense grains
        p.size = 0.6f;
        p.shape = 0.5f;
        p.soften = soften;
        e.setParams (p);
        e.prepare (fs, 256);
        const auto n = static_cast<std::size_t> (fs * 3.0);
        std::vector<float> l (n), r (n);
        for (std::size_t i = 0; i < n; ++i)
            l[i] = r[i] = 0.3f * static_cast<float> (std::sin (2.0 * 3.141592653589793 * freqHz * static_cast<double> (i) / fs));
        for (std::size_t i = 0; i < n; i += 256)
            e.process (l.data() + i, r.data() + i, static_cast<int> (std::min<std::size_t> (256, n - i)));
        return std::sqrt (energy (l.data() + n / 2, n / 2) / static_cast<double> (n / 2));
    }
}

TEST_CASE ("SOFTEN rolls off the highs and lows of the grains and keeps the middle")
{
    const double highRatio = wetLevel (7000.0f, 1.0f) / wetLevel (7000.0f, 0.0f);
    const double midRatio = wetLevel (700.0f, 1.0f) / wetLevel (700.0f, 0.0f);
    const double lowRatio = wetLevel (60.0f, 1.0f) / wetLevel (60.0f, 0.0f);
    INFO ("high " << highRatio << " middle " << midRatio << " low " << lowRatio);
    CHECK (highRatio < 0.25);
    CHECK (lowRatio < 0.4);
    CHECK (midRatio > 0.75);
    CHECK (midRatio < 1.1);
}

TEST_CASE ("Consonance favours the root, fifth and thirds")
{
    CHECK (consonance (0.0f) > consonance (7.0f));
    CHECK (consonance (12.0f) == consonance (0.0f));
    CHECK (consonance (-5.0f) == consonance (7.0f));
    CHECK (consonance (7.0f) > consonance (4.0f));
    CHECK (consonance (4.0f) > consonance (2.0f));
}

TEST_CASE ("PITCH follows the Beads knob law")
{
    // Each sixth of the travel holds an interval for its first third.
    CHECK (detuneSemitones (0.0f) == 0.0f);
    CHECK (detuneSemitones (0.05f) == 0.0f);
    CHECK (detuneSemitones (1.0f / 6.0f + 0.05f) == Catch::Approx (1.0f));
    CHECK (detuneSemitones (4.0f / 6.0f + 0.05f) == Catch::Approx (7.0f));
    CHECK (detuneSemitones (5.0f / 6.0f + 0.05f) == Catch::Approx (12.0f));
    CHECK (detuneSemitones (1.0f) == Catch::Approx (24.0f));
    CHECK (detuneSemitones (-1.0f) == Catch::Approx (-24.0f));
    // Halfway along a glide is halfway between the intervals.
    CHECK (detuneSemitones ((4.0f + 1.0f / 3.0f + 1.0f / 3.0f) / 6.0f) == Catch::Approx (9.5f).margin (0.01f));
    float previous = -100.0f;
    for (int i = -100; i <= 100; ++i)
    {
        const float st = detuneSemitones (static_cast<float> (i) / 100.0f);
        CHECK (st >= previous);
        previous = st;
    }
    for (float st : { -24.0f, -12.0f, -7.0f, 0.0f, 1.0f, 3.0f, 4.0f, 5.5f, 7.0f, 12.0f, 18.0f, 24.0f })
        CHECK (detuneSemitones (detunePosition (st)) == Catch::Approx (st).margin (0.01f));
}

TEST_CASE ("Random values follow a slow drift with a little per-grain variation")
{
    // Full TIME RANDOM: on the module each grain lands anywhere; with correlated
    // randomness grains stay within a narrow band around the drifting value.
    beads::RandomizedParameter r { 0.5f, 1.0f, 0.0f, false };
    for (float drift : { -0.8f, 0.0f, 0.6f })
    {
        r.drift = drift;
        r.jitter = 0.2f;
        const float centre = 0.8f * (0.5f + 0.5f * drift);
        float lo = 1.0f, hi = 0.0f;
        for (int i = 0; i < 2000; ++i)
        {
            const float v = r.value();
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        INFO ("drift " << drift << ": " << lo << " to " << hi);
        CHECK (lo >= centre - 1.0e-4f);
        CHECK (hi <= centre + 0.2f + 1.0e-4f);
        CHECK (hi - lo > 0.15f); // still some variation per grain
    }

    // jitter 1 is the module's behaviour: anywhere in the range.
    r.jitter = 1.0f;
    float lo = 1.0f, hi = 0.0f;
    for (int i = 0; i < 2000; ++i)
    {
        lo = std::min (lo, r.value());
        hi = std::max (hi, r.value());
    }
    CHECK (lo < 0.05f);
    CHECK (hi > 0.95f);
}

TEST_CASE ("Randomness is per grain by default and smooth only when asked")
{
    for (bool smooth : { false, true })
    {
        RainEngine e;
        RainParams p;
        p.timeRandom = 1.0f;
        p.smoothRandom = smooth;
        e.setParams (p);
        e.prepare (48000.0, 512);
        std::vector<float> l (512 * 94, 0.1f), r = l;
        for (std::size_t i = 0; i < l.size(); i += 512)
            e.process (l.data() + i, r.data() + i, 512);
        INFO ("smooth " << smooth);
        CHECK (e.monitor().randomJitter.load() == Catch::Approx (smooth ? 0.2f : 1.0f));
    }
}
