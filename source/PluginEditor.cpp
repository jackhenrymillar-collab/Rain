#include "PluginEditor.h"

namespace rain
{

using namespace juce;
using ui::Fonts;
namespace Theme = ui::Theme;

namespace
{
    struct ModeInfo
    {
        const char* title;
        const char* description;
    };

    // Matches modeNames() and dsp::Mode.
    constexpr ModeInfo kModes[] = {
        { "Grains", "Short slices of the buffer, scattered and layered into a cloud." },
        { "Delay", "Echoes from the buffer. DENSITY sets the delay, FEEDBACK the repeats." },
    };

    // Tooltips for the quality chips (matches qualityNames() and dsp::Quality).
    constexpr const char* kQualityTips[] = {
        "Crisp and clean. 48 kHz, 16-bit, 4 s buffer, bright reverb.",
        "Early sampler grit. 32 kHz, 12-bit, 8 s buffer, a little darker.",
        "Soft saturation and a warm, rounded top end. 10 s buffer.",
        "Hiss, wow and flutter, and a worn 8-bit buffer. 16 s.",
    };

    constexpr float kColumnStep = 135.25f; // eight knob columns across 1074 px
    constexpr int kColumnWidth = 127;
    constexpr int kLeft = 48;

    int columnX (int i) { return kLeft + roundToInt (static_cast<float> (i) * kColumnStep); }

