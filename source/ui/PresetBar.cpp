#include "PresetBar.h"

namespace rain::ui
{

using namespace juce;

namespace
{
    constexpr int kArrowWidth = 52;
}

PresetBar::PresetBar (PresetManager& p) : presets (p)
{
    setTitle ("Preset");
    setTooltip ("Click the name for the preset list; arrows step through presets");
    refresh();
}

PresetBar::Zone PresetBar::zoneAt (Point<int> p) const
{
    if (! getLocalBounds().contains (p))
        return Zone::none;
    if (p.x < kArrowWidth)
        return Zone::previous;
    if (p.x > getWidth() - kArrowWidth)
        return Zone::next;
    return Zone::menu;
}

void PresetBar::refresh()
{
    const auto name = presets.currentName();
    const bool modified = presets.isModified();
    if (name != shownName || modified != shownModified)
    {
        shownName = name;
        shownModified = modified;
        repaint();
    }
}

void PresetBar::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colour (18, 24, 66).withAlpha (hover == Zone::menu ? 0.75f : 0.6f));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (Theme::hairline);
    g.drawRoundedRectangle (b, 10.0f, 1.0f);

    // Arrows.
    auto arrow = [&] (Rectangle<float> r, bool left, bool hot) {
        const auto c = r.getCentre();
        Path p;
        p.startNewSubPath (c.x + (left ? 3.0f : -3.0f), c.y - 7.0f);
        p.lineTo (c.x + (left ? -3.0f : 3.0f), c.y);
        p.lineTo (c.x + (left ? 3.0f : -3.0f), c.y + 7.0f);
        g.setColour (hot ? Theme::text : Theme::textMuted);
        g.strokePath (p, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    };
    arrow (b.withWidth (static_cast<float> (kArrowWidth)), true, hover == Zone::previous);
    arrow (b.withTrimmedLeft (b.getWidth() - static_cast<float> (kArrowWidth)), false, hover == Zone::next);

    // Number and name.
    auto middle = b.reduced (static_cast<float> (kArrowWidth), 0.0f);
    const int index = presets.currentFactoryIndex();
    const String number = presets.isUser() ? String ("USER") : String (index + 1).paddedLeft ('0', 3);
    const auto nameFont = Fonts::sans (23.0f);
    const auto numberFont = Fonts::caps (14.0f, 0.18f);
    const String name = shownName;
    const float nameW = GlyphArrangement::getStringWidth (nameFont, name);
    const float numberW = GlyphArrangement::getStringWidth (numberFont, number);
    const float modW = shownModified ? 14.0f : 0.0f;
    const float total = jmin (middle.getWidth(), numberW + 14.0f + nameW + modW);
    auto line = middle.withSizeKeepingCentre (total, middle.getHeight());

    g.setFont (numberFont);
    g.setColour (Theme::textMuted);
    g.drawText (number, line.removeFromLeft (numberW), Justification::centredLeft, false);
    line.removeFromLeft (14.0f);
    g.setFont (nameFont);
    g.setColour (Theme::text);
    g.drawFittedText (name, line.removeFromLeft (line.getWidth() - modW).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    if (shownModified)
    {
        // Gold dot: edited since the preset was loaded or saved.
        g.setColour (Theme::gold);
        g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre ({ line.getX() + 8.0f, line.getCentreY() - 1.0f }));
    }
}

void PresetBar::mouseMove (const MouseEvent& e)
{
    const auto z = zoneAt (e.getPosition());
    if (z != hover)
    {
        hover = z;
        repaint();
    }
}

void PresetBar::mouseExit (const MouseEvent&)
{
    hover = Zone::none;
    repaint();
}

void PresetBar::mouseDown (const MouseEvent& e)
{
    switch (zoneAt (e.getPosition()))
    {
        case Zone::previous: presets.step (-1); break;
        case Zone::next:     presets.step (+1); break;
        case Zone::menu:     showMenu(); break;
        case Zone::none:     break;
    }
    refresh();
}

void PresetBar::showMenu()
{
    PopupMenu menu;
    menu.addSectionHeader ("Factory");
    const int current = presets.currentFactoryIndex();
    for (int i = 0; i < presets.numFactory(); ++i)
        menu.addItem (PopupMenu::Item (presets.factoryName (i))
                          .setTicked (i == current)
                          .setAction ([this, i] { presets.loadFactory (i); refresh(); }));

    const auto user = presets.userNames();
    if (! user.isEmpty())
    {
        menu.addSectionHeader ("Your presets");
        for (const auto& name : user)
            menu.addItem (PopupMenu::Item (name)
                              .setTicked (presets.isUser() && presets.currentName() == name)
                              .setAction ([this, name] { presets.loadUser (name); refresh(); }));
    }

    menu.addSeparator();
    menu.addItem ("Save preset...", [this] { showSaveDialog(); });
    if (presets.isUser())
    {
        const auto name = presets.currentName();
        menu.addItem ("Delete \"" + name + "\"", [this, name] {
            presets.deleteUser (name);
            refresh();
        });
    }
    menu.addItem ("Show presets folder", [] {
        const auto folder = PresetManager::userFolder();
        folder.createDirectory();
        folder.revealToUser();
    });

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()));
}

void PresetBar::showSaveDialog()
{
    auto* window = new AlertWindow ("Save preset", "Name your preset. Saving with an existing name replaces it.", MessageBoxIconType::NoIcon, this);
    window->setLookAndFeel (&getLookAndFeel());
    window->addTextEditor ("name", presets.isUser() ? presets.currentName() : String(), {});
    window->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    window->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

    SafePointer<PresetBar> safe (this);
    window->enterModalState (true, ModalCallbackFunction::create ([safe, window] (int result) {
        if (safe == nullptr || result != 1)
            return;
        const auto name = window->getTextEditorContents ("name").trim();
        if (name.isNotEmpty())
            safe->presets.saveUser (name);
        safe->refresh();
    }), true);
}

} // namespace rain::ui
