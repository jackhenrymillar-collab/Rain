// Renders the editor to PNG frames while feeding the plugin audio, for screenshots
// and panel checks without a host. Build with -DRAIN_BUILD_SNAPSHOT=ON.
//   rain_snapshot <out-dir> [frames] [preset-index] [scale]

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const int frames = argc > 2 ? juce::String (argv[2]).getIntValue() : 1;
    const int preset = argc > 3 ? juce::String (argv[3]).getIntValue() : 1;
    const double scale = argc > 4 ? juce::String (argv[4]).getDoubleValue() : 1.0;
    outDir.createDirectory();

    std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    auto& rain = dynamic_cast<rain::RainProcessor&> (*plugin);
    rain.getPresets().loadFactory (preset);

    const double fs = 48000.0;
    const int block = 512;
    plugin->setPlayConfigDetails (2, 2, fs, block);
    plugin->prepareToPlay (fs, block);

    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
    editor->setSize (juce::roundToInt (1170 * scale), juce::roundToInt (860 * scale));

    // A plucked, rainy test signal: decaying notes at random times.
    juce::Random rng (5);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    double phase = 0.0, freq = 330.0;
    float env = 0.0f;
    auto feed = [&] (double seconds) {
        const int blocks = static_cast<int> (seconds * fs / block);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                if (rng.nextFloat() < 1.5f / static_cast<float> (fs) * 3.0f)
                {
                    env = 0.8f;
                    const double notes[] = { 220.0, 261.6, 329.6, 392.0, 440.0, 523.3 };
                    freq = notes[rng.nextInt (6)];
                }
                env *= 0.99985f;
                phase += freq / fs;
                const float s = env * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * phase));
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }
            plugin->processBlock (buffer, midi);
        }
    };

    feed (4.0);
    for (int f = 0; f < frames; ++f)
    {
        feed (1.0 / 30.0);
        dynamic_cast<rain::RainEditor&> (*editor).tick();
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        auto file = outDir.getChildFile ("frame" + juce::String (f).paddedLeft ('0', 4) + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        png.writeImageToStream (image, out);
    }

    editor.reset();
    plugin->releaseResources();
    return 0;
}
