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

float GrainDisplay::grainSeconds (float duration) noexcept
{
    const bool reverse = duration < 0.3333333f;
    const float clamped = reverse ? duration * 3.0f * (2.0f - duration * 3.0f)
                                  : 1.0f - 1.5f * (duration - 0.3333333f);
    const float shortness = std::min (1.0f, 1.025f * clamped);
    return std::pow (2.0f, shortness * -9.0f) * 4.0f;
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

namespace
{
    // Beads' clocked delay ratios, selected by DENSITY.
    constexpr float kDelayRatios[] = {
        1.0f / 16, 1.0f / 16, 1.0f / 8, 1.0f / 8, 1.0f / 4, 1.0f / 4, 1.0f / 2, 1.0f / 2, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f / 2, 1.0f / 3, 1.0f / 4, 1.0f / 6, 1.0f / 8, 1.0f / 10, 1.0f / 12, 1.0f / 16, 1.0f / 16, 1.0f / 16,
    };

    float clockRatio (float density)
    {
        return kDelayRatios[jlimit (0, 21, static_cast<int> (density * 20.0f + 0.5f))];
    }
}

float GrainDisplay::delaySeconds (float density, float buffer, bool sync, double bpm, int rateIndex)
{
    if (sync)
    {
        const auto step = dsp::kSyncDivisionBeats[static_cast<size_t> (jlimit (0, 5, rateIndex))];
        return static_cast<float> (step * 60.0 / bpm) * clockRatio (density);
    }
    return std::max (68.0f / 48000.0f, buffer * std::pow (2.0f, -std::abs (density - 0.5f) * 18.0f));
}

String GrainDisplay::delayText (float density, float buffer, bool sync, double bpm, int rateIndex)
{
    if (sync)
    {
        const auto step = static_cast<double> (dsp::kSyncDivisionBeats[static_cast<size_t> (jlimit (0, 5, rateIndex))]);
        return noteName (step * clockRatio (density));
    }
    const float s = delaySeconds (density, buffer, false, bpm, rateIndex);
    return s < 1.0f ? String (roundToInt (s * 1000.0f)) + " ms" : String (s, 2) + " s";
}

void GrainDisplay::spawn()
{
    triggers.push_back (0.0f);
    if (grains.size() >= 200)
        return;

    auto& e = monitor.effective;
    const auto lerpRandom = [this] (float base, float knob) {
        const float a = randomAmount (knob * 0.01f);
        const float blend = a > 0.0f ? a : std::min (1.0f, -3.0f * a * rng.nextFloat());
        return base + (rng.nextFloat() - base) * blend;
    };

    Grain g;
    const float size = lerpRandom (e[static_cast<size_t> (dsp::ModTarget::size)].load(), param (ParamID::sizeRandom));
    g.age = 0.0f;
    g.position = lerpRandom (e[static_cast<size_t> (dsp::ModTarget::time)].load(), param (ParamID::timeRandom));
    g.seconds = grainSeconds (size);
    g.reverse = size < 0.3333333f;
    g.shape = lerpRandom (e[static_cast<size_t> (dsp::ModTarget::shape)].load(), param (ParamID::shapeRandom));
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

    const bool delayMode = param (ParamID::mode) > 0.5f;
    const bool playing = param (ParamID::seedLock) > 0.5f || param (ParamID::seed) > 0.5f;
    const float d = monitor.effective[static_cast<size_t> (dsp::ModTarget::density)].load();

    if (delayMode || ! playing)
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
    g.setColour (Colour (18, 24, 66).withAlpha (0.55f));
    g.fillRoundedRectangle (bounds.reduced (0.5f), 12.0f);
    g.setColour (Theme::hairline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

    const float left = 14.0f, right = W - 15.0f, top = 34.0f, bottom = H - 44.0f, laneY = H - 16.0f;
    const float plotW = right - left, mid = (top + bottom) * 0.5f;
    const float buffer = monitor.bufferSeconds.load();
    const bool frozen = param (ParamID::freeze) > 0.5f;
    const bool delayMode = param (ParamID::mode) > 0.5f;
    const bool sync = param (ParamID::sync) > 0.5f;
    const int rateIndex = jlimit (0, 5, static_cast<int> (param (ParamID::syncRate)));
    auto& e = monitor.effective;
    const float time = e[static_cast<size_t> (dsp::ModTarget::time)].load();
    const float shape = e[static_cast<size_t> (dsp::ModTarget::shape)].load();
    const float density = e[static_cast<size_t> (dsp::ModTarget::density)].load();

    // Buffer waveform: newest on the right.
    {
        const int readings = jlimit (16, dsp::EngineMonitor::kNumLevels, roundToInt (buffer * dsp::EngineMonitor::kLevelRate));
        const int write = monitor.levelWrite.load (std::memory_order_acquire);
        const int columns = roundToInt (plotW / 3.0f);
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
        Path wave;
        const float half = (bottom - top) * 0.5f;
        wave.startNewSubPath (left, mid);
        for (int c = 0; c <= columns; ++c)
            wave.lineTo (left + plotW * static_cast<float> (c) / static_cast<float> (columns), mid - 2.0f - amp[static_cast<size_t> (c)] * (half - 2.0f));
        for (int c = columns; c >= 0; --c)
            wave.lineTo (left + plotW * static_cast<float> (c) / static_cast<float> (columns), mid + 2.0f + amp[static_cast<size_t> (c)] * (half - 2.0f));
        wave.closeSubPath();
        g.setColour (Colour (150, 160, 230).withAlpha (frozen ? 0.32f : 0.22f));
        g.fillPath (wave);
    }

    // Write head.
    g.setColour ((frozen ? Theme::gold : Theme::text).withAlpha (0.7f));
    g.fillRect (right, top - 6.0f, 1.5f, bottom - top + 12.0f);

    // Labels.
    g.setFont (Fonts::caps (13.0f, 0.2f));
    g.setColour (Theme::textMuted);
    const String bufferText = "BUFFER  " + String (roundToInt (buffer)) + " s";
    g.drawText (bufferText, Rectangle<float> (left, 8.0f, 200.0f, 16.0f), Justification::centredLeft, false);
    g.drawText ("NOW", Rectangle<float> (right - 100.0f, 8.0f, 101.0f, 16.0f), Justification::centredRight, false);
    if (frozen)
    {
        g.setColour (Theme::gold);
        g.drawText ("FROZEN", Rectangle<float> (left + 120.0f, 8.0f, 120.0f, 16.0f), Justification::centredLeft, false);
    }

    const double bpm = monitor.bpm.load();
    const float delay = delaySeconds (density, buffer, sync, bpm, rateIndex);

    // TIME marker, or in delay mode the delay time.
    const float tx = right - (delayMode ? jmin (1.0f, delay / buffer) : time) * plotW;
    {
        Path line;
        line.startNewSubPath (tx, top - 6.0f);
        line.lineTo (tx, bottom + 4.0f);
        Path dashed;
        const float dashes[] = { 3.0f, 3.0f };
        PathStrokeType (1.5f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (Theme::gold.withAlpha (0.95f));
        g.fillPath (dashed);
        g.setFont (Fonts::caps (12.0f, 0.2f));
        // Keep the label clear of BUFFER and NOW.
        const float labelX = jlimit (left + 150.0f, right - 80.0f, tx);
        g.drawText (delayMode ? "DELAY" : "TIME", Rectangle<float> (labelX - 40.0f, top - 22.0f, 80.0f, 14.0f), Justification::centred, false);
    }

    if (delayMode)
    {
        // Echo taps: one per repeat, fading with FEEDBACK.
        const float fb = e[static_cast<size_t> (dsp::ModTarget::feedback)].load();
        const float spacing = delay / buffer;
        float a = 0.9f;
        for (int k = 1; k <= 24 && spacing * plotW > 2.0f; ++k)
        {
            const float x = right - static_cast<float> (k) * spacing * plotW;
            if (x < left)
                break;
            const float h = (bottom - top) * 0.42f * a;
            g.setColour (Theme::dotGlow.withAlpha (0.15f + 0.75f * a));
            g.fillRoundedRectangle (x - 1.5f, mid - h, 3.0f, 2.0f * h, 1.5f);
            a *= std::max (fb, 0.08f);
            if (a < 0.03f)
                break;
        }
    }
    else
    {
        // Grains: each its envelope shape, laid over the part of the buffer it plays.
        // Fainter when there are many, so a dense cloud still reads as grains.
        const float crowd = jmin (1.0f, std::sqrt (14.0f / static_cast<float> (jmax<size_t> (1, grains.size()))));
        for (const auto& gr : grains)
        {
            const float fade = (1.0f - gr.age / gr.life) * crowd;
            const float width = jlimit (14.0f, plotW * 0.6f, gr.seconds / buffer * plotW);
            const float x0 = right - gr.position * plotW;
            const float xa = gr.reverse ? x0 - width : x0;
            const float h = 30.0f * gr.amp;
            const float base = top + 6.0f + h + gr.lane * jmax (0.0f, bottom - top - 12.0f - h);
            Path p;
            p.startNewSubPath (xa, base);
            for (int i = 0; i <= 24; ++i)
            {
                const float q = static_cast<float> (i) / 24.0f;
                p.lineTo (xa + q * width, base - envelope (q, gr.shape) * h);
            }
            p.lineTo (xa + width, base);
            p.closeSubPath();
            g.setColour (Theme::dotGlow.withAlpha (0.08f + 0.2f * fade));
            g.fillPath (p);
            g.setColour (Theme::dotGlow.withAlpha (0.3f + 0.55f * fade));
            g.strokePath (p, PathStrokeType (1.1f));
        }
    }

    // Density lane.
    g.setColour (Colour (190, 196, 255).withAlpha (0.18f));
    g.fillRect (left, laneY - 0.5f, plotW, 1.0f);
    for (const float age : triggers)
    {
        const float x = right - age / kLaneSeconds * plotW;
        g.setColour (Theme::dotGlow.withAlpha (jmax (0.0f, 0.9f - age / 3.0f)));
        g.fillEllipse (x - 2.2f, laneY - 2.2f, 4.4f, 4.4f);
    }

    String readout;
    if (delayMode)
    {
        readout = "Echo every " + delayText (density, buffer, sync, bpm, rateIndex);
    }
    else if (! (param (ParamID::seedLock) > 0.5f || param (ParamID::seed) > 0.5f))
        readout = "Hold SEED to play grains";
    else if (sync)
        readout = syncDensityText (density, rateIndex) + "  ·  " + shapeName (shape);
    else
        readout = state.getParameter (ParamID::density)->getText (state.getParameter (ParamID::density)->convertTo0to1 (density * 100.0f), 32)
                + "  ·  " + shapeName (shape);

    g.setFont (Fonts::caps (13.0f, 0.2f));
    g.setColour (Theme::textMuted);
    g.drawText (delayMode ? "ECHOES" : sync ? "CLOCK" : "DENSITY", Rectangle<float> (left, laneY - 22.0f, 160.0f, 14.0f), Justification::centredLeft, false);
    g.setFont (Fonts::sans (16.0f));
    g.setColour (Theme::text);
    g.drawText (readout, Rectangle<float> (left + 120.0f, laneY - 24.0f, plotW - 120.0f, 16.0f), Justification::centredRight, false);
}

} // namespace rain::ui