    // Which modulation target each large knob shows, in panel order.
    constexpr dsp::ModTarget kKnobTargets[] = {
        dsp::ModTarget::time, dsp::ModTarget::size, dsp::ModTarget::shape, dsp::ModTarget::pitch,
        dsp::ModTarget::density, dsp::ModTarget::feedback, dsp::ModTarget::reverb, dsp::ModTarget::dryWet,
    };
}

Label& RainEditor::styleCaps (Label& l, float height)
{
    l.setFont (Fonts::caps (height, 0.22f));
    l.setColour (Label::textColourId, Theme::textMuted);
    l.setBorderSize ({});
    l.setInterceptsMouseClicks (false, false);
    return l;
}

RainEditor::RainEditor (RainProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      state (p.getState()),
      presetBar (p.getPresets()),
      grainDisplay (p.getState(), p.getMonitor())
{
    content.setLookAndFeel (&lookAndFeel);
    tooltips.setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.addAndMakeVisible (sky);
    content.addAndMakeVisible (presetBar);
    content.addAndMakeVisible (saveButton);
    saveButton.getProperties().set ("pill", true);
    saveButton.setTooltip ("Save the current sound as a preset");
    saveButton.onClick = [this] { presetBar.showSaveDialog(); };

    // Mode title, description and switch.
    modeTitle.setFont (Fonts::serif (70.0f));
    modeTitle.setColour (Label::textColourId, Theme::text);
    modeTitle.setBorderSize ({});
    modeDescription.setFont (Fonts::sans (21.0f));
    modeDescription.setColour (Label::textColourId, Theme::textSoft);
    modeDescription.setBorderSize ({});
    modeDescription.setMinimumHorizontalScale (0.85f);
    for (auto* l : { &modeTitle, &modeDescription })
    {
        l->setInterceptsMouseClicks (false, false);
        content.addAndMakeVisible (*l);
    }
    const auto modes = modeNames();
    for (int i = 0; i < 2; ++i)
    {
        auto& chip = modeChips[static_cast<size_t> (i)];
        chip.setButtonText (modes[i]);
        chip.onClick = [this, i] {
            if (modeAttachment != nullptr)
                modeAttachment->setValueAsCompleteGesture (static_cast<float> (i));
        };
        content.addAndMakeVisible (chip);
    }
    modeChips[0].setTooltip ("Grains: Beads' granular mode");
    modeChips[1].setTooltip ("Delay: Beads' delay mode (SIZE fully clockwise on the module)");
    modeAttachment = std::make_unique<ParameterAttachment> (*state.getParameter (ParamID::mode), [this] (float v) { showMode (roundToInt (v)); });

    content.addAndMakeVisible (grainDisplay);

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

    // Motion.
    const char* modNames[] = { "WAVE", "DRIFT" };
    const char* modTips[] = { "A slow sine wave", "Smooth random drift" };
    const char* rateIds[] = { ParamID::mod1Rate, ParamID::mod2Rate };
    const char* depthIds[] = { ParamID::mod1Depth, ParamID::mod2Depth };
    const char* targetIds[] = { ParamID::mod1Target, ParamID::mod2Target };
    for (size_t m = 0; m < 2; ++m)
    {
        auto& row = modRows[m];
        styleCaps (row.name, 17.0f).setText (modNames[m], dontSendNotification);
        row.name.setColour (Label::textColourId, Theme::textSoft);
        row.name.setTooltip (modTips[m]);
        row.name.setInterceptsMouseClicks (true, false);
        row.rate = std::make_unique<ui::OrbKnob> (state, rateIds[m], "RATE", ui::OrbKnob::Size::small);
        row.depth = std::make_unique<ui::OrbKnob> (state, depthIds[m], "DEPTH", ui::OrbKnob::Size::small);
        row.target = std::make_unique<ui::ChoiceBox> (state, targetIds[m]);
        row.target->getBox().setTooltip ("What this modulator moves");
        content.addAndMakeVisible (row.name);
        content.addAndMakeVisible (row.scope);
        content.addAndMakeVisible (*row.rate);
        content.addAndMakeVisible (*row.depth);
        content.addAndMakeVisible (*row.target);
    }

    // Tempo and scale.
    syncRate = std::make_unique<ui::ChoiceBox> (state, ParamID::syncRate);
    scale = std::make_unique<ui::ChoiceBox> (state, ParamID::scale);
    syncButton.setTooltip ("Fire grains from the host tempo: DENSITY divides the clock, TIME snaps to clock steps");
    syncRate->getBox().setTooltip ("Clock note length");
    scale->getBox().setTooltip ("Keep grain pitch, PITCH RANDOM and modulation in a scale");
    styleCaps (scaleLabel, 17.0f).setText ("SCALE", dontSendNotification);
    scaleLabel.setColour (Label::textColourId, Theme::textSoft);
    for (auto* l : { &tempoReadout, &scaleReadout })
    {
        l->setFont (Fonts::sans (18.0f));
        l->setColour (Label::textColourId, Theme::textMuted);
        l->setJustificationType (Justification::centredRight);
        l->setBorderSize ({});
        l->setInterceptsMouseClicks (false, false);
        content.addAndMakeVisible (*l);
    }
    content.addAndMakeVisible (syncButton);
    content.addAndMakeVisible (*syncRate);
    content.addAndMakeVisible (*scale);
    content.addAndMakeVisible (scaleLabel);

    // Footer: performance buttons and quality.
    styleCaps (qualityLabel).setText ("QUALITY", dontSendNotification);
    qualityLabel.setJustificationType (Justification::centredRight);
    content.addAndMakeVisible (qualityLabel);
    const auto names = qualityNames();
    for (int i = 0; i < 4; ++i)
    {
        auto& chip = qualityChips[static_cast<size_t> (i)];
        chip.setButtonText (names[i]);
        chip.getProperties().set ("compact", true);
        chip.setTooltip (kQualityTips[i]);
        chip.onClick = [this, i] {
            if (qualityAttachment != nullptr)
                qualityAttachment->setValueAsCompleteGesture (static_cast<float> (i));
        };
        content.addAndMakeVisible (chip);
    }
    qualityAttachment = std::make_unique<ParameterAttachment> (*state.getParameter (ParamID::quality), [this] (float v) { showQuality (roundToInt (v)); });

    // Toggle pills.
    const std::pair<TextButton*, const char*> toggles[] = {
        { &freezeButton, ParamID::freeze }, { &seedLockButton, ParamID::seedLock },
        { &autoGainButton, ParamID::autoGain }, { &syncButton, ParamID::sync },
    };
    for (const auto& [button, id] : toggles)
    {
        button->setClickingTogglesState (true);
        buttonAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, id, *button));
    }
    for (auto* b : { &freezeButton, &seedButton, &seedLockButton, &autoGainButton, &syncButton })
    {
        b->getProperties().set ("pill", true);
        content.addAndMakeVisible (*b);
    }
    freezeButton.setTooltip ("Stop recording; grains keep playing the held buffer");
    seedButton.setTooltip ("Hold to play grains (when SEED LOCK is off)");
    seedLockButton.setTooltip ("On: grains run on their own. Off: grains play only while SEED is held");
    autoGainButton.setTooltip ("Set the input level automatically");

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

