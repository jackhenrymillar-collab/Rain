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

namespace
{
    void glowDot (Graphics& g, Point<float> c, float radius, float spread, Colour colour, float alpha)
    {
        for (int i = 4; i >= 1; --i)
        {
            const float rr = radius + static_cast<float> (i) * spread;
            g.setColour (colour.withAlpha (alpha * 0.1f));
            g.fillEllipse (Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c));
        }
        g.setColour (colour.withAlpha (alpha));
        g.fillEllipse (Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c));
    }

    // Halo: a flat glass disc ringed by a faint track and a gold arc of light up to
    // the setting, ending in a small four-point star. Centre-zero knobs light the arc
    // from the top, towards whichever side the setting is on.
    void drawHaloKnob (Graphics& g, Rectangle<float> b, bool small, float startAngle, float endAngle, float angle, bool bipolar)
    {
        const auto c = b.getCentre();
        const float ringR = b.getWidth() * 0.5f;
        const auto disc = b.reduced (small ? 5.0f : 8.0f);
        Path orb;
        orb.addEllipse (disc);
        DropShadow (Colours::black.withAlpha (0.4f), small ? 10 : 18, { 0, small ? 4 : 8 }).drawForPath (g, orb);
        ColourGradient face (Colour (0xff2a3184), disc.getCentreX(), disc.getY(), Colour (0xff141a4c), disc.getCentreX(), disc.getBottom(), false);
        g.setGradientFill (face);
        g.fillPath (orb);
        g.setColour (Colour (190, 196, 255).withAlpha (0.28f));
        g.drawEllipse (disc.reduced (0.5f), 1.0f);

        const float stroke = small ? 2.0f : 3.0f;
        Path track;
        track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
        g.setColour (Colour (190, 196, 255).withAlpha (0.16f));
        g.strokePath (track, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));
        const float from = bipolar ? 0.5f * (startAngle + endAngle) : startAngle;
        if (std::abs (angle - from) > 0.01f)
        {
            Path arc;
            arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, jmin (from, angle), jmax (from, angle), true);
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (Theme::gold.withAlpha (0.08f));
                g.strokePath (arc, PathStrokeType (stroke + static_cast<float> (i) * 3.0f, PathStrokeType::curved, PathStrokeType::rounded));
            }
            g.setColour (Theme::gold.withAlpha (0.9f));
            g.strokePath (arc, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));
        }
        // The star at the end of the arc.
        const Point<float> tip (c.x + ringR * std::sin (angle), c.y - ringR * std::cos (angle));
        const float s = small ? 5.0f : 7.5f;
        Path star;
        star.startNewSubPath (tip.x, tip.y - s);
        star.quadraticTo (tip, { tip.x + s, tip.y });
        star.quadraticTo (tip, { tip.x, tip.y + s });
        star.quadraticTo (tip, { tip.x - s, tip.y });
        star.quadraticTo (tip, { tip.x, tip.y - s });
        star.closeSubPath();
        glowDot (g, tip, small ? 1.5f : 2.0f, small ? 1.2f : 1.8f, Theme::dotGlow, 1.0f);
        g.setColour (Theme::moonLight);
        g.fillPath (star);
    }

}

void RainLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle, Slider& slider)
{
    const bool small = jmin (width, height) < 70;
    // Leave a margin round the orb for the modulation ring.
    const float size = static_cast<float> (jmin (width, height)) - (small ? 10.0f : 14.0f);
    const auto bounds = Rectangle<float> (size, size).withCentre (Rectangle<int> (x, y, width, height).toFloat().getCentre());

    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    drawHaloKnob (g, bounds, small, startAngle, endAngle, angle, slider.getProperties()["bipolar"]);
    drawModRing (g, slider, bounds, size, small, startAngle, endAngle, angle);
}

