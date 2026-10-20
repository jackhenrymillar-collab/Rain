#include "Components.h"

namespace rain::ui
{

using namespace juce;

// ---------------------------------------------------------------------------
// Look and feel
// ---------------------------------------------------------------------------

RainLookAndFeel::RainLookAndFeel()
{
    setColour (Label::textColourId, Theme::text);
    setColour (TextButton::textColourOffId, Theme::text);
    setColour (TextButton::textColourOnId, Theme::text);
}

void RainLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle, Slider&)
{
    const float size = static_cast<float> (jmin (width, height)) - 2.0f;
    const auto bounds = Rectangle<float> (size, size).withCentre (Rectangle<int> (x, y, width, height).toFloat().getCentre());
    const bool small = size < 60.0f;

    Path orb;
    orb.addEllipse (bounds);

    // Soft drop shadow.
    DropShadow (Colours::black.withAlpha (small ? 0.4f : 0.45f), small ? 12 : 22, { 0, small ? 5 : 10 }).drawForPath (g, orb);

    // Glassy navy sphere, lit from the upper left.
    ColourGradient body (Colour (small ? 0xff5560ae : 0xff5d66b8), bounds.getX() + bounds.getWidth() * 0.36f, bounds.getY() + bounds.getHeight() * 0.30f,
                         Colour (small ? 0xff181d52 : 0xff1a1f57), bounds.getX() + bounds.getWidth() * 0.36f, bounds.getBottom() + bounds.getHeight() * 0.05f,
                         true);
    body.addColour (0.48, Colour (small ? 0xff2a3180 : 0xff2f3787));
    g.setGradientFill (body);
    g.fillPath (orb);

    // Darker underside.
    ColourGradient under (Colours::transparentBlack, bounds.getCentreX(), bounds.getCentreY(),
                          Colours::black.withAlpha (0.3f), bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill (under);
    g.fillPath (orb);

    g.setColour (Colour (190, 196, 255).withAlpha (small ? 0.3f : 0.34f));
    g.drawEllipse (bounds.reduced (0.5f), 1.0f);

    // Glowing indicator dot.
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float r = size * 0.34f;
    const Point<float> dot (bounds.getCentreX() + r * std::sin (angle), bounds.getCentreY() - r * std::cos (angle));
    const float dotRadius = small ? 2.5f : 3.5f;
    for (int i = 4; i >= 1; --i)
    {
        const float rr = dotRadius + static_cast<float> (i) * (small ? 1.2f : 1.8f);
        g.setColour (Theme::dotGlow.withAlpha (0.10f));
        g.fillEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (dot));
    }
    g.setColour (Theme::dotGlow);
    g.fillEllipse (Rectangle<float> (dotRadius * 2.0f, dotRadius * 2.0f).withCentre (dot));
}

void RainLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    const bool pill = b.getProperties()["pill"];
    const bool on = b.getToggleState() || down;
    auto bounds = b.getLocalBounds().toFloat().reduced (1.0f);
    const float radius = pill ? bounds.getHeight() * 0.5f : 8.0f;

    if (on)
    {
        // Gold glow around the active chip.
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (Theme::gold.withAlpha (0.07f));
            g.drawRoundedRectangle (bounds.expanded (static_cast<float> (i) * 2.0f), radius + static_cast<float> (i) * 2.0f, 3.0f);
        }
    }

    g.setColour (on ? Theme::gold.withAlpha (0.15f) : Theme::glass.withMultipliedAlpha (highlighted ? 1.3f : 1.0f));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (on ? Theme::gold : Theme::hairline.withMultipliedAlpha (highlighted ? 1.5f : 1.0f));
    g.drawRoundedRectangle (bounds, radius, 1.0f);
}

void RainLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    const bool pill = b.getProperties()["pill"];
    g.setColour (Theme::text.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.setFont (pill ? Fonts::caps (19.0f, 0.2f) : Fonts::sans (20.0f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred, false);
}

Font RainLookAndFeel::getLabelFont (Label& l)
{
    return l.getFont();
}

// ---------------------------------------------------------------------------
// Knob
// ---------------------------------------------------------------------------

OrbKnob::OrbKnob (AudioProcessorValueTreeState& state, const String& paramID, const String& name, Size s)
    : param (*state.getParameter (paramID)),
      size (s),
      attachment (state, paramID, slider)
{
    slider.setSliderStyle (Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setDoubleClickReturnValue (true, param.convertFrom0to1 (param.getDefaultValue()));
    slider.setMouseDragSensitivity (220);
    slider.setTitle (name);
    slider.onValueChange = [this] { updateValueText(); };
    addAndMakeVisible (slider);

    nameLabel.setText (name, dontSendNotification);
    nameLabel.setInterceptsMouseClicks (false, false);
    valueLabel.setInterceptsMouseClicks (false, false);

    if (size == Size::large)
    {
        nameLabel.setFont (Fonts::caps (17.5f, 0.2f));
        nameLabel.setColour (Label::textColourId, Theme::textSoft);
        nameLabel.setJustificationType (Justification::centred);
        valueLabel.setFont (Fonts::sans (20.0f));
        valueLabel.setJustificationType (Justification::centred);
    }
    else
    {
        nameLabel.setFont (Fonts::caps (15.0f, 0.18f));
        nameLabel.setColour (Label::textColourId, Theme::textMuted);
        nameLabel.setJustificationType (Justification::bottomLeft);
        valueLabel.setFont (Fonts::sans (17.5f));
        valueLabel.setJustificationType (Justification::topLeft);
    }
    nameLabel.setBorderSize ({});
    valueLabel.setBorderSize ({});
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (valueLabel);
    updateValueText();
}

void OrbKnob::updateValueText()
{
    auto text = param.getCurrentValueAsText();
    const auto label = param.getLabel();
    if (label.isNotEmpty())
        text << (label == "%" ? "" : " ") << label;
    // Bipolar knobs show their sign.
    if (param.getNormalisableRange().start < 0.0f && param.getValue() > 0.5f + 1.0e-4f && ! text.startsWithChar ('+'))
        text = "+" + text;
    valueLabel.setText (text, dontSendNotification);
}

void OrbKnob::resized()
{
    auto b = getLocalBounds();
    if (size == Size::large)
    {
        slider.setBounds (b.removeFromTop (88).withSizeKeepingCentre (88, 88));
        b.removeFromTop (12);
        nameLabel.setBounds (b.removeFromTop (22));
        b.removeFromTop (4);
        valueLabel.setBounds (b.removeFromTop (24));
    }
    else
    {
        // Small knob: orb on the left, name and value stacked beside it.
        const int content = 44 + 10 + 80;
        b = b.withSizeKeepingCentre (jmin (content, b.getWidth()), 44);
        slider.setBounds (b.removeFromLeft (44));
        b.removeFromLeft (10);
        nameLabel.setBounds (b.removeFromTop (22));
        valueLabel.setBounds (b);
    }
}

// ---------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------

SkyBackground::SkyBackground()
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
}

void SkyBackground::resized()
{
    renderStatic();
}

void SkyBackground::advance (float seconds)
{
    time += seconds;
    repaint();
}

namespace
{
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
}

void SkyBackground::renderStatic()
{
    const float scale = 2.0f; // render at 2x so it stays sharp when the window grows
    const int w = Theme::designWidth, h = Theme::designHeight;
    sky = Image (Image::RGB, static_cast<int> (w * scale), static_cast<int> (h * scale), true);
    Graphics g (sky);
    g.addTransform (AffineTransform::scale (scale));

    ColourGradient bg (Theme::skyTop, 0, 0, Theme::skyBottom, 0, static_cast<float> (h), false);
    bg.addColour (0.48, Theme::skyMid);
    g.setGradientFill (bg);
    g.fillRect (0, 0, w, h);

    // Stars.
    Random rng (11);
    for (int i = 0; i < 46; ++i)
    {
        const float sx = rng.nextFloat() * static_cast<float> (w);
        const float sy = rng.nextFloat() * static_cast<float> (h) * 0.7f;
        const float sz = rng.nextFloat() > 0.8f ? 2.0f : 1.2f;
        g.setColour (Colours::white.withAlpha (0.3f + rng.nextFloat() * 0.6f));
        g.fillEllipse (sx, sy, sz, sz);
    }

    // Moon with a halo, two faint craters.
    const Rectangle<float> moon (900.0f, 62.0f, 150.0f, 150.0f);
    softEllipse (g, moon.expanded (70.0f), Colour (246, 236, 198).withAlpha (0.22f));
    ColourGradient moonFill (Theme::moonLight, moon.getX() + 60.0f, moon.getY() + 57.0f,
                             Colour (0xffd9cb9a), moon.getRight(), moon.getBottom(), true);
    moonFill.addColour (0.55, Colour (0xfff1e6be));
    g.setGradientFill (moonFill);
    g.fillEllipse (moon);
    g.setColour (Colour (170, 155, 110).withAlpha (0.28f));
    g.fillEllipse (948.0f, 92.0f, 26.0f, 26.0f);
    g.setColour (Colour (170, 155, 110).withAlpha (0.25f));
    g.fillEllipse (1004.0f, 120.0f, 16.0f, 16.0f);

    // The rain cloud drifting across the moon.
    softEllipse (g, { 820.0f, 140.0f, 300.0f, 96.0f }, Colour (100, 108, 165).withAlpha (0.8f));
    softEllipse (g, { 770.0f, 158.0f, 270.0f, 86.0f }, Colour (80, 88, 150).withAlpha (0.88f));
    softEllipse (g, { 910.0f, 176.0f, 240.0f, 74.0f }, Colour (70, 78, 140).withAlpha (0.95f));

    // Low mist along the bottom.
    softEllipse (g, { -120.0f, 640.0f, 640.0f, 220.0f }, Colour (160, 168, 225).withAlpha (0.2f));
    softEllipse (g, { 340.0f, 650.0f, 700.0f, 230.0f }, Colour (150, 158, 220).withAlpha (0.16f));
    softEllipse (g, { 760.0f, 640.0f, 520.0f, 210.0f }, Colour (170, 176, 230).withAlpha (0.18f));

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
    g.drawText ("QUALITY", Rectangle<float> (48.0f, 118.0f, 300.0f, 20.0f), Justification::centredLeft, false);
    g.drawText ("GRAINS", Rectangle<float> (66.0f, 330.0f, 300.0f, 18.0f), Justification::centredLeft, false);
    g.drawText ("TEXTURE", Rectangle<float> (66.0f + 4.0f * 135.25f, 330.0f, 300.0f, 18.0f), Justification::centredLeft, false);
}

void SkyBackground::paint (Graphics& g)
{
    g.drawImage (sky, getLocalBounds().toFloat());

    // Falling rain, slanted like the mockup.
    const float sx = static_cast<float> (getWidth()) / static_cast<float> (Theme::designWidth);
    const float sy = static_cast<float> (getHeight()) / static_cast<float> (Theme::designHeight);
    const float travel = static_cast<float> (Theme::designHeight) + 140.0f;
    for (const auto& s : streaks)
    {
        const float p = std::fmod (s.phase + time * s.speed, 1.0f);
        const float y = -80.0f + p * travel;
        const float x = s.x * static_cast<float> (Theme::designWidth) - p * 150.0f;
        g.setColour (Colour (210, 222, 255).withAlpha (s.alpha));
        g.drawLine (x * sx, y * sy, (x - s.length * 0.17f) * sx, (y + s.length) * sy, 1.0f);
    }
}

} // namespace rain::ui
