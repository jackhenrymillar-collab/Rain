#include "PluginEditor.h"

namespace rain
{

using namespace juce;
using ui::Fonts;
namespace Theme = ui::Theme;

namespace
{
    struct QualityInfo
    {
        const char* title;
        const char* description;
        const char* buffer;
    };

    // Matches qualityNames() and dsp::Quality.
    constexpr QualityInfo kQualities[] = {
        { "Digital", "Crisp and clean. 48 kHz, 16-bit, with a bright reverb.", "4 s" },
        { "Vintage Digital", "Early sampler grit. 32 kHz, 12-bit, a little darker.", "8 s" },
        { "Clean Tape", "Soft saturation and a warm, rounded top end.", "10 s" },
        { "Dirty Tape", "Hiss, wow and flutter, and a worn 8-bit buffer.", "16 s" },
    };

    constexpr float kColumn = 127.25f;  // eight knob columns across 1074 px
    constexpr float kColumnStep = 135.25f;
    constexpr int kLeft = 48;

    int columnX (int i) { return kLeft + roundToInt (static_cast<float> (i) * kColumnStep); }
}

void RainEditor::PresetBar::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colour (18, 24, 66).withAlpha (0.6f));
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (Theme::hairline);
    g.drawRoundedRectangle (b, 10.0f, 1.0f);

    // Presets arrive in a later phase; for now the bar names the default state.
    g.setColour (Theme::textMuted);
    g.setFont (Fonts::caps (14.0f, 0.22f));
    g.drawText ("PRESET", b.reduced (22.0f, 0.0f), Justification::centredLeft, false);
    g.setColour (Theme::text);
    g.setFont (Fonts::sans (23.0f));
    g.drawText ("Init", b, Justification::centred, false);
}

RainEditor::RainEditor (RainProcessor& p)
    : AudioProcessorEditor (p), processor (p), state (p.getState())
{
    content.setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.addAndMakeVisible (sky);
    content.addAndMakeVisible (presetBar);

    qualityTitle.setFont (Fonts::serif (74.0f));
    qualityTitle.setColour (Label::textColourId, Theme::text);
    qualityTitle.setBorderSize ({});
    qualityDescription.setFont (Fonts::sans (23.0f));
    qualityDescription.setColour (Label::textColourId, Theme::textSoft);
    qualityDescription.setBorderSize ({});
    status.setFont (Fonts::caps (18.0f, 0.16f));
    status.setColour (Label::textColourId, Theme::textMuted);
    status.setJustificationType (Justification::centredRight);
    for (auto* l : { &qualityTitle, &qualityDescription, &status })
    {
        l->setInterceptsMouseClicks (false, false);
        content.addAndMakeVisible (*l);
    }

    // Quality chips, like Overcast's machine chips.
    auto* qualityParam = state.getParameter (ParamID::quality);
    const auto names = qualityNames();
    for (int i = 0; i < 4; ++i)
    {
        auto& chip = qualityChips[static_cast<size_t> (i)];
        chip.setButtonText (names[i]);
        chip.setClickingTogglesState (false);
        chip.onClick = [this, i] {
            if (qualityAttachment != nullptr)
                qualityAttachment->setValueAsCompleteGesture (static_cast<float> (i));
        };
        content.addAndMakeVisible (chip);
    }
    qualityAttachment = std::make_unique<ParameterAttachment> (*qualityParam, [this] (float v) { showQuality (roundToInt (v)); });
    qualityAttachment->sendInitialUpdate();

    // Knobs.
    const std::pair<const char*, const char*> large[] = {
        { ParamID::time, "TIME" }, { ParamID::size, "SIZE" }, { ParamID::shape, "SHAPE" }, { ParamID::pitch, "PITCH" },
        { ParamID::density, "DENSITY" }, { ParamID::feedback, "FEEDBACK" }, { ParamID::reverb, "REVERB" }, { ParamID::dryWet, "DRY / WET" },
    };
    for (const auto& [id, name] : large)
    {
        knobs.push_back (std::make_unique<ui::OrbKnob> (state, id, name, ui::OrbKnob::Size::large));
        content.addAndMakeVisible (*knobs.back());
    }
    const std::pair<const char*, const char*> small[] = {
        { ParamID::timeRandom, "RANDOM" }, { ParamID::sizeRandom, "RANDOM" }, { ParamID::shapeRandom, "RANDOM" },
        { ParamID::pitchRandom, "RANDOM" }, { ParamID::inputGain, "INPUT" }, { ParamID::outputGain, "OUTPUT" },
    };
    for (const auto& [id, name] : small)
    {
        smallKnobs.push_back (std::make_unique<ui::OrbKnob> (state, id, name, ui::OrbKnob::Size::small));
        content.addAndMakeVisible (*smallKnobs.back());
    }

    // Pill buttons.
    for (auto* b : { &freezeButton, &seedButton, &seedLockButton, &autoGainButton })
    {
        b->getProperties().set ("pill", true);
        content.addAndMakeVisible (*b);
    }
    freezeAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, ParamID::freeze, freezeButton);
    seedLockAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, ParamID::seedLock, seedLockButton);
    autoGainAttachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, ParamID::autoGain, autoGainButton);
    freezeButton.setClickingTogglesState (true);
    seedLockButton.setClickingTogglesState (true);
    autoGainButton.setClickingTogglesState (true);
    freezeButton.setTooltip ("Stop recording; grains keep playing the held buffer");
    seedLockButton.setTooltip ("On: grains run on their own. Off: grains play only while SEED is held");

    // SEED is momentary: the parameter is on only while the button is held.
    seedAttachment = std::make_unique<ParameterAttachment> (*state.getParameter (ParamID::seed), [] (float) {});
    seedButton.onStateChange = [this] {
        const bool down = seedButton.isDown();
        if (down == seedHeld)
            return;
        seedHeld = down;
        if (down)
        {
            seedAttachment->beginGesture();
            seedAttachment->setValueAsPartOfGesture (1.0f);
        }
        else
        {
            seedAttachment->setValueAsPartOfGesture (0.0f);
            seedAttachment->endGesture();
        }
    };

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio (static_cast<double> (Theme::designWidth) / Theme::designHeight);
        c->setSizeLimits (Theme::designWidth * 6 / 10, Theme::designHeight * 6 / 10,
                          Theme::designWidth * 12 / 10, Theme::designHeight * 12 / 10);
    }
    setSize (Theme::designWidth * 9 / 10, Theme::designHeight * 9 / 10);

    updateStatus();
    startTimerHz (30);
}