    modeAttachment->sendInitialUpdate();
    qualityAttachment->sendInitialUpdate();

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio (static_cast<double> (Theme::designWidth) / Theme::designHeight);
        c->setSizeLimits (Theme::designWidth * 6 / 10, Theme::designHeight * 6 / 10,
                          Theme::designWidth * 12 / 10, Theme::designHeight * 12 / 10);
    }
    setSize (Theme::designWidth * 9 / 10, Theme::designHeight * 9 / 10);

    updateLiveValues();
    startTimerHz (30);
}

RainEditor::~RainEditor()
{
    stopTimer();
    tooltips.setLookAndFeel (nullptr);
    content.setLookAndFeel (nullptr);
}

void RainEditor::showMode (int index)
{
    index = jlimit (0, 1, index);
    for (int i = 0; i < 2; ++i)
        modeChips[static_cast<size_t> (i)].setToggleState (i == index, dontSendNotification);
    modeTitle.setText (kModes[index].title, dontSendNotification);
    modeDescription.setText (kModes[index].description, dontSendNotification);

    // SIZE does nothing in delay mode.
    const bool delay = index == 1;
    knobs[1]->setAlpha (delay ? 0.4f : 1.0f);
    smallKnobs[1]->setAlpha (delay ? 0.4f : 1.0f);
    knobs[1]->getSlider().setTooltip (delay ? "SIZE has no effect in Delay mode" : String());
    knobs[0]->getSlider().setTooltip (delay ? "In Delay mode, TIME picks which slice loops while FROZEN" : String());
}

void RainEditor::showQuality (int index)
{
    index = jlimit (0, 3, index);
    for (int i = 0; i < 4; ++i)
        qualityChips[static_cast<size_t> (i)].setToggleState (i == index, dontSendNotification);
}

