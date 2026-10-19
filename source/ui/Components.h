#pragma once

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

// The sky: gradient, stars, moon behind a rain cloud and low mist are rendered once
// to an image; the falling rain is drawn on top and animated by advance().
class SkyBackground final : public juce::Component
{
public:
    SkyBackground();

    void paint (juce::Graphics&) override;
    void resized() override;

    void advance (float seconds);

    // The sky follows the sound: rain (0 to 1) sets how heavily it rains, and
    // night (0 to 1) darkens the sky and brings out nebulae and more stars.
    void setWeather (float rain, float night) noexcept;
    // Bypassed: the moon wanes to a crescent and the sky goes very dark.
    void setBypassed (bool b) noexcept { eclipseTarget = b ? 1.0f : 0.0f; }

    // Where the moon sits, in design coordinates (it is also the bypass switch).
    static juce::Rectangle<float> moonBounds() noexcept { return { 1004.0f, 22.0f, 100.0f, 100.0f }; }

private:
    void renderStatic();

    struct Streak { float x, phase, length, alpha, speed; };

    // sky: gradient, stars and mist. night: darkening, nebulae and extra stars,
    // faded in by `night`. decor: moon, panels, wordmark and headings, always on top.
    juce::Image sky, nightSky, decor;
    std::array<Streak, 260> streaks {};
    float time = 0.0f;
    float rainTarget = 0.5f, rainAmount = 0.5f;
    float nightTarget = 0.0f, nightAmount = 0.0f;
    float eclipseTarget = 0.0f, eclipseAmount = 0.0f;
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
