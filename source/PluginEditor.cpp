#include "PluginEditor.h"

#include <tuple>

namespace rain
{

using namespace juce;
using ui::Fonts;
namespace Theme = ui::Theme;

namespace
{
    // Tooltips for the quality chips (matches qualityNames() and dsp::Quality).
    constexpr const char* kScaleTips[] = {
        "Scale: Off. Pitch is free, so PITCH, PITCH RANDOM and modulation glide smoothly",
        "Scale: Chromatic. Droplet pitch snaps to semitones",
        "Scale: Major. Droplet pitch stays in a major scale",
        "Scale: Minor. Droplet pitch stays in a natural minor scale",
        "Scale: Pentatonic. Five notes of the major scale, nothing clashes",
        "Scale: Minor pentatonic. Five notes of the minor scale, nothing clashes",
        "Scale: Fifths. Only roots and fifths, for open, drone-like layers",
        "Scale: Octaves. Only octaves of the source pitch",
    };
    constexpr const char* kQualityTips[] = {
        "Crisp and clean, like the module. 48 kHz, 16-bit, 4 s buffer, bright reverb.",
        "Early sampler grit. 32 kHz, 12-bit, 8 s buffer, a little darker.",
        "Soft saturation and a warm, rounded top end. 10 s buffer.",
        "Hiss, wow and flutter, and a worn 8-bit buffer. 16 s.",
        "The cleanest. 32-bit float buffer and reverb, 8 s, no noise floor even with heavy feedback.",
    };
    // Panel order of the quality chips, cleanest to dirtiest, as parameter indices.
    constexpr int kQualityOrder[] = { 4, 0, 1, 2, 3 };

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
      grainDisplay (p.getState(), p.getMonitor()),
      presetBar (p.getPresets())
{
    content.setLookAndFeel (&lookAndFeel);
    tooltips.setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.addAndMakeVisible (sky);
    moon = std::make_unique<ui::MoonSwitch> (state, ParamID::bypass);
    content.addAndMakeVisible (*moon);

    content.addAndMakeVisible (presetBar);
    content.addAndMakeVisible (saveButton);
    saveButton.getProperties().set ("pill", true);
    saveButton.setTooltip ("Save the current sound as a preset");
    saveButton.onClick = [this] { presetBar.showSaveDialog(); };

    content.addAndMakeVisible (grainDisplay);
    moon->toFront (false); // the moon can drift behind the visualiser; keep it clickable

    // Knobs.
    const std::pair<const char*, const char*> large[] = {
        { ParamID::time, "TIME" }, { ParamID::size, "SIZE" }, { ParamID::shape, "SHAPE" }, { ParamID::pitch, "PITCH" },
        { ParamID::density, "DENSITY" }, { ParamID::feedback, "FEEDBACK" }, { ParamID::reverb, "CLOUD" }, { ParamID::dryWet, "DRY / WET" },
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

    // Grain character: how each grain sounds and where it sits.
    const std::tuple<const char*, const char*, const char*> character[] = {
        { ParamID::reverse, "REVERSE", "Chance each droplet plays backwards" },
        { ParamID::soften, "SOFTEN", "Rounds off the highs and lows of the droplets, leaving the dry signal alone" },
        { ParamID::randomPan, "RANDOM PAN", "Places each droplet at a random point in the stereo field" },
    };
    for (const auto& [id, name, tip] : character)
    {
        characterKnobs.push_back (std::make_unique<ui::OrbKnob> (state, id, name, ui::OrbKnob::Size::medium));
        characterKnobs.back()->getSlider().setTooltip (tip);
        content.addAndMakeVisible (*characterKnobs.back());
    }

    // Overlap: grains keep their length, and come often enough to overlap.
    overlapButton.setTooltip ("Overlap: fills the gaps. When droplets are sparse they come more often, so each one overlaps the next. Droplet length stays as SIZE sets it");
    content.addAndMakeVisible (overlapButton);
    smoothButton.setTooltip ("Smooth random: the RANDOM knobs wander slowly instead of picking a fresh value for every droplet, so textures evolve rather than scatter. Off is the module's per-droplet randomness");
    formantButton.setTooltip ("Formant: pitched droplets keep the tone colour of the source, so voices don't go chipmunk going up or monster going down");

    // Motion.
    const char* modNames[] = { "WAVE", "DRIFT" };
    const char* modTips[] = { "Wave: a slow sine wave. RATE sets its speed, DEPTH how far it moves the control chosen on the right",
                              "Drift: smooth random wandering. RATE sets its speed, DEPTH how far it moves the control chosen on the right" };
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
        row.target->getBox().setTooltip ("The control this modulator moves");
        content.addAndMakeVisible (row.name);
        content.addAndMakeVisible (row.scope);
        content.addAndMakeVisible (*row.rate);
        content.addAndMakeVisible (*row.depth);
        content.addAndMakeVisible (*row.target);
    }

    // Tempo and scale.
    syncRate = std::make_unique<ui::ChoiceBox> (state, ParamID::syncRate);
    scale = std::make_unique<ui::ChoiceBox> (state, ParamID::scale);
    syncButton.setTooltip ("Sync: fire droplets from the host tempo. DENSITY divides the clock and TIME snaps to clock steps");
    syncRate->getBox().setTooltip ("Clock note length");
    scale->getBox().setTooltip ("Keep droplet pitch, PITCH RANDOM and modulation in a scale");
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
    styleCaps (presetLabel).setText ("PRESET", dontSendNotification);
    presetLabel.setJustificationType (Justification::centredRight);
    content.addAndMakeVisible (presetLabel);
    const auto names = qualityNames();
    for (int i = 0; i < 5; ++i)
    {
        const int q = kQualityOrder[i];
        auto& chip = qualityChips[static_cast<size_t> (i)];
        chip.setButtonText (names[q]);
        chip.getProperties().set ("compact", true);
        chip.setTooltip (kQualityTips[q]);
        chip.onClick = [this, q] {
            if (qualityAttachment != nullptr)
                qualityAttachment->setValueAsCompleteGesture (static_cast<float> (q));
        };
        content.addAndMakeVisible (chip);
    }
    qualityAttachment = std::make_unique<ParameterAttachment> (*state.getParameter (ParamID::quality), [this] (float v) { showQuality (roundToInt (v)); });

    // Toggle pills.
    const std::pair<TextButton*, const char*> toggles[] = {
        { &freezeButton, ParamID::freeze }, { &seedLockButton, ParamID::seedLock },
        { &autoGainButton, ParamID::autoGain }, { &syncButton, ParamID::sync }, { &overlapButton, ParamID::overlap },
        { &formantButton, ParamID::formant }, { &smoothButton, ParamID::smoothRandom },
    };
    for (const auto& [button, id] : toggles)
    {
        button->setClickingTogglesState (true);
        buttonAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (state, id, *button));
    }
    for (auto* b : { &freezeButton, &seedButton, &seedLockButton, &autoGainButton, &syncButton, &overlapButton, &formantButton, &smoothButton })
    {
        b->getProperties().set ("pill", true);
        content.addAndMakeVisible (*b);
    }
    freezeButton.setTooltip ("Freeze: stop recording and hold the buffer. Droplets keep playing what's held");
    seedButton.setTooltip ("Seed: hold to play droplets while SEED LOCK is off");
    seedLockButton.setTooltip ("Seed Lock: on, droplets run on their own. Off, droplets play only while SEED is held");
    autoGainButton.setTooltip ("Auto: set the input level automatically, so peaks land in the meter's gold band");
    content.addAndMakeVisible (inputMeter);
    content.addAndMakeVisible (outputMeter);
    inputMeter.setTooltip ("Input level reaching the buffer. Aim for peaks in the gold band");
    outputMeter.setTooltip ("Output level. Peaks ideally sit in the gold band, below -1 dB");

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
    // Closing the window while SEED is held lets it go.
    if (seedHeld)
    {
        seedAttachment->setValueAsPartOfGesture (0.0f);
        seedAttachment->endGesture();
    }
    tooltips.setLookAndFeel (nullptr);
    content.setLookAndFeel (nullptr);
}

void RainEditor::showQuality (int index)
{
    index = jlimit (0, 4, index);
    for (int i = 0; i < 5; ++i)
        qualityChips[static_cast<size_t> (i)].setToggleState (kQualityOrder[i] == index, dontSendNotification);
}

void RainEditor::updateLiveValues()
{
    const auto& mon = processor.getMonitor();
    auto value = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };

    const bool sync = value (ParamID::sync) > 0.5f;
    const int rateIndex = jlimit (0, 5, static_cast<int> (value (ParamID::syncRate)));

    // Modulation rings on the large knobs.
    const float depths[] = { value (ParamID::mod1Depth), value (ParamID::mod2Depth) };
    const int targets[] = { static_cast<int> (value (ParamID::mod1Target)), static_cast<int> (value (ParamID::mod2Target)) };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int t = static_cast<int> (kKnobTargets[i]);
        bool moved = (targets[0] == t && std::abs (depths[0]) > 0.05f) || (targets[1] == t && std::abs (depths[1]) > 0.05f);
        if (kKnobTargets[i] == dsp::ModTarget::time && sync)
            moved = true; // TIME snaps to clock steps
        knobs[i]->setModulated (moved ? mon.effective[static_cast<size_t> (t)].load() : -1.0f);
    }

    // Synced DENSITY and TIME read in note lengths.
    const float effectiveTime = mon.effective[static_cast<size_t> (dsp::ModTarget::time)].load();
    const float effectiveDensity = mon.effective[static_cast<size_t> (dsp::ModTarget::density)].load();
    const float density = value (ParamID::density) * 0.01f;
    knobs[4]->setValueOverride (sync ? ui::GrainDisplay::syncDensityText (density, rateIndex) : String());
    knobs[0]->setValueOverride (sync ? ui::GrainDisplay::syncTimeText (effectiveTime, mon.bufferSeconds.load(), mon.bpm.load(), rateIndex) : String());
    ignoreUnused (effectiveDensity);
    for (size_t m = 0; m < 2; ++m)
        modRows[m].scope.push (mon.modValue[m].load());

    const String tempo = sync ? "Locked to " + String (roundToInt (mon.bpm.load())) + " BPM"
                              : String ("Droplets run free");
    if (tempoReadout.getText() != tempo)
    {
        tempoReadout.setText (tempo, dontSendNotification);
        syncRate->getBox().setTooltip ("Clock note length. " + tempo);
    }
    const String scaleText = value (ParamID::scale) > 0.5f ? "Droplets stay in key" : "Pitch is free";
    if (scaleReadout.getText() != scaleText)
        scaleReadout.setText (scaleText, dontSendNotification);
    const int scaleIndex = jlimit (0, static_cast<int> (std::size (kScaleTips)) - 1, roundToInt (value (ParamID::scale)));
    if (scale->getBox().getTooltip() != kScaleTips[scaleIndex])
        scale->getBox().setTooltip (kScaleTips[scaleIndex]);
    const String syncTip = String ("Sync: fire droplets from the host tempo. DENSITY divides the clock and TIME snaps to clock steps. ") + tempo;
    if (syncButton.getTooltip() != syncTip)
        syncButton.setTooltip (syncTip);
    syncRate->setAlpha (sync ? 1.0f : 0.5f);
}

