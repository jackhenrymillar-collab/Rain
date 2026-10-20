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
};

// One knob: the orb, its name and its live value, bound to a parameter.
class OrbKnob final : public juce::Component
{
public:
    enum class Size { large, small };

    OrbKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
             const juce::String& name, Size size);

    void resized() override;

private:
    void updateValueText();

    juce::RangedAudioParameter& param;
    Size size;
    juce::Slider slider;
    juce::Label nameLabel, valueLabel;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
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

private:
    void renderStatic();

    struct Streak { float x, phase, length, alpha, speed; };

    juce::Image sky;
    std::array<Streak, 70> streaks {};
    float time = 0.0f;
};

} // namespace rain::ui
