// Sound quality: the untouched dry path, Studio quality, band-limited pitched grains,
// formant keep and phase-aligned grain starts.

#include "TestHelpers.h"
#include "dsp/RainEngine.h"

#include "beads/dsp/delay_granular_player.h"
#include "beads/dsp/delay_line.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

using namespace rain::dsp;
using namespace rain::test;

namespace
{
    constexpr double kPi = 3.141592653589793;

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

    std::vector<float> sine (std::size_t n, double fs, double hz, float amp)
    {
        std::vector<float> v (n);
        for (std::size_t i = 0; i < n; ++i)
            v[i] = amp * static_cast<float> (std::sin (2.0 * kPi * hz * static_cast<double> (i) / fs));
        return v;
    }

    // Energy in a band, summed over Goertzel bins every 25 Hz.
    double bandEnergy (const std::vector<float>& x, std::size_t from, std::size_t to, double fs, double lo, double hi)
    {
        double total = 0.0;
        for (double f = lo; f <= hi; f += 25.0)
        {
            const double w = 2.0 * kPi * f / fs, c = 2.0 * std::cos (w);
            double s1 = 0.0, s2 = 0.0;
            for (std::size_t i = from; i < to; ++i)
            {
                const double s0 = x[i] + c * s1 - s2;
                s2 = s1;
                s1 = s0;
            }
            total += s1 * s1 + s2 * s2 - c * s1 * s2;
        }
        return total;
    }

    // A buzzy "voice": a 150 Hz pulse train through a resonance at 1 kHz (a formant).
    std::vector<float> voice (std::size_t n, double fs)
    {
        std::vector<float> v (n);
        const double f = 1000.0, bw = 120.0;
        const double r = std::exp (-kPi * bw / fs), a1 = 2.0 * r * std::cos (2.0 * kPi * f / fs), a2 = -r * r;
        double y1 = 0.0, y2 = 0.0;
        const auto period = static_cast<std::size_t> (fs / 150.0);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double in = i % period == 0 ? 1.0 : 0.0;
            const double y = in + a1 * y1 + a2 * y2;
            y2 = y1;
            y1 = y;
            v[i] = static_cast<float> (0.05 * y);
        }
        return v;
    }

    RainParams grainsOnly()
    {
        RainParams p;
        p.dryWet = 1.0f; // grains only
        p.reverb = 0.0f;
        p.feedback = 0.0f;
        p.soften = 0.0f;
        p.density = 0.85f;
        p.size = 0.5f;
        p.time = 0.2f;
        p.outputGainDb = 0.0f;
        return p;
    }
}

TEST_CASE ("The dry signal passes untouched, in every quality and at any rate")
{
    for (auto q : { Quality::digital, Quality::vintage, Quality::dirtyTape, Quality::studio })
    {
        for (double fs : { 44100.0, 96000.0 })
        {
            RainEngine e;
            RainParams p;
            p.dryWet = 0.0f;
            p.reverb = 0.0f;
            p.quality = q;
            p.outputGainDb = 0.0f;
            e.setParams (p);
            e.prepare (fs, 256);

            // Noise with full-band content, which Beads' own path would have
            // band-limited (and, in Vintage and Dirty Tape, quantised).
            const auto n = static_cast<std::size_t> (fs * 2.0);
            std::mt19937 rng (5);
            std::uniform_real_distribution<float> d (-0.3f, 0.3f);
            std::vector<float> l (n), r (n);
            for (std::size_t i = 0; i < n; ++i)
                l[i] = r[i] = d (rng);
            const auto in = l;
            run (e, l, r);

            // At 0% wet Beads gives the dry signal a gain of 0.75.
            const auto lag = static_cast<std::size_t> (e.latencySamples());
            double err = 0.0, ref = 0.0;
            for (std::size_t i = n / 2; i < n; ++i)
            {
                const double expected = 0.75 * in[i - lag];
                err += (l[i] - expected) * (l[i] - expected);
                ref += expected * expected;
            }
            INFO ("quality " << static_cast<int> (q) << ", sample rate " << fs);
            CHECK (10.0 * std::log10 (err / ref) < -80.0);
        }
    }
}

TEST_CASE ("Studio keeps detail below the 16-bit floor")
{
    // A tone too quiet for Beads' 16-bit buffer to hold at all.
    auto wetLevel = [] (Quality q) {
        RainEngine e;
        RainParams p = grainsOnly();
        p.quality = q;
        e.setParams (p);
        e.prepare (48000.0, 256);
        auto l = sine (48000 * 3, 48000.0, 440.0, 2.0e-5f), r = l;
        run (e, l, r);
        return rms (l, 48000 * 2, 48000 * 3);
    };
    const double digital = wetLevel (Quality::digital);
    const double studio = wetLevel (Quality::studio);
    INFO ("digital " << digital << ", studio " << studio);
    CHECK (studio > 2.0e-6);
    CHECK (digital < 0.1 * studio);
    CHECK (RainEngine::bufferSeconds (Quality::studio) == 8.0f);
}

