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
    setColour (ComboBox::textColourId, Theme::text);
    setColour (ComboBox::backgroundColourId, Colours::transparentBlack);
    setColour (ComboBox::outlineColourId, Colours::transparentBlack);
    setColour (PopupMenu::backgroundColourId, Colour (0xff141a46));
    setColour (PopupMenu::textColourId, Theme::text);
    setColour (PopupMenu::headerTextColourId, Theme::gold);
    setColour (PopupMenu::highlightedBackgroundColourId, Theme::gold.withAlpha (0.18f));
    setColour (PopupMenu::highlightedTextColourId, Theme::text);
    setColour (AlertWindow::backgroundColourId, Colour (0xff141a46));
    setColour (AlertWindow::textColourId, Theme::text);
    setColour (AlertWindow::outlineColourId, Theme::hairline);
    setColour (TextEditor::backgroundColourId, Colour (0xff0e1336));
    setColour (TextEditor::textColourId, Theme::text);
    setColour (TextEditor::outlineColourId, Theme::hairline);
    setColour (TextEditor::focusedOutlineColourId, Theme::gold);
    setColour (TextButton::buttonColourId, Theme::glass);
}

void RainLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle, Slider& slider)
{
    const bool small = jmin (width, height) < 70;
    // Leave a margin round the orb for the modulation ring.
    const float size = static_cast<float> (jmin (width, height)) - (small ? 10.0f : 14.0f);
    const auto bounds = Rectangle<float> (size, size).withCentre (Rectangle<int> (x, y, width, height).toFloat().getCentre());

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

    // Modulation ring: a gold arc from the knob's setting to where it has been moved.
    const auto mod = slider.getProperties()["mod"];
    if (! mod.isVoid() && static_cast<double> (mod) >= 0.0)
    {
        const float modPos = jlimit (0.0f, 1.0f, static_cast<float> (static_cast<double> (mod)));
        const float modAngle = startAngle + modPos * (endAngle - startAngle);
        const float ringR = size * 0.5f + (small ? 3.5f : 5.0f);
        const auto c = bounds.getCentre();
        Path arc;
        arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, jmin (angle, modAngle), jmax (angle, modAngle), true);
        g.setColour (Theme::gold.withAlpha (0.85f));
        g.strokePath (arc, PathStrokeType (small ? 2.0f : 2.5f, PathStrokeType::curved, PathStrokeType::rounded));
        const Point<float> tip (c.x + ringR * std::sin (modAngle), c.y - ringR * std::cos (modAngle));
        g.setColour (Theme::gold);
        g.fillEllipse (Rectangle<float> (small ? 5.0f : 6.0f, small ? 5.0f : 6.0f).withCentre (tip));
    }
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
    const bool compact = b.getProperties()["compact"];
    g.setColour (Theme::text.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.setFont (pill ? Fonts::caps (18.0f, 0.2f) : Fonts::sans (compact ? 18.0f : 20.0f));
    // Shrink rather than clip if a label is ever too long for its button.
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 0), Justification::centred, 1, 0.8f);
}

Font RainLookAndFeel::getLabelFont (Label& l)
{
    return l.getFont();
}

void RainLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox& box)
{
    auto bounds = Rectangle<float> (static_cast<float> (width), static_cast<float> (height)).reduced (1.0f);
    const bool hot = box.isMouseOver (true) || box.isPopupActive();
    g.setColour (Theme::glass.withMultipliedAlpha (hot ? 1.3f : 1.0f));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (Theme::hairline.withMultipliedAlpha (hot ? 1.5f : 1.0f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    // Chevron.
    const float cx = bounds.getRight() - 16.0f, cy = bounds.getCentreY();
    Path chevron;
    chevron.startNewSubPath (cx - 4.5f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 4.5f, cy - 2.0f);
    g.setColour (Theme::textMuted.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
    g.strokePath (chevron, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
}

Font RainLookAndFeel::getComboBoxFont (ComboBox&)
{
    return Fonts::sans (18.0f);
}

void RainLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    label.setBounds (12, 0, box.getWidth() - 34, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (Justification::centredLeft);
}

Font RainLookAndFeel::getPopupMenuFont()
{
    return Fonts::sans (17.0f);
}

void RainLookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    g.fillAll (Colour (0xff141a46));
    g.setColour (Theme::hairline);
    g.drawRect (0, 0, width, height);
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
    // The orb's soft shadow is costly to draw; cache it, since the falling rain
    // behind repaints the panel every frame.
    slider.setBufferedToImage (true);
    addAndMakeVisible (slider);

    nameLabel.setText (name, dontSendNotification);
    nameLabel.setInterceptsMouseClicks (false, false);
    valueLabel.setInterceptsMouseClicks (false, false);

    if (size == Size::medium)
    {
        nameLabel.setFont (Fonts::caps (14.5f, 0.18f));
        nameLabel.setColour (Label::textColourId, Theme::textSoft);
        nameLabel.setJustificationType (Justification::centred);
        valueLabel.setFont (Fonts::sans (17.0f));
        valueLabel.setJustificationType (Justification::centred);
    }
    else if (size == Size::large)
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

void OrbKnob::setModulated (float normalised)
{
    const auto current = slider.getProperties()["mod"];
    const double v = normalised < 0.0f ? -1.0 : static_cast<double> (normalised);
    if (current.isVoid() || std::abs (static_cast<double> (current) - v) > 1.0e-4)
    {
        slider.getProperties().set ("mod", v);
        slider.repaint();
    }
}

void OrbKnob::setValueOverride (const String& text)
{
    if (text == valueOverride)
        return;
    valueOverride = text;
    updateValueText();
}

void OrbKnob::updateValueText()
{
    if (valueOverride.isNotEmpty())
    {
        valueLabel.setText (valueOverride, dontSendNotification);
        return;
    }
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
        slider.setBounds (b.removeFromTop (100).withSizeKeepingCentre (100, 100));
        b.removeFromTop (2);
        nameLabel.setBounds (b.removeFromTop (22));
        b.removeFromTop (4);
        valueLabel.setBounds (b.removeFromTop (24));
    }
    else if (size == Size::medium)
    {
        // Medium knob: a smaller orb with name and value underneath, like the large ones.
        slider.setBounds (b.removeFromTop (62).withSizeKeepingCentre (62, 62));
        b.removeFromTop (4);
        nameLabel.setBounds (b.removeFromTop (18));
        b.removeFromTop (2);
        valueLabel.setBounds (b.removeFromTop (22));
    }
    else
    {
        // Small knob: orb on the left, name and value stacked beside it.
        const int content = 52 + 6 + 76;
        b = b.withSizeKeepingCentre (jmin (content, b.getWidth()), 52);
        slider.setBounds (b.removeFromLeft (52));
        b.removeFromLeft (6);
        b.reduce (0, 4);
        nameLabel.setBounds (b.removeFromTop (22));
        valueLabel.setBounds (b);
    }
}

// ---------------------------------------------------------------------------
// Drop-down and modulator trace
// ---------------------------------------------------------------------------

namespace
{
    // Fills a combo box with a choice parameter's options before attaching to it.
    ComboBox& filled (ComboBox& box, AudioProcessorValueTreeState& state, const String& id)
    {
        if (auto* choice = dynamic_cast<AudioParameterChoice*> (state.getParameter (id)))
            box.addItemList (choice->choices, 1);
        return box;
    }
}

ChoiceBox::ChoiceBox (AudioProcessorValueTreeState& state, const String& paramID)
    : attachment (state, paramID, filled (box, state, paramID))
{
    box.setTitle (state.getParameter (paramID)->getName (64));
    addAndMakeVisible (box);
}

ModScope::ModScope()
{
    setInterceptsMouseClicks (false, false);
}

void ModScope::push (float value)
{
    history[head] = value;
    head = (head + 1) % history.size();
    repaint();
}

void ModScope::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Theme::hairline.withMultipliedAlpha (0.5f));
    g.drawHorizontalLine (roundToInt (b.getCentreY()), b.getX(), b.getRight());

    Path trace;
    const auto n = history.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        const float v = history[(head + i) % n];
        const float x = b.getX() + b.getWidth() * static_cast<float> (i) / static_cast<float> (n - 1);
        const float y = b.getCentreY() - v * b.getHeight() * 0.45f;
        if (i == 0)
            trace.startNewSubPath (x, y);
        else
            trace.lineTo (x, y);
    }
    g.setColour (Theme::gold.withAlpha (0.8f));
    g.strokePath (trace, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    const float last = history[(head + n - 1) % n];
    g.setColour (Theme::dotGlow);
    g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre ({ b.getRight(), b.getCentreY() - last * b.getHeight() * 0.45f }));
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

void SkyBackground::setWeather (float rain, float night) noexcept
{
    rainTarget = jlimit (0.0f, 1.0f, rain);
    nightTarget = jlimit (0.0f, 1.0f, night);
}

void SkyBackground::advance (float seconds)
{
    time += seconds;
    // Ease towards the knobs over about half a second, so the weather drifts.
    const float k = 1.0f - std::exp (-seconds / 0.5f);
    rainAmount += k * (rainTarget - rainAmount);
    nightAmount += k * (nightTarget - nightAmount);
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
    nightSky = Image (Image::ARGB, static_cast<int> (w * scale), static_cast<int> (h * scale), true);
    decor = Image (Image::ARGB, static_cast<int> (w * scale), static_cast<int> (h * scale), true);

    // The deep-night layer: darker sky, nebulae and a denser star field.
    {
        Graphics n (nightSky);
        n.addTransform (AffineTransform::scale (scale));
        ColourGradient dark (Colour (2, 3, 16).withAlpha (0.62f), 0, 0, Colour (4, 6, 26).withAlpha (0.35f), 0, static_cast<float> (h), false);
        n.setGradientFill (dark);
        n.fillRect (0, 0, w, h);
        // Nebulae: soft violet, rose and teal clouds across the upper sky.
        softEllipse (n, { 120.0f, 60.0f, 520.0f, 200.0f }, Colour (120, 70, 190).withAlpha (0.22f));
        softEllipse (n, { 330.0f, 120.0f, 380.0f, 150.0f }, Colour (190, 90, 160).withAlpha (0.14f));
        softEllipse (n, { 640.0f, 20.0f, 460.0f, 180.0f }, Colour (60, 140, 190).withAlpha (0.16f));
        softEllipse (n, { 760.0f, 250.0f, 420.0f, 170.0f }, Colour (110, 80, 200).withAlpha (0.14f));
        softEllipse (n, { -60.0f, 280.0f, 420.0f, 160.0f }, Colour (70, 120, 200).withAlpha (0.12f));
        Random nr (29);
        for (int i = 0; i < 220; ++i)
        {
            const float x = nr.nextFloat() * static_cast<float> (w);
            const float y = nr.nextFloat() * static_cast<float> (h) * 0.85f;
            const float r = nr.nextFloat() > 0.92f ? 1.9f : (nr.nextFloat() > 0.5f ? 1.2f : 0.8f);
            n.setColour (Colour (230, 236, 255).withAlpha (0.25f + nr.nextFloat() * 0.65f));
            n.fillEllipse (x, y, r, r);
        }
    }

    {
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

    // Low mist along the bottom.
    const auto bottom = static_cast<float> (h);
    softEllipse (g, { -120.0f, bottom - 120.0f, 640.0f, 220.0f }, Colour (160, 168, 225).withAlpha (0.2f));
    softEllipse (g, { 340.0f, bottom - 110.0f, 700.0f, 230.0f }, Colour (150, 158, 220).withAlpha (0.16f));
    softEllipse (g, { 760.0f, bottom - 120.0f, 520.0f, 210.0f }, Colour (170, 176, 230).withAlpha (0.18f));

    }

    // Everything from here on sits above the night layer.
    Graphics g (decor);
    g.addTransform (AffineTransform::scale (scale));

    // Moon with a halo, two faint craters, tucked into the top-right corner.
    const Rectangle<float> moon (1004.0f, 22.0f, 100.0f, 100.0f);
    softEllipse (g, moon.expanded (50.0f), Colour (246, 236, 198).withAlpha (0.2f));
    ColourGradient moonFill (Theme::moonLight, moon.getX() + 40.0f, moon.getY() + 38.0f,
                             Colour (0xffd9cb9a), moon.getRight(), moon.getBottom(), true);
    moonFill.addColour (0.55, Colour (0xfff1e6be));
    g.setGradientFill (moonFill);
    g.fillEllipse (moon);
    g.setColour (Colour (170, 155, 110).withAlpha (0.28f));
    g.fillEllipse (1036.0f, 42.0f, 17.0f, 17.0f);
    g.setColour (Colour (170, 155, 110).withAlpha (0.25f));
    g.fillEllipse (1073.0f, 61.0f, 11.0f, 11.0f);

    // The rain cloud drifting across the moon.
    softEllipse (g, { 950.0f, 78.0f, 210.0f, 64.0f }, Colour (100, 108, 165).withAlpha (0.8f));
    softEllipse (g, { 1010.0f, 92.0f, 170.0f, 54.0f }, Colour (80, 88, 150).withAlpha (0.9f));

    // Glass panels behind the motion and tempo rows.
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

void SkyBackground::paint (Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.drawImage (sky, bounds);
    if (nightAmount > 0.005f)
    {
        g.setOpacity (nightAmount);
        g.drawImage (nightSky, bounds);
        g.setOpacity (1.0f);
    }
    g.drawImage (decor, bounds);

    // Falling rain, slanted like the mockup: a drizzle with DENSITY at noon, a
    // downpour at either end.
    const float sx = static_cast<float> (getWidth()) / static_cast<float> (Theme::designWidth);
    const float sy = static_cast<float> (getHeight()) / static_cast<float> (Theme::designHeight);
    const float travel = static_cast<float> (Theme::designHeight) + 140.0f;
    const auto count = static_cast<size_t> (20.0f + rainAmount * static_cast<float> (streaks.size() - 20));
    for (size_t i = 0; i < count; ++i)
    {
        const auto& s = streaks[i];
        const float p = std::fmod (s.phase + time * s.speed, 1.0f);
        const float y = -80.0f + p * travel;
        const float x = s.x * static_cast<float> (Theme::designWidth) - p * 150.0f;
        g.setColour (Colour (210, 222, 255).withAlpha (s.alpha));
        g.drawLine (x * sx, y * sy, (x - s.length * 0.17f) * sx, (y + s.length) * sy, 1.0f);
    }
}

} // namespace rain::ui
