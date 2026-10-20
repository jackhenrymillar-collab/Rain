#include "Sky.h"
#include "Theme.h"

#include <cmath>

namespace rain::ui
{

using namespace juce;

namespace
{
    constexpr float kW = static_cast<float> (Theme::designWidth);
    constexpr float kH = static_cast<float> (Theme::designHeight);
    constexpr float kPi = MathConstants<float>::pi;

    // A soft cloud or glow: an ellipse whose colour fades to nothing at its edge.
    void softEllipse (Graphics& g, Rectangle<float> r, Colour c)
    {
        ColourGradient grad (c, r.getCentreX(), r.getCentreY(), c.withAlpha (0.0f), r.getRight(), r.getCentreY(), true);
        grad.addColour (0.45, c.withMultipliedAlpha (0.8f));
        g.setGradientFill (grad);
        // Draw a circle scaled into the ellipse, so the radial fade follows its shape.
        g.saveState();
        g.addTransform (AffineTransform::scale (1.0f, r.getHeight() / r.getWidth(), r.getCentreX(), r.getCentreY()));
        g.fillEllipse (r.withSizeKeepingCentre (r.getWidth(), r.getWidth()));
        g.restoreState();
    }

    void softCircle (Graphics& g, Point<float> c, float rx, float ry, Colour colour)
    {
        softEllipse (g, Rectangle<float> (rx * 2.0f, ry * 2.0f).withCentre (c), colour);
    }

    // Moon craters: x, y and radius as fractions of the moon's radius.
    constexpr float kCraters[][3] = {
        { -0.18f, -0.38f, 0.17f }, { 0.24f, -0.05f, 0.11f }, { -0.42f, 0.12f, 0.14f }, { 0.05f, 0.42f, 0.13f },
        { -0.1f, 0.05f, 0.08f },   { 0.45f, 0.32f, 0.09f },  { -0.5f, -0.2f, 0.07f },  { 0.3f, -0.5f, 0.07f },
        { -0.28f, 0.55f, 0.06f },  { 0.58f, -0.12f, 0.06f },
    };