RainEditor::~RainEditor()
{
    stopTimer();
    content.setLookAndFeel (nullptr);
}

void RainEditor::showQuality (int index)
{
    index = jlimit (0, 3, index);
    for (int i = 0; i < 4; ++i)
        qualityChips[static_cast<size_t> (i)].setToggleState (i == index, dontSendNotification);
    qualityTitle.setText (kQualities[index].title, dontSendNotification);
    qualityDescription.setText (kQualities[index].description, dontSendNotification);
    updateStatus();
}

void RainEditor::updateStatus()
{
    const int q = jlimit (0, 3, roundToInt (state.getRawParameterValue (ParamID::quality)->load()));
    const bool delayMode = state.getRawParameterValue (ParamID::size)->load() > 99.0f;
    const String text = String ("BUFFER  ") + kQualities[q].buffer + "        MODE  " + (delayMode ? "Delay" : "Grains");
    if (status.getText() != text)
        status.setText (text, dontSendNotification);
}

void RainEditor::timerCallback()
{
    sky.advance (1.0f / 30.0f);
    updateStatus();
}

void RainEditor::resized()
{
    content.setBounds (0, 0, Theme::designWidth, Theme::designHeight);
    content.setTransform (AffineTransform::scale (static_cast<float> (getWidth()) / static_cast<float> (Theme::designWidth)));
    layoutContent();
}

void RainEditor::layoutContent()
{
    sky.setBounds (content.getLocalBounds());
    presetBar.setBounds (385, 34, 400, 54);

    qualityTitle.setBounds (kLeft, 138, 760, 70);
    qualityDescription.setBounds (kLeft, 210, 760, 28);
    for (int i = 0; i < 4; ++i)
        qualityChips[static_cast<size_t> (i)].setBounds (kLeft + i * 150, 254, 140, 46);

    const int columnWidth = roundToInt (kColumn);
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (columnX (static_cast<int> (i)), 358, columnWidth, 150);
    for (size_t i = 0; i < smallKnobs.size(); ++i)
        smallKnobs[i]->setBounds (columnX (static_cast<int> (i)), 524, columnWidth, 48);
    autoGainButton.setBounds (columnX (6) + (columnWidth - 92) / 2, 531, 92, 34);

    freezeButton.setBounds (kLeft, 684, 150, 46);
    seedButton.setBounds (kLeft + 162, 684, 120, 46);
    seedLockButton.setBounds (kLeft + 294, 684, 160, 46);
    status.setBounds (600, 684, 1122 - 600, 46);
}

} // namespace rain
