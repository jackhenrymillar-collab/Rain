#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace rain::ui
{

// The night sky behind the panel. It follows the sound:
//   rain   (DENSITY away from noon)  heavier rain and more stars
//   night  (CLOUD)                   a much darker sky, vast nebulae, aurora low down,
//                                    no mist, fainter rain, and the moon wanes to a crescent
//   bloom  (SHAPE)                   the moon's glow blooms into a wide halo ring
//   storm  (FEEDBACK)                forked lightning with an afterglow, brighter stars
// Bypass takes the moon to a thin crescent and the sky very dark. The dark side of
// the moon stays faintly visible in earthshine, craters and all.
class SkyBackground final : public juce::Component
{
public:
    SkyBackground();

    void paint (juce::Graphics&) override;
    void resized() override;

    void advance (float seconds);

    struct Weather
    {
        float rain = 0.5f, night = 0.0f, bloom = 0.5f, storm = 0.0f;
    };
    void setWeather (Weather w) noexcept;
    void setBypassed (bool b) noexcept { eclipseTarget = b ? 1.0f : 0.0f; }

    // Where the moon sits, in design coordinates (it is also the bypass switch).
    static juce::Rectangle<float> moonBounds() noexcept { return { 1004.0f, 22.0f, 100.0f, 100.0f }; }

    // Test hook: strike lightning now.
    void strikeLightning();

private:
    void renderStatic();
    void drawStars (juce::Graphics&, float sx, float sy);
    void drawAurora (juce::Graphics&, float sx, float sy, float amount);
    void drawMoon (juce::Graphics&, float sx, float sy);

    struct Streak { float x, phase, length, alpha, speed; };
    struct Star { float x, y, radius, alpha, twinkle, speed; };
    struct Branch { juce::Path path; float width; };
    struct Bolt
    {
        juce::Rectangle<float> bounds;    // design coordinates
        juce::Image glow, core;           // afterglow and bright channel, rendered once
        float age = 0.0f, flicker = -1.0f;
    };

    void addBranch (std::vector<Branch>&, juce::Point<float> from, juce::Point<float> to,
                    float spread, int depth, float width, int& budget);

    // sky: gradient. mist: low mist, cleared by night. night: darkening and nebulae.
    // decor: panels, wordmark and headings, always on top.
    juce::Image sky, mist, nightSky, decor, starGlow, aurora;
    std::array<Streak, 260> streaks {};
    std::array<Star, 700> stars {};
    std::vector<Bolt> bolts;
    juce::Random random;

    float time = 0.0f;
    Weather target, amount;
    float eclipseTarget = 0.0f, eclipseAmount = 0.0f;
    float untilStrike = 1.5f, pendingStrike = -1.0f, flash = 0.0f;
};

} // namespace rain::ui