TEST_CASE ("Grains pitched up two octaves don't alias")
{
    auto wetLevel = [] (double hz) {
        RainEngine e;
        RainParams p = grainsOnly();
        p.pitchSemis = 24.0f;
        p.quality = Quality::digital;
        e.setParams (p);
        e.prepare (48000.0, 256);
        auto l = sine (48000 * 3, 48000.0, hz, 0.3f), r = l;
        run (e, l, r);
        return rms (l, 48000 * 2, 48000 * 3);
    };
    // 2 kHz becomes 8 kHz and plays; 9 kHz would become 36 kHz, above the top of
    // the audio band, so it must vanish rather than fold back as a 12 kHz whistle.
    const double inBand = wetLevel (2000.0);
    const double aliased = wetLevel (9000.0);
    INFO ("in band " << inBand << ", above the band " << aliased);
    CHECK (inBand > 0.03);
    CHECK (aliased < 0.01 * inBand);
}

TEST_CASE ("Formant keep holds the tone colour when grains are pitched")
{
    // One octave up, a resonance at 1 kHz moves to 2 kHz when resampled, but stays at
    // 1 kHz with Formant on.
    auto bands = [] (bool formant) {
        RainEngine e;
        RainParams p = grainsOnly();
        p.pitchSemis = 12.0f;
        p.size = 0.75f;
        p.formant = formant;
        p.quality = Quality::studio;
        e.setParams (p);
        e.prepare (48000.0, 256);
        auto l = voice (48000 * 4, 48000.0), r = l;
        run (e, l, r);
        const std::size_t from = 48000 * 2, to = 48000 * 4;
        return std::pair { bandEnergy (l, from, to, 48000.0, 800.0, 1250.0),
                           bandEnergy (l, from, to, 48000.0, 1750.0, 2200.0) };
    };
    const auto [lowOff, highOff] = bands (false);
    const auto [lowOn, highOn] = bands (true);
    INFO ("resampled: 1 kHz " << lowOff << ", 2 kHz " << highOff);
    INFO ("formant:   1 kHz " << lowOn << ", 2 kHz " << highOn);
    CHECK (highOff > lowOff);       // resampling moves the formant up
    CHECK (lowOn > 2.0 * highOn);   // formant keep leaves it where it was
}

TEST_CASE ("Phase-aligned grains add up instead of cancelling")
{
    // Long, densely overlapping grains of a steady tone, read from random places. In
    // phase, they reinforce each other and the level holds steady; at random phases
    // they partly cancel, and the level swells and dips as grains come and go.
    auto level = [] (bool align) {
        constexpr std::size_t kBank = 262144;
        std::vector<std::uint8_t> memory (3 * kBank, 0);
        std::uint8_t* banks[3] = { memory.data(), memory.data() + kBank, memory.data() + 2 * kBank };
        auto line = std::make_unique<beads::DelayLine<beads::RESOLUTION_FLOAT, 48000, 2>>();
        line->Init (banks);
        auto player = std::make_unique<beads::DelayGranularPlayer>();
        player->Init (48000.0f);

        beads::Parameters p;
        p.time = { 0.5f, 0.6f, 0.0f, false };
        p.size = { 0.7f, 0.0f, 0.0f, false };
        p.shape = { 0.5f, 0.0f, 0.0f, false };
        p.pitch = { 0.0f, 0.0f, 0.0f, false };
        p.monophonic_mode = false;
        p.phase_align = align;

        stmlib::Random::Seed (7);
        std::vector<double> windows; // RMS of each 10 ms
        double sum = 0.0;
        std::size_t count = 0;
        for (int i = 0; i < 48000 * 3; ++i)
        {
            const float x = 0.3f * static_cast<float> (std::sin (2.0 * kPi * 220.0 * i / 48000.0));
            line->Write (beads::FloatFrame { x, x }, false);
            beads::TriggerGenerator::Output t {};
            t.trigger = i > 48000 && i % 960 == 0; // a grain every 20 ms after the first second
            t.regularity = 1.0f;
            beads::FloatFrame out { 0.0f, 0.0f };
            player->Render (line.get(), t, p, &out);
            if (i > 48000 * 2)
            {
                sum += static_cast<double> (out.l) * out.l;
                if (++count == 480)
                {
                    windows.push_back (std::sqrt (sum / 480.0));
                    sum = 0.0;
                    count = 0;
                }
            }
        }
        double mean = 0.0, var = 0.0;
        for (double w : windows) mean += w;
        mean /= static_cast<double> (windows.size());
        for (double w : windows) var += (w - mean) * (w - mean);
        var /= static_cast<double> (windows.size());
        return std::pair { mean, std::sqrt (var) / mean }; // level, and how much it wavers
    };
    const auto [alignedLevel, alignedWaver] = level (true);
    const auto [randomLevel, randomWaver] = level (false);
    INFO ("aligned: level " << alignedLevel << ", waver " << alignedWaver);
    INFO ("unaligned: level " << randomLevel << ", waver " << randomWaver);
    CHECK (alignedWaver < 0.5 * randomWaver);
    CHECK (alignedLevel > randomLevel);
    CHECK (alignedLevel < 1.5 * randomLevel); // louder, but not by much
}
