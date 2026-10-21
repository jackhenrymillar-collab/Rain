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
//   time   (TIME)                    the night turns: stars, nebulae and moon wheel
//                                    across the sky the further back droplets reach
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
        float rain = 0.5f, night = 0.0f, bloom = 0.5f, storm = 0.0f, time = 0.0f;
    };
    void setWeather (Weather w) noexcept;
    void setBypassed (bool b) noexcept { eclipseTarget = b ? 1.0f : 0.0f; }

    // Where the moon sits at TIME 0, in design coordinates (it is also the bypass switch).
    static juce::Rectangle<float> moonBounds() noexcept { return { 1004.0f, 22.0f, 100.0f, 100.0f }; }
    // Where it is now, as TIME turns the sky.
    juce::Rectangle<float> currentMoonBounds() const noexcept;

    // Test hook: strike lightning now.
    void strikeLightning();

private:
    void renderStatic();
    // The slow-changing layers (sky, mist, night, nebulae, star glows), cached at the
    // screen's pixel size and redrawn only when CLOUD, TIME, FEEDBACK or DENSITY move.
    // Returns true when it redrew.
    bool updateBase (int pixelWidth, int pixelHeight);
    // The moon and its glow, cached likewise and redrawn only when its look changes.
    void updateMoonSprite (float pixelScale);
    void drawStarGlows (juce::Graphics&);
    void drawStars (juce::Graphics&);
    void drawAurora (juce::Graphics&, float amount);
    void drawMoon (juce::Graphics&, juce::Point<float> centre);
    float moonReach() const noexcept;
    // Turns a design-space point with the sky, about a pole far below the panel.
    juce::Point<float> turned (juce::Point<float> p) const noexcept;
    juce::AffineTransform turning() const noexcept;

    struct Streak { float x, phase, length, alpha, speed; };
    struct Star { float x, y, radius, alpha, twinkle, speed; };
    struct Branch { juce::Path path; float width; };
    struct Bolt
    {
        juce::Rectangle<float> bounds;    // design coordinates
        std::vector<Branch> branches;
        juce::Image glow, core;           // afterglow and bright channel, rendered once
        float scale = 0.0f;               // screen pixels per design unit they were rendered at
        float age = 0.0f, flicker = -1.0f;
    };
    void renderBolt (Bolt&, float pixelScale);

    void addBranch (std::vector<Branch>&, juce::Point<float> from, juce::Point<float> to,
                    float spread, int depth, float width, int& budget);

    // sky: gradient. mist: low mist, cleared by night. night: darkening and nebulae.
    // decor: panels, wordmark and headings, always on top.
    juce::Image sky, mist, nightSky, decor, starGlow, aurora;
    // Cached layers (see updateBase and updateMoonSprite), the values and the frame
    // they were drawn for.
    juce::Image baseLayer, skyLayer, moonSprite;   // skyLayer: the base with the aurora on top
    std::array<float, 4> baseKey { -1.0f, -1.0f, -1.0f, -1.0f };
    std::array<float, 4> moonKey { -1.0f, -1.0f, -1.0f, -1.0f };
    float moonSpriteReach = 0.0f;
    int frame = 0, baseFrame = -10, moonFrame = -10, auroraFrame = -1;
    std::array<Streak, 260> streaks {};
    std::array<Star, 900> stars {};
    std::vector<Bolt> bolts;
    juce::Random random;

    float time = 0.0f;
    Weather target, amount;
    float eclipseTarget = 0.0f, eclipseAmount = 0.0f;
    float untilStrike = 1.5f, pendingStrike = -1.0f, flash = 0.0f;
};

} // namespace rain::ui