    // Draws the craters on a moon of radius r: a filled bowl and a rim lit from one side.
    // Angles are clockwise from 12 o'clock.
    void craters (Graphics& g, Point<float> c, float r, Colour bowl, Colour rim, float rimFrom, float rimTo)
    {
        const float rimWidth = std::max (0.8f, r * 0.025f);
        for (const auto& k : kCraters)
        {
            const Point<float> p (c.x + k[0] * r, c.y + k[1] * r);
            const float cr = k[2] * r;
            g.setColour (bowl);
            g.fillEllipse (Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (p));
            Path arc;
            arc.addCentredArc (p.x + cr * 0.12f, p.y + cr * 0.12f, cr * 0.92f, cr * 0.92f, 0.0f, rimFrom, rimTo, true);
            g.setColour (rim);
            g.strokePath (arc, PathStrokeType (rimWidth, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    float smooth (float amount, float targetValue, float k) { return amount + k * (targetValue - amount); }
}

SkyBackground::SkyBackground() : random (19)
{
    setOpaque (true);
    setInterceptsMouseClicks (false, false);

    Random rng (7);
    for (auto& s : streaks)
    {
        s.x = rng.nextFloat() * 1.12f;
        s.phase = rng.nextFloat();
        s.length = 18.0f + rng.nextFloat() * 26.0f;
        s.alpha = 0.12f + rng.nextFloat() * 0.35f;
        s.speed = 1.0f / (0.9f + rng.nextFloat() * 0.8f);
    }

    Random sr (11);
    for (auto& s : stars)
    {
        s.x = sr.nextFloat() * kW;
        s.y = sr.nextFloat() * kH * 0.85f;
        s.radius = sr.nextFloat() > 0.9f ? 1.8f : (sr.nextFloat() > 0.5f ? 1.15f : 0.75f);
        s.alpha = 0.3f + sr.nextFloat() * 0.65f;
        s.twinkle = sr.nextFloat() * 6.28f;
        s.speed = 0.6f + sr.nextFloat() * 1.6f;
    }

    // A small soft glow, stamped behind stars as FEEDBACK rises.
    starGlow = Image (Image::ARGB, 32, 32, true);
    {
        Graphics g (starGlow);
        ColourGradient grad (Colour (220, 228, 255).withAlpha (0.55f), 16.0f, 16.0f, Colour (190, 205, 255).withAlpha (0.0f), 32.0f, 16.0f, true);
        grad.addColour (0.25, Colour (190, 205, 255).withAlpha (0.18f));
        g.setGradientFill (grad);
        g.fillRect (0, 0, 32, 32);
    }

    aurora = Image (Image::ARGB, 240, 120, true);
}

void SkyBackground::resized()
{
    renderStatic();
}

void SkyBackground::setWeather (Weather w) noexcept
{
    target.rain = jlimit (0.0f, 1.0f, w.rain);
    target.night = jlimit (0.0f, 1.0f, w.night);
    target.bloom = jlimit (0.0f, 1.0f, w.bloom);
    target.storm = jlimit (0.0f, 1.0f, w.storm);
}

void SkyBackground::advance (float seconds)
{
    time += seconds;
    // Ease towards the knobs over about half a second, so the weather drifts.
    const float k = 1.0f - std::exp (-seconds / 0.5f);
    amount.rain = smooth (amount.rain, target.rain, k);
    amount.night = smooth (amount.night, target.night, k);
    amount.bloom = smooth (amount.bloom, target.bloom, k);
    amount.storm = smooth (amount.storm, target.storm, k);
    const float ke = 1.0f - std::exp (-seconds / 0.25f);
    eclipseAmount += ke * (eclipseTarget - eclipseAmount);
    if (std::abs (eclipseAmount - eclipseTarget) < 0.002f)
        eclipseAmount = eclipseTarget;

    // Lightning: rare at low FEEDBACK, about one strike every three or four seconds at full,
    // with the odd quick double strike.
    if (target.storm > 0.03f)
    {
        untilStrike -= seconds;
        if (untilStrike <= 0.0f)
        {
            strikeLightning();
            const float rate = 0.08f + 0.26f * std::pow (target.storm, 1.4f); // strikes per second
            untilStrike = 0.8f - std::log (1.0f - random.nextFloat() * 0.95f) / rate;
            if (random.nextFloat() < target.storm * 0.25f)
                pendingStrike = 0.09f + random.nextFloat() * 0.12f;
        }
    }
    if (pendingStrike > 0.0f)
    {
        pendingStrike -= seconds;
        if (pendingStrike <= 0.0f)
        {
            pendingStrike = -1.0f;
            strikeLightning();
        }
    }
    for (auto& b : bolts)
        b.age += seconds;
    bolts.erase (std::remove_if (bolts.begin(), bolts.end(), [] (const Bolt& b) { return b.age > 5.0f; }), bolts.end());
    flash *= std::exp (-seconds / 0.12f);

    repaint();
}

void SkyBackground::addBranch (std::vector<Branch>& out, Point<float> from, Point<float> to,
                               float spread, int depth, float width, int& budget)
{
    // Midpoint displacement: split every segment and nudge the midpoint sideways.
    std::vector<Point<float>> pts { from, to };
    const float length = from.getDistanceFrom (to);
    const int passes = jlimit (3, 7, roundToInt (std::log2 (std::max (1.0f, length / 3.0f))));
    for (int pass = 0; pass < passes; ++pass)
    {
        for (size_t i = pts.size() - 1; i > 0; --i)
        {
            const auto a = pts[i - 1], b = pts[i];
            const float len = std::max (0.001f, a.getDistanceFrom (b));
            const Point<float> normal (-(b.y - a.y) / len, (b.x - a.x) / len);
            const float off = (random.nextFloat() - 0.5f) * spread * len * 0.55f;
            pts.insert (pts.begin() + static_cast<std::ptrdiff_t> (i), (a + b) * 0.5f + normal * off);
        }
    }
    Path path;
    path.startNewSubPath (pts.front());
    for (size_t i = 1; i < pts.size(); ++i)
        path.lineTo (pts[i]);
    out.push_back ({ path, width });

    if (depth <= 0 || budget <= 0)
        return;
    // Forks split off along the channel, and split again into ever finer twigs.
    const int forks = depth >= 3 ? 2 + random.nextInt (2) : 2 + random.nextInt (4);
    const float dir = std::atan2 (to.x - from.x, to.y - from.y);
    for (int f = 0; f < forks && budget > 0; ++f)
    {
        --budget;
        const auto p = pts[static_cast<size_t> (static_cast<float> (pts.size() - 1) * (0.12f + random.nextFloat() * 0.8f))];
        const float ang = dir + (random.nextBool() ? -1.0f : 1.0f) * (0.35f + random.nextFloat() * 0.75f);
        const float len = length * (0.22f + random.nextFloat() * 0.33f);
        addBranch (out, p, { p.x + std::sin (ang) * len, p.y + std::cos (ang) * len }, spread * 1.05f, depth - 1,
                   std::max (0.35f, width * 0.48f), budget);
    }
}

void SkyBackground::strikeLightning()
{
    const auto moonCentre = moonBounds().getCentre();
    float x = kW * (0.1f + random.nextFloat() * 0.75f);
    if (std::abs (x - moonCentre.x) < 150.0f)
        x = moonCentre.x - 200.0f;

    std::vector<Branch> branches;
    int budget = 40 + roundToInt (amount.storm * 50.0f);
    addBranch (branches, { x, -10.0f }, { x + (random.nextFloat() - 0.5f) * kW * 0.18f, kH * (0.4f + random.nextFloat() * 0.25f) },
               0.85f, 4, 3.0f, budget);

    Bolt bolt;
    bolt.flicker = random.nextBool() ? 0.09f + random.nextFloat() * 0.06f : -1.0f;
    for (const auto& b : branches)
        bolt.bounds = bolt.bounds.isEmpty() ? b.path.getBounds() : bolt.bounds.getUnion (b.path.getBounds());
    bolt.bounds = bolt.bounds.expanded (30.0f).getSmallestIntegerContainer().toFloat();

    // Render the bolt once: a wide afterglow layer, and the bright channel that only lasts a moment.
    const float scale = 1.5f;
    const int iw = std::max (1, roundToInt (bolt.bounds.getWidth() * scale));
    const int ih = std::max (1, roundToInt (bolt.bounds.getHeight() * scale));
    bolt.glow = Image (Image::ARGB, iw, ih, true);
    bolt.core = Image (Image::ARGB, iw, ih, true);
    const auto place = AffineTransform::translation (-bolt.bounds.getX(), -bolt.bounds.getY()).scaled (scale);
    const auto stroke = [] (float w) { return PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded); };
    {
        Graphics g (bolt.glow);
        g.addTransform (place);
        for (const auto& b : branches)
        {
            const float fine = std::min (1.0f, b.width / 2.0f);
            g.setColour (Colour (110, 100, 220).withAlpha (0.025f + 0.04f * fine));
            g.strokePath (b.path, stroke (b.width * 14.0f + 6.0f));
            g.setColour (Colour (130, 120, 235).withAlpha (0.03f + 0.06f * fine));
            g.strokePath (b.path, stroke (b.width * 7.0f + 3.0f));
            g.setColour (Colour (170, 170, 255).withAlpha (0.1f + 0.18f * fine));
            g.strokePath (b.path, stroke (b.width * 3.0f + 1.0f));
            g.setColour (Colour (240, 242, 255).withAlpha (0.14f));
            g.strokePath (b.path, stroke (b.width));
        }
    }
    {
        Graphics g (bolt.core);
        g.addTransform (place);
        for (const auto& b : branches)
        {
            g.setColour (Colour (170, 170, 255).withAlpha (0.22f));
            g.strokePath (b.path, stroke (b.width * 3.0f + 1.0f));
            g.setColour (Colour (240, 242, 255).withAlpha (0.95f));
            g.strokePath (b.path, stroke (b.width));
        }
    }
    bolts.push_back (std::move (bolt));
    flash = std::min (1.0f, flash + 0.5f + 0.3f * amount.storm);
}

void SkyBackground::renderStatic()
{
    const float scale = 2.0f; // render at 2x so it stays sharp when the window grows
    const int w = Theme::designWidth, h = Theme::designHeight;
    const auto make = [scale, w, h] (Image::PixelFormat f) { return Image (f, static_cast<int> (w * scale), static_cast<int> (h * scale), true); };
    sky = make (Image::RGB);
    mist = make (Image::ARGB);
    nightSky = make (Image::ARGB);
    decor = make (Image::ARGB);

    {
        Graphics g (sky);
        g.addTransform (AffineTransform::scale (scale));
        ColourGradient bg (Theme::skyTop, 0, 0, Theme::skyBottom, 0, kH, false);
        bg.addColour (0.48, Theme::skyMid);
        g.setGradientFill (bg);
        g.fillRect (0, 0, w, h);
    }

    {
        // Low mist along the bottom, which CLOUD clears away.
        Graphics g (mist);
        g.addTransform (AffineTransform::scale (scale));
        softEllipse (g, { -120.0f, kH - 120.0f, 640.0f, 220.0f }, Colour (160, 168, 225).withAlpha (0.2f));
        softEllipse (g, { 340.0f, kH - 110.0f, 700.0f, 230.0f }, Colour (150, 158, 220).withAlpha (0.16f));
        softEllipse (g, { 760.0f, kH - 120.0f, 520.0f, 210.0f }, Colour (170, 176, 230).withAlpha (0.18f));
    }

    {
        // Vast nebulae: big colour washes plus a drifting band of smaller wisps across the sky.
        Graphics n (nightSky);
        n.addTransform (AffineTransform::scale (scale));
        const float sh = 600.0f; // the open sky above the knob panels
        softCircle (n, { kW * 0.28f, sh * 0.3f }, 620.0f, 250.0f, Colour (110, 60, 190).withAlpha (0.2f));
        softCircle (n, { kW * 0.5f, sh * 0.42f }, 480.0f, 190.0f, Colour (190, 80, 160).withAlpha (0.12f));
        softCircle (n, { kW * 0.78f, sh * 0.22f }, 560.0f, 220.0f, Colour (50, 130, 190).withAlpha (0.15f));
        softCircle (n, { kW * 0.1f, sh * 0.62f }, 420.0f, 170.0f, Colour (70, 110, 210).withAlpha (0.12f));
        const Colour wisps[] = { Colour (150, 80, 210), Colour (210, 100, 170), Colour (80, 160, 210), Colour (120, 100, 230) };
        Random rn (23);
        for (int i = 0; i < 70; ++i)
        {
            const float u = rn.nextFloat();
            const float x = kW * (-0.05f + u * 1.1f);
            const float y = sh * (0.62f - 0.5f * u + 0.12f * std::sin (u * 7.0f) + (rn.nextFloat() - 0.5f) * 0.14f);
            const auto c = wisps[rn.nextInt (4)];
            const float rx = 60.0f + rn.nextFloat() * 140.0f, ry = 24.0f + rn.nextFloat() * 50.0f;
            softCircle (n, { x, y }, rx, ry, c.withAlpha (0.06f + rn.nextFloat() * 0.08f));
        }
    }

    // Everything from here on sits above the sky.
    Graphics g (decor);
    g.addTransform (AffineTransform::scale (scale));

    // Glass panels behind the lower rows.
    for (const auto& panel : { Rectangle<float> (40.0f, 604.0f, 300.0f, 130.0f), Rectangle<float> (358.0f, 604.0f, 506.0f, 130.0f),
                               Rectangle<float> (882.0f, 604.0f, 248.0f, 130.0f) })
    {
        g.setColour (Colour (18, 24, 66).withAlpha (0.38f));
        g.fillRoundedRectangle (panel, 12.0f);
        g.setColour (Theme::hairline.withMultipliedAlpha (0.6f));
        g.drawRoundedRectangle (panel, 12.0f, 1.0f);
    }

    // Wordmark: raindrop and RAIN.
    auto drop = Drawable::parseSVGPath ("M11 1 C11 1 2 13 2 19.5 A9 9 0 0 0 20 19.5 C20 13 11 1 11 1 Z");
    drop.applyTransform (AffineTransform::translation (48.0f, 30.0f));
    g.setColour (Colour (0xfff4eccd));
    g.fillPath (drop);
    g.setColour (Theme::text);
    g.setFont (Fonts::serif (46.0f, true).withExtraKerningFactor (0.2f));
    g.drawText ("RAIN", Rectangle<float> (84.0f, 30.0f, 240.0f, 56.0f), Justification::centredLeft, false);

    // Section headings above the knob rows.
    g.setColour (Theme::textMuted);
    g.setFont (Fonts::caps (16.0f, 0.26f));
    g.drawText ("MODE", Rectangle<float> (48.0f, 118.0f, 300.0f, 20.0f), Justification::centredLeft, false);
    g.drawText ("GRAINS", Rectangle<float> (66.0f, 326.0f, 300.0f, 18.0f), Justification::centredLeft, false);
    g.drawText ("TEXTURE", Rectangle<float> (66.0f + 4.0f * 135.25f, 326.0f, 300.0f, 18.0f), Justification::centredLeft, false);
    g.drawText ("GRAIN CHARACTER", Rectangle<float> (56.0f, 580.0f, 280.0f, 18.0f), Justification::centredLeft, false);
    g.drawText ("MOTION", Rectangle<float> (374.0f, 580.0f, 300.0f, 18.0f), Justification::centredLeft, false);
    g.drawText ("TEMPO & SCALE", Rectangle<float> (898.0f, 580.0f, 230.0f, 18.0f), Justification::centredLeft, false);
}

void SkyBackground::drawStars (Graphics& g, float, float)
{
    // DENSITY adds some stars and CLOUD many more. FEEDBACK makes them brighter, a touch
    // larger, and gives them a soft glow.
    const float storm = amount.storm;
    const auto count = std::min (stars.size(), static_cast<size_t> (60.0f + amount.rain * 200.0f + amount.night * 420.0f));
    for (size_t i = 0; i < count; ++i)
    {
        const auto& s = stars[i];
        const float tw = 0.75f + 0.25f * std::sin (time * s.speed + s.twinkle);
        const float r = s.radius * (1.0f + 0.35f * storm);
        const float glow = storm * (s.radius > 1.0f ? 1.0f : 0.45f);
        if (glow > 0.02f)
        {
            g.setOpacity (glow * tw * s.alpha);
            g.drawImage (starGlow, Rectangle<float> (r * 14.0f, r * 14.0f).withCentre ({ s.x, s.y }));
            g.setOpacity (1.0f);
        }
        g.setColour (Colour (236, 240, 255).withAlpha (std::min (1.0f, s.alpha * tw * (1.0f + 0.7f * storm))));
        g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ s.x, s.y }));
    }
}

void SkyBackground::drawAurora (Graphics& g, float, float, float strength)
{
    // Slow curtains of green and violet light along the bottom of the sky, computed into a
    // small image and drawn stretched, so they stay soft.
    const auto area = Rectangle<float> (0.0f, kH * 0.4f, kW, kH * 0.6f);
    const int aw = aurora.getWidth(), ah = aurora.getHeight();
    Image::BitmapData px (aurora, Image::BitmapData::writeOnly);
    const Colour violet (150, 90, 220), green (80, 230, 170), pale (140, 255, 200), teal (60, 190, 150);
    for (int i = 0; i < aw; ++i)
    {
        const float u = (static_cast<float> (i) + 0.5f) / static_cast<float> (aw);
        const float base = kH * (0.9f + 0.035f * std::sin (u * 9.0f + time * 0.35f) + 0.02f * std::sin (u * 23.0f - time * 0.6f));
        const float height = kH * (0.3f + 0.16f * (0.5f + 0.5f * std::sin (u * 5.3f - time * 0.22f)));
        // Broad folds that drift slowly, with no fine striping, so the curtains stay soft.
        const float folds = 0.5f + 0.5f * std::sin (u * 17.0f + time * 0.25f + std::sin (u * 4.0f + time * 0.12f) * 1.5f);
        const float glow = (0.5f + 0.5f * folds) * (0.3f + 0.7f * (0.5f + 0.5f * std::sin (u * 3.1f - time * 0.17f)));
        const float a = 2.4f * strength * glow;
        const float tail = kH * 0.09f, top = base - height, f = height / (height + tail);
        for (int j = 0; j < ah; ++j)
        {
            const float y = area.getY() + (static_cast<float> (j) + 0.5f) / static_cast<float> (ah) * area.getHeight();
            const float v = (y - top) / (height + tail);
            Colour c = Colours::transparentBlack;
            if (v > 0.0f && v < 1.0f)
            {
                // Gradient stops: violet tops, green body, a pale lower edge, then a faint tail.
                const auto mix = [] (Colour c0, float a0, Colour c1, float a1, float t) {
                    return c0.interpolatedWith (c1, t).withAlpha (jlimit (0.0f, 1.0f, a0 + (a1 - a0) * t));
                };
                if (v < 0.35f * f)       c = mix (violet, 0.0f, violet, 0.1f * a, v / (0.35f * f));
                else if (v < 0.75f * f)  c = mix (violet, 0.1f * a, green, 0.2f * a, (v - 0.35f * f) / (0.4f * f));
                else if (v < f)          c = mix (green, 0.2f * a, pale, 0.26f * a, (v - 0.75f * f) / (0.25f * f));
                else if (v < f + (1.0f - f) * 0.4f) c = mix (pale, 0.26f * a, teal, 0.08f * a, (v - f) / ((1.0f - f) * 0.4f));
                else                     c = mix (teal, 0.08f * a, teal, 0.0f, (v - f - (1.0f - f) * 0.4f) / ((1.0f - f) * 0.6f));
            }
            px.setPixelColour (i, j, c);
        }
    }
    // drawImage uses the current fill's alpha, so reset it: otherwise the aurora takes the
    // opacity of whichever star was drawn last and flickers with its twinkle.
    g.setOpacity (1.0f);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (aurora, area);
}

void SkyBackground::drawMoon (Graphics& g, float, float)
{
    const auto bounds = moonBounds();
    const auto c = bounds.getCentre();
    const float r = bounds.getWidth() * 0.5f;
    const float e = eclipseAmount, night = amount.night;

    // CLOUD wanes the moon towards a crescent, and bypass takes it to a thin one.
    const float phase = std::max (night * 0.8f, e);
    const float litShare = 1.0f - 0.75f * phase;
    const float lit = (1.0f - 0.85f * e) * (0.45f + 0.55f * litShare);

    // SHAPE blooms the glow, and at the top of its range a wide 22-degree halo ring appears.
    const float bloom = amount.bloom;
    const float glowR = r * (1.6f + 2.6f * bloom * bloom);
    softCircle (g, c, glowR, glowR, Colour (246, 236, 198).withAlpha ((0.16f + 0.3f * bloom) * lit));
    const float ring = std::max (0.0f, (bloom - 0.55f) / 0.45f);
    if (ring > 0.0f)
    {
        const float outer = r * 4.2f * 1.12f;
        const auto at = [] (float s) { return (0.82 + 0.3 * s) / 1.12; };
        ColourGradient grad (Colour (255, 200, 170).withAlpha (0.0f), c.x, c.y, Colour (190, 205, 255).withAlpha (0.0f), c.x + outer, c.y, true);
        grad.addColour (at (0.0f), Colour (255, 200, 170).withAlpha (0.0f));
        grad.addColour (at (0.42f), Colour (255, 214, 180).withAlpha (0.16f * ring * lit));
        grad.addColour (at (0.55f), Colour (240, 236, 220).withAlpha (0.24f * ring * lit));
        grad.addColour (at (0.7f), Colour (190, 205, 255).withAlpha (0.14f * ring * lit));
        g.setGradientFill (grad);
        g.fillEllipse (Rectangle<float> (outer * 2.0f, outer * 2.0f).withCentre (c));
    }

    // Earth's shadow: a disc offset to the left, leaving the right side lit.
    const float shift = r * 2.0f * (1.0f - 0.78f * phase);
    const auto shadow = Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ c.x - shift, c.y - 0.1f * r });

    if (phase > 0.002f)
    {
        // The dark side in earthshine: a slate disc with its craters and a faint rim.
        const float ea = std::min (1.0f, phase * 1.6f);
        ColourGradient dark (Colour (0xff2c3360).withAlpha (ea), c.x + 0.3f * r, c.y - 0.2f * r,
                             Colour (0xff171b3a).withAlpha (ea), c.x + 0.3f * r + r * 1.2f, c.y, true);
        g.setGradientFill (dark);
        g.fillEllipse (bounds);
        craters (g, c, r, Colour (14, 17, 40).withAlpha (0.55f * ea), Colour (150, 165, 225).withAlpha (0.4f * ea),
                 2.4f * kPi, 3.4f * kPi);
        g.setColour (Colour (150, 165, 220).withAlpha (0.35f * ea));
        const float rim = std::max (1.0f, r * 0.03f);
        g.drawEllipse (bounds.reduced (rim * 0.5f), rim);
    }

    // The lit face, minus the shadow.
    g.saveState();
    Path disc;
    disc.addEllipse (bounds);
    g.reduceClipRegion (disc);
    if (phase > 0.002f)
    {
        Path litPart;
        litPart.addEllipse (bounds);
        litPart.addEllipse (shadow);
        litPart.setUsingNonZeroWinding (false);
        g.reduceClipRegion (litPart);
    }
    ColourGradient face (Theme::moonLight, c.x - 0.2f * r, c.y - 0.24f * r, Colour (0xffd9cb9a), c.x + r * 0.8f, c.y + r * 0.76f, true);
    face.addColour (0.55, Colour (0xfff1e6be));
    g.setGradientFill (face);
    g.fillEllipse (bounds);
    craters (g, c, r, Colour (170, 155, 110).withAlpha (0.3f), Colour (255, 250, 230).withAlpha (0.35f), 1.4f * kPi, 2.4f * kPi);
    g.restoreState();

    // The rain cloud by the moon thins away as CLOUD rises.
    const float wisp = (1.0f - 0.5f * e) * (1.0f - night);
    if (wisp > 0.005f)
    {
        softEllipse (g, { 950.0f, 78.0f, 210.0f, 64.0f }, Colour (100, 108, 165).withAlpha (0.8f * wisp));
        softEllipse (g, { 1010.0f, 92.0f, 170.0f, 54.0f }, Colour (80, 88, 150).withAlpha (0.9f * wisp));
    }
}

