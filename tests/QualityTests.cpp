// Sound quality: the untouched dry path, Studio quality, band-limited pitched grains,
// formant keep and phase-aligned grain starts.

#include "TestHelpers.h"
#include "dsp/RainEngine.h"

#include "beads/dsp/delay_granular_player.h"
#include "beads/dsp/delay_line.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
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

TEST_CASE ("Formant keep stays in tune and smooth on a sung note with vibrato")
{
    // A 196 Hz pulse train with vibrato through two formants, pitched a fifth up. The
    // slices must follow the source's pulses: if they drift off them, the output loses
    // its steady pitch and turns rough. Measured as how well each 40 ms stretch repeats
    // at the new pitch period (1 = perfectly periodic).
    constexpr double fs = 48000.0;
    std::vector<float> v (static_cast<std::size_t> (fs * 5));
    {
        struct Res { double a1, a2, g, y1 = 0.0, y2 = 0.0; };
        auto res = [] (double f, double bw, double g) {
            const double r = std::exp (-kPi * bw / fs);
            return Res { 2.0 * r * std::cos (2.0 * kPi * f / fs), -r * r, g };
        };
        Res rs[] = { res (700.0, 110.0, 1.0), res (1220.0, 120.0, 0.5) };
        double phase = 0.0;
        for (std::size_t i = 0; i < v.size(); ++i)
        {
            const double t = static_cast<double> (i) / fs;
            phase += 196.0 * (1.0 + 0.008 * std::sin (2.0 * kPi * 5.2 * t)) / fs;
            const double in = phase >= 1.0 ? 1.0 : 0.0;
            if (phase >= 1.0)
                phase -= 1.0;
            double y = 0.0;
            for (auto& r : rs)
            {
                const double o = in + r.a1 * r.y1 + r.a2 * r.y2;
                r.y2 = r.y1;
                r.y1 = o;
                y += r.g * o;
            }
            v[i] = static_cast<float> (0.03 * y);
        }
    }
    RainEngine e;
    RainParams p = grainsOnly();
    p.pitchSemis = 7.0f;
    p.size = 0.6f;
    p.density = 0.75f;
    p.time = 0.1f;
    p.overlap = true;
    p.formant = true;
    p.quality = Quality::studio;
    e.setParams (p);
    e.prepare (fs, 512);
    auto l = v, r = v;
    run (e, l, r);

    const double period = fs / (196.0 * std::pow (2.0, 7.0 / 12.0));
    constexpr std::size_t kFrame = 1920;
    double sum = 0.0;
    int frames = 0;
    for (auto i = static_cast<std::size_t> (fs * 1.5); i + kFrame + 2 * static_cast<std::size_t> (period) < l.size(); i += kFrame / 2)
    {
        double best = 0.0;
        for (auto lag = static_cast<std::size_t> (period * 0.97); lag <= static_cast<std::size_t> (period * 1.03) + 1; ++lag)
        {
            double ab = 0.0, aa = 0.0, bb = 0.0;
            for (std::size_t n = 0; n < kFrame; ++n)
            {
                ab += l[i + n] * l[i + n + lag];
                aa += l[i + n] * l[i + n];
                bb += l[i + n + lag] * l[i + n + lag];
            }
            best = std::max (best, ab / std::sqrt (aa * bb + 1e-12));
        }
        sum += best;
        ++frames;
    }
    const double periodicity = sum / frames;
    INFO ("periodicity " << periodicity);
    CHECK (periodicity > 0.93);
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

TEST_CASE ("Frozen audio survives a change of quality")
{
    // Record a tone, freeze, then go silent and switch quality: the grains should keep
    // playing the frozen tone, at the same pitch, in every quality.
    for (auto from : { Quality::digital, Quality::studio, Quality::dirtyTape })
    {
        for (auto to : { Quality::digital, Quality::vintage, Quality::cleanTape, Quality::dirtyTape, Quality::studio })
        {
            if (from == to)
                continue;
            RainEngine e;
            RainParams p = grainsOnly();
            p.quality = from;
            p.time = 0.1f;
            e.setParams (p);
            e.prepare (48000.0, 256);

            auto l = sine (48000 * 2, 48000.0, 440.0, 0.3f), r = l;
            run (e, l, r);

            p.freeze = true;
            e.setParams (p);
            std::vector<float> sl (48000 / 2, 0.0f), sr = sl;
            run (e, sl, sr);

            p.quality = to;
            e.setParams (p);
            std::vector<float> ol (48000 * 2, 0.0f), orr = ol;
            run (e, ol, orr);

            const std::size_t from0 = 48000 / 2, to0 = 48000 * 2;
            const double level = rms (ol, from0, to0);
            const double at440 = bandEnergy (ol, from0, to0, 48000.0, 425.0, 455.0);
            // Where the tone would land if the buffer were read at the wrong rate.
            double nearby = 0.0;
            for (double wrong : { 220.0, 293.3, 660.0, 880.0 })
                nearby += bandEnergy (ol, from0, to0, 48000.0, wrong - 15.0, wrong + 15.0);
            INFO ("from " << static_cast<int> (from) << " to " << static_cast<int> (to) << ": level " << level
                  << ", 440 Hz " << at440 << ", at wrong pitches " << nearby);
            CHECK (level > 0.02);
            CHECK (at440 > 20.0 * nearby);
        }
    }
}

TEST_CASE ("Frozen audio survives a host reset and a re-prepare")
{
    for (int howToInterrupt = 0; howToInterrupt < 2; ++howToInterrupt)
    {
        RainEngine e;
        RainParams p = grainsOnly();
        p.time = 0.1f;
        e.setParams (p);
        e.prepare (48000.0, 256);
        auto l = sine (48000 * 2, 48000.0, 440.0, 0.3f), r = l;
        run (e, l, r);
        p.freeze = true;
        e.setParams (p);

        if (howToInterrupt == 0)
            e.reset();                  // e.g. transport stop in Logic
        else
            e.prepare (48000.0, 1024);  // e.g. a new buffer size

        std::vector<float> ol (48000 * 2, 0.0f), orr = ol;
        run (e, ol, orr);
        INFO ((howToInterrupt == 0 ? "reset" : "re-prepare"));
        CHECK (rms (ol, 48000 / 2, 48000 * 2) > 0.02);
    }
}

TEST_CASE ("The dry signal carries on through a change of quality")
{
    RainEngine e;
    RainParams p;
    p.dryWet = 0.0f;
    p.reverb = 0.0f;
    p.outputGainDb = 0.0f;
    e.setParams (p);
    e.prepare (48000.0, 64);
    auto l = sine (48000, 48000.0, 200.0, 0.5f), r = l;
    std::vector<float> out;
    for (std::size_t i = 0; i < l.size(); i += 64)
    {
        if (i == 24000)
        {
            p.quality = Quality::cleanTape;
            e.setParams (p);
        }
        e.process (l.data() + i, r.data() + i, 64);
    }
    // No gap: the tone never drops to silence for more than a sample or two (it only
    // passes through zero as the sine crosses it).
    int run0 = 0, longest = 0;
    for (std::size_t i = 23000; i < 30000; ++i)
    {
        run0 = std::abs (l[i]) < 0.01f ? run0 + 1 : 0;
        longest = std::max (longest, run0);
    }
    INFO ("longest near-silent stretch " << longest << " samples");
    CHECK (longest < 10); // was 20 before the fix
}

TEST_CASE ("A NaN from the host does not silence Rain for good")
{
    RainEngine e;
    RainParams p = grainsOnly();
    e.setParams (p);
    e.prepare (48000.0, 256);
    auto l = sine (48000, 48000.0, 440.0, 0.3f), r = l;
    l[1000] = r[1000] = std::numeric_limits<float>::quiet_NaN();
    l[2000] = std::numeric_limits<float>::infinity();
    run (e, l, r);
    auto l2 = sine (48000 * 2, 48000.0, 440.0, 0.3f), r2 = l2;
    run (e, l2, r2);
    bool allFinite = true;
    for (float v : l2) allFinite = allFinite && std::isfinite (v);
    CHECK (allFinite);
    CHECK (rms (l2, 48000, 48000 * 2) > 0.02);
}

TEST_CASE ("Latency is the same in every quality")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        int first = -1;
        for (auto q : { Quality::digital, Quality::vintage, Quality::cleanTape, Quality::dirtyTape, Quality::studio })
        {
            RainEngine e;
            RainParams p;
            p.quality = q;
            e.setParams (p);
            e.prepare (fs, 256);
            if (first < 0)
                first = e.latencySamples();
            INFO ("rate " << fs << ", quality " << static_cast<int> (q));
            CHECK (e.latencySamples() == first);
        }
    }
}

