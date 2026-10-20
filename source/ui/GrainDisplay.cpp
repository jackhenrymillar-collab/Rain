#include "GrainDisplay.h"

#include "../Parameters.h"

namespace rain::ui
{

using namespace juce;

namespace
{
    constexpr float kMaxGrainRate = 130.81278f; // Hz, Beads' fastest trigger rate
    constexpr float kLaneSeconds = 2.4f;        // how much history the density lane shows
    constexpr float kMaxVisualRate = 60.0f;     // beyond this the picture is just "solid"

    // Beads' attenuverter law (see dsp::RainEngine::attenuverterLaw).
    float randomAmount (float knob) { return dsp::RainEngine::attenuverterLaw (knob); }
}

GrainDisplay::GrainDisplay (AudioProcessorValueTreeState& s, const dsp::EngineMonitor& m)
    : state (s), monitor (m)
{
    setInterceptsMouseClicks (false, false);
    grains.reserve (256);
    triggers.reserve (512);
}

float GrainDisplay::param (const char* id) const
{
    return state.getRawParameterValue (id)->load();
}

float GrainDisplay::envelope (float p, float shape) noexcept
{
    // Same stage lengths and smoothing as beads::GrainEnvelope, normalised to 0..1.
    const float sustain = std::max (1.0f - 3.0f * shape, 0.0f);
    float s = (shape - 0.33333333f) * 1.5f;
    if (s < 0.0f)
        s = 0.0f;
    else if (s < 0.5f)
        s = s * s * s * 4.0f;
    const float attack = std::max (s, 0.006f * (1.0f + 3.0f * sustain));
    const float release = std::max ((1.0f - s) * (1.0f - sustain), 0.04f);
    const float total = attack + sustain + release;
    const float a = attack / total, su = sustain / total;
    const float smooth = s < 0.5f ? 2.0f * s : 2.0f - 2.0f * s;
    auto curve = [smooth] (float t) {
        // Linear blended with a raised cosine, as Beads does with "smoothness".
        return t * (1.0f - smooth) + smooth * (0.5f - 0.5f * std::cos (MathConstants<float>::pi * t));
    };
    if (p < a)
        return curve (p / a);
    if (p < a + su)
        return 1.0f;
    return curve (1.0f - (p - a - su) / std::max (1.0e-4f, 1.0f - a - su));
}

String GrainDisplay::shapeName (float s)
{
    return s < 0.15f ? "Square" : s < 0.5f ? "Decay" : s < 0.8f ? "Bell" : "Reverse";
}

String GrainDisplay::noteName (double beats)
{
    struct Named { double beats; const char* name; };
    static const Named names[] = {
        { 16.0, "4 bars" }, { 8.0, "2 bars" }, { 6.0, "1/2 bar ×3" }, { 4.0, "1 bar" }, { 3.0, "1/2." }, { 2.0, "1/2" },
        { 1.5, "1/4." }, { 4.0 / 3.0, "1/2T" }, { 1.0, "1/4" }, { 0.75, "1/8." }, { 2.0 / 3.0, "1/4T" },
        { 0.5, "1/8" }, { 0.375, "1/16." }, { 1.0 / 3.0, "1/8T" }, { 0.25, "1/16" }, { 1.0 / 6.0, "1/16T" }, { 0.125, "1/32" },
    };
    for (const auto& n : names)
        if (std::abs (n.beats - beats) < 1.0e-3)
            return n.name;
    return String (beats, 2) + " beats";
}

String GrainDisplay::syncDensityText (float density, int rateIndex)
{
    const auto step = static_cast<double> (dsp::kSyncDivisionBeats[static_cast<size_t> (jlimit (0, 5, rateIndex))]);
    const int divider = dsp::syncDivider (density);
    if (divider > 0)
        return "Every " + noteName (step * divider);
    const float p = std::min (1.0f, (density - 0.52f) * 2.15f);
    return "Chance " + String (roundToInt (100.0f * p * p)) + "% · " + noteName (step);
}

String GrainDisplay::syncTimeText (float t, float bufferSeconds, double bpm, int rateIndex)
{
    const auto step = static_cast<double> (dsp::kSyncDivisionBeats[static_cast<size_t> (jlimit (0, 5, rateIndex))]);
    const double steps = std::round (t * bufferSeconds / (step * 60.0 / bpm));
    if (steps < 0.5)
        return "Now";
    return String (static_cast<int> (steps)) + " × " + noteName (step);
}

void GrainDisplay::spawn()
{
    triggers.push_back (0.0f);
    if (grains.size() >= 200)
        return;

    auto& e = monitor.effective;
    // As the engine does it: a slowly drifting random value plus a little per grain.
    const auto lerpRandom = [this] (float base, float knob, int drift) {
        const float a = randomAmount (knob * 0.01f);
        const float blend = a > 0.0f ? a : std::min (1.0f, -3.0f * a * rng.nextFloat());
        const float d = monitor.randomDrift[static_cast<size_t> (drift)].load();
        const float r = 0.8f * (0.5f + 0.5f * d) + 0.2f * rng.nextFloat();
        return base + (r - base) * blend;
    };

    Grain g;
    // SIZE RANDOM works on Beads' own SIZE value, around the length the engine asks for.
    const float size = lerpRandom (dsp::RainEngine::beadsSizeForSeconds (monitor.grainSeconds.load()), param (ParamID::sizeRandom), 1);
    g.age = 0.0f;
    g.position = lerpRandom (e[static_cast<size_t> (dsp::ModTarget::time)].load(), param (ParamID::timeRandom), 0);
    g.seconds = dsp::RainEngine::grainSeconds (size);
    g.reverse = rng.nextFloat() * 100.0f < param (ParamID::reverse);
    g.shape = lerpRandom (e[static_cast<size_t> (dsp::ModTarget::shape)].load(), param (ParamID::shapeRandom), 2);
    g.life = jlimit (0.35f, 1.6f, g.seconds + 0.45f);
    g.lane = rng.nextFloat();
    g.amp = 0.55f + 0.45f * rng.nextFloat();
    grains.push_back (g);
}

void GrainDisplay::advance (float dt)
{
    for (auto& g : grains)
        g.age += dt;
    grains.erase (std::remove_if (grains.begin(), grains.end(), [] (const Grain& g) { return g.age > g.life; }), grains.end());
    for (auto& t : triggers)
        t += dt;
    triggers.erase (std::remove_if (triggers.begin(), triggers.end(), [] (float t) { return t > kLaneSeconds; }), triggers.end());

    const bool playing = param (ParamID::seedLock) > 0.5f || param (ParamID::seed) > 0.5f;
    const float d = monitor.spawnDensity.load();

    if (! playing)
    {
        lastStep = -1;
        untilNext = 0.0f;
    }
    else if (param (ParamID::sync) > 0.5f)
    {
        // Grains fire on the host clock, divided by DENSITY as Beads does.
        const int rateIndex = jlimit (0, 5, static_cast<int> (param (ParamID::syncRate)));
        const double step = dsp::kSyncDivisionBeats[static_cast<size_t> (rateIndex)];
        const auto index = static_cast<long long> (std::floor (monitor.beat.load() / step));
        if (lastStep < 0 || index < lastStep || index - lastStep > 8)
            lastStep = index;
        while (lastStep < index)
        {
            ++lastStep;
            const int divider = dsp::syncDivider (d);
            const float p = std::min (1.0f, (d - 0.52f) * 2.15f);
            const bool fire = divider > 0 ? (lastStep % divider) == 0 : rng.nextFloat() < p * p;
            if (fire)
                spawn();
        }
    }
    else if (std::abs (d - 0.5f) >= 0.02f)
    {
        lastStep = -1;
        const bool regular = d < 0.48f;
        const float rate = std::min (kMaxVisualRate, regular ? kMaxGrainRate * std::pow (2.0f, -d * 16.0f)
                                                             : 0.5f * kMaxGrainRate * std::pow (2.0f, -(1.0f - d) * 16.0f));
        untilNext = std::min (untilNext, 2.0f / rate);
        untilNext -= dt;
        int guard = 0;
        while (untilNext <= 0.0f && guard++ < 8)
        {
            spawn();
            untilNext += regular ? 1.0f / rate : -std::log (1.0f - 0.95f * rng.nextFloat()) / rate;
        }
    }

    repaint();
}

void GrainDisplay::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float W = bounds.getWidth(), H = bounds.getHeight();
    // No frame or labels: the buffer and grains float on the sky, fading into it at
    // the edges (older audio on the left drifts off like mist).
    const float left = 10.0f, right = W - 12.0f, top = 10.0f, bottom = H - 10.0f;
    const float plotW = right - left, mid = (top + bottom) * 0.5f;
    const float buffer = monitor.bufferSeconds.load();
    const bool frozen = param (ParamID::freeze) > 0.5f;
    auto& e = monitor.effective;
    const float time = e[static_cast<size_t> (dsp::ModTarget::time)].load();

