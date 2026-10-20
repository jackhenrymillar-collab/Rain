#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rain::ui
{

// The night-sky look, shared with Overcast. All layout is in a fixed 1170 x 760
// design space; the editor scales it to the window size.
namespace Theme
{
    inline constexpr int designWidth = 1170;
    inline constexpr int designHeight = 860;

    inline const juce::Colour skyTop     { 0xff0a0f2e };
    inline const juce::Colour skyMid     { 0xff131a48 };
    inline const juce::Colour skyBottom  { 0xff1d2462 };
    inline const juce::Colour text       { 0xffeceaf7 };
    inline const juce::Colour textSoft   { 0xffd6d7ee };
    inline const juce::Colour textMuted  { 0xffa9aed6 };
    inline const juce::Colour gold       { 0xffe8c987 };
    inline const juce::Colour moonLight  { 0xfffff8df };
    inline const juce::Colour glass      { juce::Colour (18, 24, 66).withAlpha (0.55f) };
    inline const juce::Colour hairline   { juce::Colour (190, 196, 255).withAlpha (0.32f) };
    inline const juce::Colour dotGlow    { 0xfffff6d8 };
}

// Embedded fonts (SIL Open Font License, see resources/fonts).
struct Fonts
{
    static juce::Font serif (float height, bool semiBold = false);
    static juce::Font sans (float height, bool medium = false);
    // Uppercase label style: Jost with wide letter spacing.
    static juce::Font caps (float height, float tracking = 0.2f);
};

} // namespace rain::ui