TEST_CASE ("Changing quality mid-sound doesn't click")
{
    // A steady tone through a half-wet patch; switch quality and look for a spike in
    // the second difference (a click) compared with the settled sound.
    for (auto [from, to] : { std::pair { Quality::digital, Quality::vintage }, { Quality::studio, Quality::dirtyTape },
                             { Quality::cleanTape, Quality::studio } })
    {
        RainEngine e;
        RainParams p;
        p.density = 0.3f;
        p.dryWet = 0.5f;
        p.reverb = 0.2f;
        p.quality = from;
        e.setParams (p);
        e.prepare (48000.0, 64);
        const std::size_t n = 48000 * 3, change = 48000 * 2;
        auto l = sine (n, 48000.0, 110.0, 0.3f), r = l;
        for (std::size_t i = 0; i < n; i += 64)
        {
            if (i == change)
            {
                p.quality = to;
                e.setParams (p);
            }
            e.process (l.data() + i, r.data() + i, 64);
        }
        auto spiky = [&] (std::size_t a, std::size_t b) {
            double m = 0.0;
            for (std::size_t i = a; i < b; ++i)
                m = std::max (m, static_cast<double> (std::abs (l[i] - 2.0f * l[i - 1] + l[i - 2])));
            return m / rms (l, a, b);
        };
        const double settled = std::max (spiky (change - 24000, change), spiky (change + 24000, n));
        const double around = spiky (change, change + 4800);
        INFO ("from " << static_cast<int> (from) << " to " << static_cast<int> (to) << ": " << around / settled);
        CHECK (around < 1.5 * settled);
    }
}
