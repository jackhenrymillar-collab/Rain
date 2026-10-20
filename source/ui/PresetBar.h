#pragma once

#include "Theme.h"
#include "../Presets.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace rain::ui
{

// The preset selector: arrows step through presets, clicking the name opens the
// full list (factory and your own), with save and delete.
class PresetBar final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    explicit PresetBar (PresetManager&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void refresh();          // call from the editor's timer
    void showSaveDialog();

private:
    enum class Zone { none, previous, menu, next };
    Zone zoneAt (juce::Point<int>) const;
    void showMenu();

    PresetManager& presets;
    juce::String shownName;
    bool shownModified = false;
    Zone hover = Zone::none;
};

} // namespace rain::ui
