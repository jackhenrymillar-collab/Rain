#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace rain
{

// Factory and user presets. A preset sets every sound parameter; it leaves the
// performance and level controls alone (FREEZE, SEED, INPUT and AUTO), since those
// depend on the moment and the source rather than the sound.
//
// User presets are small XML files in the user's presets folder
// (macOS: ~/Library/Audio/Presets/Overcast Audio/Rain).
class PresetManager final : private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState&);
    ~PresetManager() override;

    static constexpr const char* fileExtension = ".rainpreset";

    int numFactory() const noexcept;
    juce::String factoryName (int index) const;
    void loadFactory (int index);
    int currentFactoryIndex() const; // -1 when the current preset is a user preset

    static juce::File userFolder();
    juce::StringArray userNames() const;
    bool saveUser (const juce::String& name);
    bool loadUser (const juce::String& name);
    bool deleteUser (const juce::String& name);

    // Steps through factory presets, then user presets, wrapping round.
    void step (int delta);

    juce::String currentName() const;
    bool isUser() const;
    bool isModified() const noexcept { return modified.load(); }

    // Session save/restore keeps the name and whether it was edited.
    void writeToState (juce::ValueTree& stateCopy) const;
    void readFromState (const juce::ValueTree& restored);

    static bool affectsPreset (const juce::String& paramID);

private:
    void parameterChanged (const juce::String& paramID, float newValue) override;
    void applyValues (const juce::StringPairArray& values);
    void setCurrent (const juce::String& name, bool user);

    juce::AudioProcessorValueTreeState& state;
    std::atomic<bool> modified { false };
    std::atomic<bool> loading { false };
};

} // namespace rain
