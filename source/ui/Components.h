#pragma once

#include "Sky.h"
#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace rain::ui
{

// Draws the glowing navy orb knobs and the gold-outlined chip and pill buttons.
class RainLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    RainLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;
    void drawModRing (juce::Graphics&, juce::Slider&, juce::Rectangle<float> bounds, float size, bool small,
                      float startAngle, float endAngle, float angle);

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    juce::Font getLabelFont (juce::Label&) override;

    // Glass drop-down selectors and their menus.
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
};

// One knob: the orb, its name and its live value, bound to a parameter.
class OrbKnob final : public juce::Component
{
public:
    enum class Size { large, medium, small };

    OrbKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
             const juce::String& name, Size size);

    void resized() override;

    // Shows where modulation (or tempo snapping) has moved the knob, 0 to 1 along its
    // travel; a negative value hides the ring.
    void setModulated (float normalised);
    // Replaces the value text (empty restores the parameter's own text).
    void setValueOverride (const juce::String& text);
    juce::Slider& getSlider() noexcept { return slider; }

private:
    void updateValueText();
    juce::String valueOverride;

    juce::RangedAudioParameter& param;
    Size size;
    juce::Slider slider;
    juce::Label nameLabel, valueLabel;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

// A drop-down bound to a choice parameter.
class ChoiceBox final : public juce::Component
{
public:
    ChoiceBox (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);
    void resized() override { box.setBounds (getLocalBounds()); }
    juce::ComboBox& getBox() noexcept { return box; }

private:
    juce::ComboBox box;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment attachment;
};

// A tiny scrolling trace of a modulator's output.
class ModScope final : public juce::Component
{
public:
    ModScope();
    void push (float value);
    void paint (juce::Graphics&) override;

private:
    std::array<float, 48> history {};
    std::size_t head = 0;
};

// A slim horizontal peak meter, -48 to 0 dBFS, with a gold band marking the range
// to aim for. Anything past the band shows in coral.
class LevelMeter final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    LevelMeter (float zoneLowDb, float zoneHighDb);
    // A new peak reading (linear), dt seconds after the last one.
    void push (float peak, float dt);
    void paint (juce::Graphics&) override;

    static constexpr float kFloorDb = -48.0f;

private:
    float xFor (float db, juce::Rectangle<float> area) const;

    float zoneLow, zoneHigh;
    float levelDb = kFloorDb, holdDb = kFloorDb, holdTime = 0.0f;
};

// An invisible switch over the moon: click to bypass Rain.
class MoonSwitch final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    MoonSwitch (juce::AudioProcessorValueTreeState& state, const juce::String& paramID);
    bool hitTest (int x, int y) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isOn() const noexcept { return on; }

private:
    juce::ParameterAttachment attachment;
    bool on = false;
};

} // namespace rain::ui
