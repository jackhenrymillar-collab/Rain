#pragma once

#include "PluginProcessor.h"
#include "ui/Components.h"
#include "ui/GrainDisplay.h"
#include "ui/PresetBar.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>
#include <vector>

namespace rain
{

// Rain's night-sky front panel, styled after Overcast. Laid out in a fixed
// 1170 x 860 design space and scaled to the window, which keeps its aspect ratio.
class RainEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit RainEditor (RainProcessor&);
    ~RainEditor() override;

    void resized() override;

    // One animation step (the 30 Hz timer calls this; the snapshot tool calls it directly).
    void tick();

private:
    void timerCallback() override { tick(); }
    void layoutContent();
    void showMode (int index);
    void showQuality (int index);
    void updateLiveValues();

    static juce::Label& styleCaps (juce::Label&, float height = 16.0f);

    RainProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    ui::RainLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 700 };

    juce::Component content;
    ui::SkyBackground sky;
    ui::PresetBar presetBar;
    juce::TextButton saveButton { "SAVE" };

    // Mode.
    juce::Label modeTitle, modeDescription;
    std::array<juce::TextButton, 2> modeChips;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    ui::GrainDisplay grainDisplay;

    // Knobs.
    std::vector<std::unique_ptr<ui::OrbKnob>> knobs, smallKnobs;
    juce::TextButton autoGainButton { "AUTO INPUT" };
    juce::TextButton overlapButton { "OVERLAP" };

    // Motion: two modulators.
    struct ModRow
    {
        juce::Label name;
        ui::ModScope scope;
        std::unique_ptr<ui::OrbKnob> rate, depth;
        std::unique_ptr<ui::ChoiceBox> target;
    };
    std::array<ModRow, 2> modRows;

    // Tempo and scale.
    juce::TextButton syncButton { "SYNC" };
    std::unique_ptr<ui::ChoiceBox> syncRate, scale;
    juce::Label scaleLabel, tempoReadout, scaleReadout;

    // Footer.
    juce::TextButton freezeButton { "FREEZE" }, seedButton { "SEED" }, seedLockButton { "SEED LOCK" };
    juce::Label qualityLabel;
    std::array<juce::TextButton, 4> qualityChips;
    std::unique_ptr<juce::ParameterAttachment> qualityAttachment;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::ParameterAttachment> seedAttachment;
    bool seedHeld = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RainEditor)
};

} // namespace rain