void RainEditor::tick()
{
    constexpr float dt = 1.0f / 30.0f;
    // The sky follows the sound: DENSITY (rain, stars), CLOUD (deep night), SHAPE (moon halo)
    // and FEEDBACK (lightning, brighter stars).
    {
        const auto& mon = processor.getMonitor();
        const float d = mon.effective[static_cast<size_t> (dsp::ModTarget::density)].load();
        const float r = mon.effective[static_cast<size_t> (dsp::ModTarget::reverb)].load();
        const float shape = mon.effective[static_cast<size_t> (dsp::ModTarget::shape)].load();
        const float fb = mon.effective[static_cast<size_t> (dsp::ModTarget::feedback)].load();
        const float t = mon.effective[static_cast<size_t> (dsp::ModTarget::time)].load();
        sky.setWeather ({ jlimit (0.0f, 1.0f, std::abs (d - 0.5f) * 2.0f), r, shape, fb, t });
    }
    sky.setBypassed (moon->isOn());
    sky.advance (dt);
    // The moon (the bypass switch) travels with the sky.
    const auto moonArea = sky.currentMoonBounds().toNearestInt();
    if (moon->getBounds() != moonArea)
        moon->setBounds (moonArea);
    grainDisplay.advance (dt);
    auto& monitor = processor.getMonitor();
    inputMeter.push (monitor.inputPeak.exchange (0.0f, std::memory_order_relaxed), static_cast<float> (dt));
    outputMeter.push (monitor.outputPeak.exchange (0.0f, std::memory_order_relaxed), static_cast<float> (dt));
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
    moon->setBounds (sky.currentMoonBounds().toNearestInt());

    grainDisplay.setBounds ((Theme::designWidth - 600) / 2, 70, 600, 200); // centred on the panel

    // Knob rows.
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (columnX (static_cast<int> (i)), 310, kColumnWidth, 152);
    for (size_t i = 0; i < 4; ++i)
        smallKnobs[i]->setBounds (columnX (static_cast<int> (i)), 474, kColumnWidth, 54);

    // Droplet switches, beside the RANDOM knobs.
    overlapButton.setBounds (columnX (4) + (kColumnWidth - 128) / 2, 482, 128, 38);
    formantButton.setBounds (columnX (5) + (kColumnWidth - 128) / 2, 482, 128, 38);
    smoothButton.setBounds (columnX (6) + (kColumnWidth - 128) / 2, 482, 128, 38);

    // Motion rows.
    for (size_t m = 0; m < 2; ++m)
    {
        auto& row = modRows[m];
        const int y = 574 + static_cast<int> (m) * 58;
        constexpr int x = 318; // the Motion panel sits right of Grain Character
        row.name.setBounds (x + 56, y, 68, 54);
        row.scope.setBounds (x + 120, y + 9, 52, 36);
        row.rate->setBounds (x + 176, y, 124, 54);
        row.depth->setBounds (x + 300, y, 124, 54);
        row.target->setBounds (x + 428, y + 8, 106, 38);
    }

    // Grain character.
    for (size_t i = 0; i < characterKnobs.size(); ++i)
        characterKnobs[i]->setBounds (46 + static_cast<int> (i) * 96, 572, 96, 116);

    // Tempo and scale. (The tempo and scale states show as tooltips; there's no
    // room for the readouts beside them.)
    syncButton.setBounds (896, 582, 100, 38);
    syncRate->setBounds (1006, 582, 110, 38);
    scaleLabel.setBounds (900, 640, 80, 38);
    scale->setBounds (986, 640, 130, 38);
    tempoReadout.setVisible (false);
    scaleReadout.setVisible (false);

    // Footer.
    freezeButton.setBounds (kLeft, 728, 150, 46);
    seedButton.setBounds (kLeft + 162, 728, 120, 46);
    seedLockButton.setBounds (kLeft + 294, 728, 172, 46);
    qualityLabel.setBounds (556, 728, 116, 46);
    for (int i = 0; i < 5; ++i)
        qualityChips[static_cast<size_t> (i)].setBounds (684 + i * 87, 730, 83, 42);

    // Levels and presets.
    smallKnobs[4]->setBounds (kLeft, 786, 134, 54);
    inputMeter.setBounds (kLeft + 136, 803, 104, 20);
    smallKnobs[5]->setBounds (kLeft + 252, 786, 134, 54);
    outputMeter.setBounds (kLeft + 388, 803, 104, 20);
    autoGainButton.setBounds (556, 794, 96, 38);
    presetLabel.setVisible (false);
    presetBar.setBounds (684, 792, 344, 42);
    saveButton.setBounds (1036, 794, 80, 38);
}

} // namespace rain
