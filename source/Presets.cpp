#include "Presets.h"

#include "Parameters.h"

namespace rain
{

using namespace juce;

namespace
{
    struct FactoryPreset
    {
        const char* name;
        // Parameter values in their display units (percent, semitones, Hz, choice
        // index, 0/1). Anything not listed takes its default.
        std::vector<std::pair<const char*, float>> values;
    };

    // Choice indices, for readability.
    namespace Q { constexpr float vintage = 1, cleanTape = 2, dirtyTape = 3; }
    namespace S { constexpr float major = 2, penta = 4, minorPenta = 5, fifths = 6, octaves = 7; }
    namespace R { constexpr float quarter = 0, eighth = 1, sixteenth = 2, eighthT = 4; }
    namespace T { constexpr float time = 1, size = 2, shape = 3, pitch = 4, density = 5, feedback = 6, reverb = 8; }

    namespace P = ParamID;

    const FactoryPreset kFactory[] = {
        { "Init", {} },
        { "Window Pane", { { P::time, 40 }, { P::size, 38 }, { P::density, 72 }, { P::timeRandom, 25 },
                           { P::reverb, 45 }, { P::dryWet, 55 } } },
        { "Drizzle", { { P::time, 30 }, { P::size, 17 }, { P::shape, 22 }, { P::density, 82 },
                       { P::timeRandom, 60 }, { P::reverb, 50 }, { P::dryWet, 60 } } },
        { "Downpour", { { P::size, 66 }, { P::shape, 58 }, { P::density, 96 }, { P::timeRandom, 70 },
                        { P::sizeRandom, 20 }, { P::feedback, 20 }, { P::reverb, 50 }, { P::dryWet, 70 } } },
        { "Reverse Rain", { { P::time, 60 }, { P::size, 35 }, { P::reverse, 100 }, { P::shape, 62 }, { P::density, 80 },
                            { P::timeRandom, 30 }, { P::reverb, 40 }, { P::dryWet, 60 } } },
        { "Octave Mist", { { P::size, 76 }, { P::shape, 64 }, { P::density, 90 }, { P::pitchRandom, 45 },
                           { P::scale, S::octaves }, { P::feedback, 35 }, { P::reverb, 70 }, { P::dryWet, 60 } } },
        { "Pentatonic Shower", { { P::size, 45 }, { P::density, 88 }, { P::pitchRandom, 70 }, { P::timeRandom, 40 },
                                 { P::scale, S::penta }, { P::reverb, 55 }, { P::dryWet, 65 } } },
        { "Minor Chimes", { { P::size, 38 }, { P::shape, 20 }, { P::density, 84 }, { P::pitchRandom, 60 },
                            { P::scale, S::minorPenta }, { P::pitch, 12 }, { P::reverb, 65 }, { P::dryWet, 55 } } },
        { "Tidal Drift", { { P::size, 60 }, { P::density, 86 }, { P::reverb, 50 }, { P::dryWet, 60 },
                           { P::mod1Target, T::time }, { P::mod1Rate, 0.08f }, { P::mod1Depth, 45 },
                           { P::mod2Target, T::size }, { P::mod2Rate, 0.3f }, { P::mod2Depth, 45 } } },
        { "Wandering Choir", { { P::size, 82 }, { P::shape, 66 }, { P::density, 92 }, { P::scale, S::major },
                               { P::mod2Target, T::pitch }, { P::mod2Rate, 0.4f }, { P::mod2Depth, 60 },
                               { P::mod1Target, T::reverb }, { P::mod1Rate, 0.05f }, { P::mod1Depth, 30 },
                               { P::reverb, 60 }, { P::dryWet, 70 } } },
        { "Velvet Cloud", { { P::overlap, 1 }, { P::size, 67 }, { P::shape, 62 }, { P::density, 88 }, { P::timeRandom, 35 },
                            { P::soften, 65 }, { P::reverb, 55 }, { P::dryWet, 70 } } },
        { "Backwards Choir", { { P::overlap, 1 }, { P::size, 60 }, { P::shape, 70 }, { P::density, 86 }, { P::reverse, 50 },
                               { P::pitchRandom, 55 }, { P::scale, S::major }, { P::soften, 45 }, { P::reverb, 65 }, { P::dryWet, 75 } } },
        { "Soft Focus", { { P::overlap, 1 }, { P::size, 50 }, { P::density, 30 }, { P::shape, 58 }, { P::timeRandom, 20 },
                          { P::soften, 50 }, { P::reverb, 40 }, { P::dryWet, 60 } } },
        { "Clocked Drops", { { P::sync, 1 }, { P::syncRate, R::sixteenth }, { P::density, 9 }, { P::size, 26 },
                             { P::shape, 20 }, { P::time, 30 }, { P::reverb, 30 }, { P::dryWet, 55 } } },
        { "Stutter Grid", { { P::sync, 1 }, { P::syncRate, R::sixteenth }, { P::density, 2 }, { P::size, 57 },
                            { P::shape, 2 }, { P::time, 20 }, { P::reverb, 15 }, { P::dryWet, 75 } } },
        { "Triplet Hail", { { P::sync, 1 }, { P::syncRate, R::eighthT }, { P::density, 70 }, { P::size, 20 },
                            { P::pitchRandom, 50 }, { P::scale, S::fifths }, { P::reverb, 35 }, { P::dryWet, 60 } } },
        { "Sampler Hail", { { P::quality, Q::vintage }, { P::size, 7 }, { P::shape, 0 }, { P::density, 20 },
                            { P::timeRandom, 50 }, { P::reverb, 25 }, { P::dryWet, 60 } } },
        { "Tape Echo", { { P::mode, 1 }, { P::quality, Q::dirtyTape }, { P::density, 20 }, { P::feedback, 55 },
                         { P::reverb, 15 }, { P::dryWet, 40 } } },
        { "Quarter Echoes", { { P::mode, 1 }, { P::sync, 1 }, { P::syncRate, R::quarter }, { P::density, 45 }, { P::quality, Q::cleanTape },
                              { P::feedback, 50 }, { P::shape, 60 }, { P::reverb, 35 }, { P::dryWet, 45 } } },
        { "Eighth Swells", { { P::mode, 1 }, { P::sync, 1 }, { P::syncRate, R::eighth }, { P::density, 45 }, { P::feedback, 62 }, { P::shape, 80 },
                             { P::mod1Target, T::feedback }, { P::mod1Rate, 0.1f }, { P::mod1Depth, 20 },
                             { P::reverb, 55 }, { P::dryWet, 45 } } },
        { "Frozen Lake", { { P::time, 70 }, { P::size, 91 }, { P::shape, 66 }, { P::density, 88 },
                           { P::timeRandom, 30 }, { P::pitchRandom, 15 }, { P::reverb, 75 }, { P::dryWet, 80 },
                           { P::mod2Target, T::shape }, { P::mod2Rate, 0.2f }, { P::mod2Depth, 25 } } },
        { "Night Drone", { { P::quality, Q::cleanTape }, { P::size, 96 }, { P::shape, 60 }, { P::density, 94 },
                           { P::pitch, -12 }, { P::feedback, 45 }, { P::reverb, 80 }, { P::dryWet, 85 },
                           { P::mod1Target, T::density }, { P::mod1Rate, 0.03f }, { P::mod1Depth, 8 } } },
    };