void RainLookAndFeel::drawModRing (Graphics& g, Slider& slider, Rectangle<float> bounds, float size, bool small,
                                   float startAngle, float endAngle, float angle)
{
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
    // Centre-zero controls (PITCH, the RANDOMs, modulation DEPTH) light their arc
    // from the top.
    const auto& range = param.getNormalisableRange();
    slider.getProperties().set ("bipolar", range.start < 0.0f && std::abs (range.start + range.end) < 1e-4f);
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
// Moon switch
// ---------------------------------------------------------------------------

MoonSwitch::MoonSwitch (AudioProcessorValueTreeState& state, const String& paramID)
    : attachment (*state.getParameter (paramID), [this] (float v) { on = v > 0.5f; })
{
    attachment.sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
    setTooltip ("Click the moon to bypass Rain (dry sound only)");
}

bool MoonSwitch::hitTest (int x, int y)
{
    // Only the disc itself is clickable.
    const auto c = getLocalBounds().toFloat().getCentre();
    const float r = static_cast<float> (getWidth()) * 0.5f;
    return Point<float> (static_cast<float> (x), static_cast<float> (y)).getDistanceFrom (c) <= r;
}

void MoonSwitch::mouseUp (const MouseEvent& e)
{
    if (e.mouseWasClicked() && hitTest (e.x, e.y))
        attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

LevelMeter::LevelMeter (float zoneLowDb, float zoneHighDb) : zoneLow (zoneLowDb), zoneHigh (zoneHighDb)
{
    setInterceptsMouseClicks (true, false);
}

void LevelMeter::push (float peak, float dt)
{
    const float db = peak > 0.0f ? jmax (kFloorDb, Decibels::gainToDecibels (peak)) : kFloorDb;
    // Rise at once, fall back at 24 dB a second; the peak marker holds for a second.
    levelDb = jmax (db, levelDb - 24.0f * dt);
    holdTime += dt;
    if (db >= holdDb)
    {
        holdDb = db;
        holdTime = 0.0f;
    }
    else if (holdTime > 1.0f)
    {
        holdDb = jmax (db, holdDb - 18.0f * dt);
    }
    repaint();
}

float LevelMeter::xFor (float db, Rectangle<float> area) const
{
    const float t = jlimit (0.0f, 1.0f, (db - kFloorDb) / -kFloorDb);
    return area.getX() + t * area.getWidth();
}

void LevelMeter::paint (Graphics& g)
{
    // A horizontal bar: -48 dB on the left, 0 dB on the right.
    const auto bounds = getLocalBounds().toFloat();
    const auto outer = bounds.withSizeKeepingCentre (bounds.getWidth(), 12.0f);
    const float radius = outer.getHeight() * 0.5f;
    g.setColour (Theme::glass);
    g.fillRoundedRectangle (outer, radius);

    const auto area = outer.reduced (1.5f);
    const float zLeft = xFor (zoneLow, area), zRight = xFor (zoneHigh, area);
    Path clip;
    clip.addRoundedRectangle (area, area.getHeight() * 0.5f);
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (clip);
        // The target band.
        g.setColour (Theme::gold.withAlpha (0.3f));
        g.fillRect (Rectangle<float> (zLeft, area.getY(), zRight - zLeft, area.getHeight()));
        // The level, deep blue to white, coral past the band.
        if (levelDb > kFloorDb)
        {
            const float right = xFor (levelDb, area);
            ColourGradient fill (Colour (110, 128, 225), area.getX(), 0.0f, Colour (240, 243, 255), zRight, 0.0f, false);
            g.setGradientFill (fill);
            g.fillRect (Rectangle<float> (area.getX(), area.getY(), jmin (right, zRight) - area.getX(), area.getHeight()));
            if (right > zRight)
            {
                g.setColour (Colour (255, 128, 112));
                g.fillRect (Rectangle<float> (zRight, area.getY(), right - zRight, area.getHeight()));
            }
        }
        // Tint the band over the level too, so it reads while the meter is full.
        g.setColour (Theme::gold.withAlpha (0.35f));
        g.fillRect (Rectangle<float> (zLeft, area.getY(), zRight - zLeft, area.getHeight()));
        if (holdDb > kFloorDb + 1.0f)
        {
            g.setColour (holdDb > zoneHigh ? Colour (255, 128, 112) : Theme::text);
            g.fillRect (Rectangle<float> (xFor (holdDb, area) - 1.0f, area.getY(), 1.5f, area.getHeight()));
        }
    }
    // Band edges, standing just proud of the bar.
    g.setColour (Theme::gold);
    g.fillRect (Rectangle<float> (zRight - 0.75f, outer.getY() - 3.0f, 1.5f, outer.getHeight() + 6.0f));
    g.setColour (Theme::gold.withAlpha (0.6f));
    g.fillRect (Rectangle<float> (zLeft - 0.5f, outer.getY() - 2.0f, 1.0f, outer.getHeight() + 4.0f));
    g.setColour (Theme::hairline);
    g.drawRoundedRectangle (outer.reduced (0.5f), radius, 1.0f);

}

} // namespace rain::ui
