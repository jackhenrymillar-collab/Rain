// Renders the editor to PNG frames while feeding the plugin audio, for screenshots
// and panel checks without a host. Build with -DRAIN_BUILD_SNAPSHOT=ON.
//   rain_snapshot <out-dir> [frames] [preset-index] [scale] [bypass|-] [param=value ...]
// Values are normalised 0 to 1, e.g. feedback=1 reverb=0.85.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdlib>
#include <functional>
#include <map>
#include <typeinfo>

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
    // Optional 5th argument: "bypass" renders the panel with the moon switched off.
    if (argc > 5 && juce::String (argv[5]) == "bypass")
        rain.getState().getParameter (rain::ParamID::bypass)->setValueNotifyingHost (1.0f);
    for (int i = 6; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (auto* param = rain.getState().getParameter (arg.upToFirstOccurrenceOf ("=", false, false)))
            param->setValueNotifyingHost (arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
    }

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

    // RAIN_BENCH=<pixel scale>: time drawing the whole panel each frame instead of
    // saving PNGs, at that pixel scale (2 is a Retina screen), and the costliest parts.
    if (const char* bench = std::getenv ("RAIN_BENCH"))
    {
        const float px = juce::String (bench).getFloatValue();
        auto& ed = dynamic_cast<rain::RainEditor&> (*editor);
        std::map<juce::String, double> total;
        for (int f = 0; f < 120; ++f) // let the sky settle on the settings first
        {
            ed.tick();
            ed.createComponentSnapshot ({ 0, 0, 8, 8 }, true, px);
        }
        for (int f = 0; f < frames; ++f)
        {
            feed (1.0 / 30.0);
            ed.tick();
            auto time = [&] (const juce::String& name, juce::Component& c) {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                auto image = c.createComponentSnapshot (c.getLocalBounds(), true, px);
                total[name] += juce::Time::getMillisecondCounterHiRes() - t0;
            };
            time ("whole panel", ed);
            std::function<void (juce::Component&)> each = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (! child->isVisible())
                        continue;
                    juce::String name = typeid (*child).name();
                    if (child->getWidth() * child->getHeight() > 20000)
                        time (name + " " + child->getBounds().toString(), *child);
                    each (*child);
                }
            };
            if (std::getenv ("RAIN_BENCH_PARTS"))
                each (ed);
        }
        for (const auto& [name, ms] : total)
            std::printf ("%8.2f ms/frame  %s\n", ms / frames, name.toRawUTF8());
        editor.reset();
        plugin->releaseResources();
        return 0;
    }

    for (int f = 0; f < frames; ++f)
    {
        feed (1.0 / 30.0);
        dynamic_cast<rain::RainEditor&> (*editor).tick();
        // RAIN_PIXELS=2 saves at twice the size, as a Retina screen would draw it.
        const float pixels = std::getenv ("RAIN_PIXELS") != nullptr ? juce::String (std::getenv ("RAIN_PIXELS")).getFloatValue() : 1.0f;
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, pixels);
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