    constexpr const char* kNameProperty = "presetName";
    constexpr const char* kUserProperty = "presetUser";
    constexpr const char* kModifiedProperty = "presetModified";

    File fileFor (const String& name)
    {
        return PresetManager::userFolder().getChildFile (File::createLegalFileName (name) + PresetManager::fileExtension);
    }
}

PresetManager::PresetManager (AudioProcessorValueTreeState& s) : state (s)
{
    for (auto* p : state.processor.getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            if (affectsPreset (withId->paramID))
                state.addParameterListener (withId->paramID, this);
    setCurrent (kFactory[0].name, false);
}

PresetManager::~PresetManager()
{
    for (auto* p : state.processor.getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            state.removeParameterListener (withId->paramID, this);
}

bool PresetManager::affectsPreset (const String& id)
{
    return id != ParamID::freeze && id != ParamID::seed && id != ParamID::inputGain && id != ParamID::autoGain;
}

int PresetManager::numFactory() const noexcept
{
    return static_cast<int> (std::size (kFactory));
}

String PresetManager::factoryName (int index) const
{
    return isPositiveAndBelow (index, numFactory()) ? String (kFactory[index].name) : String();
}

void PresetManager::parameterChanged (const String&, float)
{
    if (! loading.load())
        modified = true;
}

void PresetManager::applyValues (const StringPairArray& values)
{
    loading = true;
    for (auto* p : state.processor.getParameters())
    {
        auto* ranged = dynamic_cast<RangedAudioParameter*> (p);
        if (ranged == nullptr || ! affectsPreset (ranged->paramID))
            continue;
        float normalised = ranged->getDefaultValue();
        if (values.containsKey (ranged->paramID))
            normalised = ranged->convertTo0to1 (values[ranged->paramID].getFloatValue());
        if (std::abs (ranged->getValue() - normalised) > 1.0e-6f)
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (normalised);
            ranged->endChangeGesture();
        }
    }
    loading = false;
    modified = false;
}

void PresetManager::setCurrent (const String& name, bool user)
{
    state.state.setProperty (kNameProperty, name, nullptr);
    state.state.setProperty (kUserProperty, user, nullptr);
}

void PresetManager::loadFactory (int index)
{
    if (! isPositiveAndBelow (index, numFactory()))
        return;
    StringPairArray values;
    for (const auto& [id, value] : kFactory[index].values)
        values.set (id, String (value));
    applyValues (values);
    setCurrent (kFactory[index].name, false);
}

int PresetManager::currentFactoryIndex() const
{
    if (isUser())
        return -1;
    const auto name = currentName();
    for (int i = 0; i < numFactory(); ++i)
        if (name == kFactory[i].name)
            return i;
    return -1;
}

File PresetManager::userFolder()
{
   #if JUCE_MAC
    return File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Overcast Audio/Rain");
   #else
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Overcast Audio/Rain/Presets");
   #endif
}

StringArray PresetManager::userNames() const
{
    StringArray names;
    for (const auto& f : userFolder().findChildFiles (File::findFiles, false, String ("*") + fileExtension))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool PresetManager::saveUser (const String& rawName)
{
    const auto name = File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;

    XmlElement xml ("RainPreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", 1);
    for (auto* p : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<RangedAudioParameter*> (p))
            if (affectsPreset (ranged->paramID))
            {
                auto* e = xml.createNewChildElement ("Param");
                e->setAttribute ("id", ranged->paramID);
                e->setAttribute ("value", ranged->convertFrom0to1 (ranged->getValue()));
            }

    if (! userFolder().createDirectory() || ! xml.writeTo (fileFor (name)))
        return false;
    setCurrent (name, true);
    modified = false;
    return true;
}

bool PresetManager::loadUser (const String& name)
{
    auto xml = XmlDocument::parse (fileFor (name));
    if (xml == nullptr || ! xml->hasTagName ("RainPreset"))
        return false;
    StringPairArray values;
    for (auto* e : xml->getChildWithTagNameIterator ("Param"))
        values.set (e->getStringAttribute ("id"), e->getStringAttribute ("value"));
    applyValues (values);
    setCurrent (name, true);
    return true;
}

bool PresetManager::deleteUser (const String& name)
{
    const bool ok = fileFor (name).deleteFile();
    if (ok && isUser() && currentName() == name)
        modified = true; // keep the sound, but it no longer matches a saved preset
    return ok;
}

void PresetManager::step (int delta)
{
    // One list: factory presets, then user presets.
    StringArray all;
    for (int i = 0; i < numFactory(); ++i)
        all.add (kFactory[i].name);
    const auto user = userNames();
    all.addArray (user);

    int current = isUser() ? numFactory() + user.indexOf (currentName()) : currentFactoryIndex();
    if (current < 0)
        current = 0;
    const int next = ((current + delta) % all.size() + all.size()) % all.size();
    if (next < numFactory())
        loadFactory (next);
    else
        loadUser (all[next]);
}

String PresetManager::currentName() const
{
    return state.state.getProperty (kNameProperty, kFactory[0].name).toString();
}

bool PresetManager::isUser() const
{
    return state.state.getProperty (kUserProperty, false);
}

void PresetManager::writeToState (ValueTree& copy) const
{
    copy.setProperty (kModifiedProperty, modified.load(), nullptr);
}

void PresetManager::readFromState (const ValueTree& restored)
{
    modified = static_cast<bool> (restored.getProperty (kModifiedProperty, false));
}

} // namespace rain