void RainEditor::updateLiveValues()
{
    const auto& mon = processor.getMonitor();
    auto value = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };

    const bool sync = value (ParamID::sync) > 0.5f;
    const bool delay = value (ParamID::mode) > 0.5f;
    const int rateIndex = jlimit (0, 5, static_cast<int> (value (ParamID::syncRate)));

    // Modulation rings on the large knobs.
    const float depths[] = { value (ParamID::mod1Depth), value (ParamID::mod2Depth) };
    const int targets[] = { static_cast<int> (value (ParamID::mod1Target)), static_cast<int> (value (ParamID::mod2Target)) };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int t = static_cast<int> (kKnobTargets[i]);
        bool moved = (targets[0] == t && std::abs (depths[0]) > 0.05f) || (targets[1] == t && std::abs (depths[1]) > 0.05f);
        if (kKnobTargets[i] == dsp::ModTarget::time && sync && ! delay)
            moved = true; // TIME snaps to clock steps
        knobs[i]->setModulated (moved ? mon.effective[static_cast<size_t> (t)].load() : -1.0f);
    }

    // Synced DENSITY and TIME read in note lengths.
    const float effectiveTime = mon.effective[static_cast<size_t> (dsp::ModTarget::time)].load();
    const float effectiveDensity = mon.effective[static_cast<size_t> (dsp::ModTarget::density)].load();
    const float density = value (ParamID::density) * 0.01f;
    knobs[4]->setValueOverride (delay ? ui::GrainDisplay::delayText (density, mon.bufferSeconds.load(), sync, mon.bpm.load(), rateIndex)
                                : sync ? ui::GrainDisplay::syncDensityText (density, rateIndex) : String());
    knobs[0]->setValueOverride (sync && ! delay ? ui::GrainDisplay::syncTimeText (effectiveTime, mon.bufferSeconds.load(), mon.bpm.load(), rateIndex) : String());
    ignoreUnused (effectiveDensity);

    for (size_t m = 0; m < 2; ++m)
        modRows[m].scope.push (mon.modValue[m].load());

    const String tempo = sync ? "Locked to " + String (roundToInt (mon.bpm.load())) + " BPM"
                              : String (delay ? "Delay runs free" : "Grains run free");
    if (tempoReadout.getText() != tempo)
        tempoReadout.setText (tempo, dontSendNotification);
    const String scaleText = value (ParamID::scale) > 0.5f ? "Grains stay in key" : "Pitch is free";
    if (scaleReadout.getText() != scaleText)
        scaleReadout.setText (scaleText, dontSendNotification);
    syncRate->setAlpha (sync ? 1.0f : 0.5f);
}

void RainEditor::tick()
{
    constexpr float dt = 1.0f / 30.0f;
    sky.advance (dt);
    grainDisplay.advance (dt);
    presetBar.refresh();
    updateLiveValues();
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
    presetBar.setBounds (330, 32, 470, 56);
    saveButton.setBounds (812, 41, 92, 38);

    // Mode.
    modeTitle.setBounds (kLeft, 138, 520, 68);
    modeDescription.setBounds (kLeft, 210, 530, 28);
    for (int i = 0; i < 2; ++i)
        modeChips[static_cast<size_t> (i)].setBounds (kLeft + i * 142, 252, 130, 44);
    grainDisplay.setBounds (596, 128, 526, 176);

    // Knob rows.
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (columnX (static_cast<int> (i)), 350, kColumnWidth, 152);
    for (size_t i = 0; i < smallKnobs.size(); ++i)
        smallKnobs[i]->setBounds (columnX (static_cast<int> (i)), 514, kColumnWidth, 54);
    autoGainButton.setBounds (columnX (6) + (kColumnWidth - 96) / 2, 522, 96, 38);

    // Motion rows.
    for (size_t m = 0; m < 2; ++m)
    {
        auto& row = modRows[m];
        const int y = 614 + static_cast<int> (m) * 58;
        row.name.setBounds (58, y, 70, 54);
        row.scope.setBounds (126, y + 9, 56, 36);
        row.rate->setBounds (188, y, 128, 54);
        row.depth->setBounds (318, y, 128, 54);
        row.target->setBounds (450, y + 8, 108, 38);
    }

    // Tempo and scale.
    syncButton.setBounds (606, 622, 104, 38);
    syncRate->setBounds (722, 622, 100, 38);
    tempoReadout.setBounds (834, 622, 280, 38);
    scaleLabel.setBounds (610, 680, 100, 38);
    scale->setBounds (722, 680, 160, 38);
    scaleReadout.setBounds (894, 680, 220, 38);

    // Footer.
    freezeButton.setBounds (kLeft, 772, 150, 46);
    seedButton.setBounds (kLeft + 162, 772, 120, 46);
    seedLockButton.setBounds (kLeft + 294, 772, 172, 46);
    qualityLabel.setBounds (600, 772, 116, 46);
    for (int i = 0; i < 4; ++i)
        qualityChips[static_cast<size_t> (i)].setBounds (728 + i * 98, 774, 94, 42);
}

} // namespace rain
