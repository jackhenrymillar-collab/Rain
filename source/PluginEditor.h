#pragma once

#include "PluginProcessor.h"
#include "ui/Components.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>
#include <vector>

namespace rain
{

// Rain's night-sky front panel, styled after Overcast. Laid out in a fixed
// 1170 x 760 design space and scaled to the window, which keeps its aspect ratio.
class RainEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit RainEditor (RainProcessor&);
    ~RainEditor() override;

    void resized() override;

private:
    void timerCallback() override;
    void layoutContent();
    void showQuality (int index);
    void updateStatus();

    RainProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    ui::RainLookAndFeel lookAndFeel;

    juce::Component content;
    ui::SkyBackground sky;

    struct PresetBar final : juce::Component
    {
        void paint (juce::Graphics&) override;
    } presetBar;

    juce::Label qualityTitle, qualityDescription, status;
    std::array<juce::TextButton, 4> qualityChips;
    std::unique_ptr<juce::ParameterAttachment> qualityAttachment;

    std::vector<std::unique_ptr<ui::OrbKnob>> knobs, smallKnobs;

    juce::TextButton freezeButton { "FREEZE" }, seedButton { "SEED" },
                     seedLockButton { "SEED LOCK" }, autoGainButton { "AUTO" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment, seedLockAttachment, autoGainAttachment;
    std::unique_ptr<juce::ParameterAttachment> seedAttachment;
    bool seedHeld = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RainEditor)
};

} // namespace rain