void SkyBackground::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float sx = bounds.getWidth() / kW;
    const float sy = bounds.getHeight() / kH;
    const auto design = Rectangle<float> (kW, kH);
    g.addTransform (AffineTransform::scale (sx, sy));

    const float night = amount.night;
    g.drawImage (sky, design);
    if (night < 0.995f)
    {
        g.setOpacity (1.0f - night);
        g.drawImage (mist, design);
        g.setOpacity (1.0f);
    }
    if (night > 0.005f)
    {
        // CLOUD takes the sky much darker and brings out vast nebulae.
        ColourGradient dark (Colour (1, 2, 10).withAlpha (0.88f * night), 0, 0, Colour (2, 3, 14).withAlpha (0.78f * night), 0, kH, false);
        g.setGradientFill (dark);
        g.fillRect (design);
        g.setOpacity (night);
        g.drawImage (nightSky, design);
        g.setOpacity (1.0f);
    }

    drawStars (g, sx, sy);
    const float auroraAmount = std::max (0.0f, (night - 0.2f) / 0.8f);
    if (auroraAmount > 0.003f)
        drawAurora (g, sx, sy, auroraAmount);

    if (eclipseAmount > 0.0f)
    {
        // Bypassed: a very dark sky.
        g.setColour (Colour (1, 2, 10).withAlpha (0.72f * eclipseAmount));
        g.fillRect (design);
    }
    if (flash > 0.01f)
    {
        // A lightning flash lights the whole sky for a moment.
        g.setColour (Colour (190, 200, 255).withAlpha (0.22f * flash * (1.0f - 0.7f * eclipseAmount)));
        g.fillRect (design);
    }

    drawMoon (g, sx, sy);

    // Lightning: a bright channel for a moment, then an afterglow that fades over a few seconds.
    for (const auto& b : bolts)
    {
        float core = std::exp (-b.age / 0.09f);
        if (b.flicker > 0.0f && b.age > b.flicker)
            core = std::max (core, 0.8f * std::exp (-(b.age - b.flicker) / 0.08f));
        const float after = std::exp (-b.age / 1.3f) * (1.0f - std::exp (-b.age / 0.05f));
        if (after > 0.003f)
        {
            g.setOpacity (std::min (1.0f, after));
            g.drawImage (b.glow, b.bounds);
        }
        if (core > 0.003f)
        {
            g.setOpacity (std::min (1.0f, core));
            g.drawImage (b.core, b.bounds);
        }
        g.setOpacity (1.0f);
    }

    g.setOpacity (1.0f);
    g.drawImage (decor, design);

    // Falling rain, slanted: a drizzle with DENSITY at noon, a downpour at either end.
    // It fades a little as CLOUD rises.
    const float travel = kH + 140.0f;
    const float fade = 1.0f - 0.3f * night;
    const auto count = static_cast<size_t> (20.0f + amount.rain * static_cast<float> (streaks.size() - 20));
    for (size_t i = 0; i < count; ++i)
    {
        const auto& s = streaks[i];
        const float p = std::fmod (s.phase + time * s.speed, 1.0f);
        const float y = -80.0f + p * travel;
        const float x = s.x * kW - p * 150.0f;
        g.setColour (Colour (210, 222, 255).withAlpha (s.alpha * fade));
        g.drawLine (x, y, x - s.length * 0.17f, y + s.length, 1.0f / std::max (0.5f, sx));
    }
}

} // namespace rain::ui