    // 0 at the edges, 1 inside: a long fade on the left, a short one on the right.
    const auto edgeFade = [&] (float x) {
        const float a = jlimit (0.0f, 1.0f, (x - left) / (0.22f * plotW));
        const float b = jlimit (0.0f, 1.0f, (right - x) / (0.04f * plotW));
        return a * a * (3.0f - 2.0f * a) * b;
    };
    const Colour mist (176, 186, 246);

    // Buffer waveform: newest on the right, drawn as a smooth band of moonlit mist.
    {
        const int readings = jlimit (16, dsp::EngineMonitor::kNumLevels, roundToInt (buffer * dsp::EngineMonitor::kLevelRate));
        const int write = monitor.levelWrite.load (std::memory_order_acquire);
        const int columns = roundToInt (plotW / 4.0f);
        std::vector<float> amp (static_cast<size_t> (columns + 1), 0.0f);
        for (int c = 0; c <= columns; ++c)
        {
            // Column c covers readings from oldest (c = 0) to newest (c = columns).
            const int from = (c * readings) / (columns + 1);
            const int to = jmax (from + 1, ((c + 1) * readings) / (columns + 1));
            float peak = 0.0f;
            for (int r = from; r < to; ++r)
            {
                const int idx = ((write - readings + r) % dsp::EngineMonitor::kNumLevels + dsp::EngineMonitor::kNumLevels) % dsp::EngineMonitor::kNumLevels;
                peak = std::max (peak, monitor.levels[static_cast<size_t> (idx)].load (std::memory_order_relaxed));
            }
            amp[static_cast<size_t> (c)] = std::min (1.0f, std::sqrt (peak) * 1.1f);
        }
        // Light smoothing so the outline reads as a soft contour, not a bar chart.
        std::vector<float> smooth (amp.size());
        for (size_t c = 0; c < amp.size(); ++c)
        {
            const float a0 = amp[c > 0 ? c - 1 : c], a2 = amp[c + 1 < amp.size() ? c + 1 : c];
            smooth[c] = 0.25f * a0 + 0.5f * amp[c] + 0.25f * a2;
        }
        const float half = (bottom - top) * 0.5f;
        const auto xAt = [&] (size_t c) { return left + plotW * static_cast<float> (c) / static_cast<float> (columns); };
        Path upper, band;
        for (size_t c = 0; c < smooth.size(); ++c)
        {
            const Point<float> pt (xAt (c), mid - 1.5f - smooth[c] * (half - 2.0f));
            if (c == 0)
                upper.startNewSubPath (pt);
            else
            {
                const Point<float> prev (xAt (c - 1), mid - 1.5f - smooth[c - 1] * (half - 2.0f));
                upper.quadraticTo (prev, (prev + pt) * 0.5f);
            }
        }
        band = upper;
        for (size_t c = smooth.size(); c-- > 0;)
            band.lineTo (xAt (c), mid + 1.5f + smooth[c] * (half - 2.0f));
        band.closeSubPath();

        // A soft glow round the band: wide, faint strokes of the outline under it.
        for (int i = 3; i >= 1; --i)
        {
            const float a = (frozen ? 0.0425f : 0.034f);
            ColourGradient haze (mist.withAlpha (0.0f), left, 0.0f, mist.withAlpha (a), right, 0.0f, false);
            haze.addColour (0.22, mist.withAlpha (a));
            haze.addColour (0.96, mist.withAlpha (a));
            g.setGradientFill (haze);
            g.strokePath (band, PathStrokeType (static_cast<float> (i) * 5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        const float body = frozen ? 0.22f : 0.14f;
        ColourGradient fill (mist.withAlpha (0.0f), left, 0.0f, mist.withAlpha (body), right, 0.0f, false);
        fill.addColour (0.22, mist.withAlpha (body));
        fill.addColour (0.96, mist.withAlpha (body));
        g.setGradientFill (fill);
        g.fillPath (band);

        // A faint silver rim along the top edge only.
        ColourGradient rim (mist.withAlpha (0.0f), left, 0.0f, mist.withAlpha (0.32f), right, 0.0f, false);
        rim.addColour (0.22, mist.withAlpha (0.32f));
        g.setGradientFill (rim);
        g.strokePath (upper, PathStrokeType (0.8f));
    }

    // Write head: a thin line of light, fading at both ends.
    {
        const auto c = frozen ? Theme::gold : Theme::text;
        ColourGradient head (c.withAlpha (0.0f), 0.0f, top - 4.0f, c.withAlpha (0.0f), 0.0f, bottom + 4.0f, false);
        head.addColour (0.5, c.withAlpha (0.75f));
        g.setGradientFill (head);
        g.fillRect (right, top - 4.0f, 1.2f, bottom - top + 8.0f);
    }

    {
        // Grains: each its envelope shape, laid over the part of the buffer it plays,
        // drawn as a soft glow that fades towards its base, with a fine bright edge.
        // Fainter when there are many, so a dense cloud still reads as grains.
        const float crowd = jmin (1.0f, std::sqrt (14.0f / static_cast<float> (jmax<size_t> (1, grains.size()))));
        for (const auto& gr : grains)
        {
            const float width = jlimit (12.0f, plotW * 0.6f, gr.seconds / buffer * plotW);
            const float x0 = right - gr.position * plotW;
            const float xa = gr.reverse ? x0 - width : x0;
            const float fade = (1.0f - gr.age / gr.life) * crowd * edgeFade (xa + 0.5f * width);
            if (fade <= 0.01f)
                continue;
            const float h = 26.0f * gr.amp;
            const float base = top + 4.0f + h + gr.lane * jmax (0.0f, bottom - top - 8.0f - h);
            Path edge;
            edge.startNewSubPath (xa, base);
            for (int i = 0; i <= 24; ++i)
            {
                const float q = static_cast<float> (i) / 24.0f;
                // A reverse grain reads the buffer right to left, so its envelope
                // starts at its right-hand end.
                edge.lineTo (xa + q * width, base - envelope (gr.reverse ? 1.0f - q : q, gr.shape) * h);
            }
            Path body (edge);
            body.lineTo (xa + width, base);
            body.closeSubPath();
            // Reverse grains are drawn in cool moonlight blue, forward grains in gold.
            const auto colour = gr.reverse ? Colour (168, 192, 255) : Theme::dotGlow;
            ColourGradient glow (colour.withAlpha (1.1f * 0.3f * fade), 0.0f, base - h, colour.withAlpha (0.0f), 0.0f, base, false);
            g.setGradientFill (glow);
            g.fillPath (body);
            g.setColour (colour.withAlpha (jmin (1.0f, 1.1f * (0.25f + 0.6f * fade))));
            g.strokePath (edge, PathStrokeType (0.9f));
        }
    }

    // TIME marker, drawn last so it sits on top: a faint gold
    // dashed line with a soft glow, present but quieter than the grains.
    const float tx = right - time * plotW;
    {
        ColourGradient halo (Theme::gold.withAlpha (0.0f), tx - 5.0f, 0.0f, Theme::gold.withAlpha (0.0f), tx + 5.0f, 0.0f, false);
        halo.addColour (0.5, Theme::gold.withAlpha (0.07f));
        g.setGradientFill (halo);
        g.fillRect (tx - 5.0f, top - 4.0f, 10.0f, bottom - top + 8.0f);
        Path line;
        line.startNewSubPath (tx, top - 4.0f);
        line.lineTo (tx, bottom + 4.0f);
        Path dashed;
        const float dashes[] = { 3.0f, 4.0f };
        PathStrokeType (1.1f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (Theme::gold.withAlpha (0.45f));
        g.fillPath (dashed);
    }
}

} // namespace rain::ui
