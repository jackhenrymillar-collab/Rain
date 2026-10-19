# Rain

A granular texture processor plugin for macOS (VST3 and AU), built with JUCE 8.
Rain runs the DSP of Mutable Instruments **Beads** (MIT licensed), so it records,
freezes, scatters and re-pitches audio the way the module does.

**Status: Phase 1.** The full Beads signal path is ported: grains, delay mode,
feedback, reverb, freeze and all four quality modes. The plugin uses JUCE's generic
editor until the night-sky UI lands. See [docs/DESIGN.md](docs/DESIGN.md) for every
control.

## Download

You don't have to build Rain yourself:

- **Latest from main:** the [`latest` release](https://github.com/jackhenrymillar-collab/Rain/releases/tag/latest)
  has `Rain-macOS.zip` (VST3 + AU, universal), rebuilt on every merge to main.
- **Any pull request:** open its *Build and test* run under Checks and download the
  `Rain-macOS` artifact (a zip containing `Rain-macOS.zip`).

Install steps, including the one-time Gatekeeper command, are in
[docs/INSTALL.md](docs/INSTALL.md).

## Building on macOS

Requirements: Xcode 15 or later (command line tools are enough) and CMake 3.22+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
ctest --test-dir build --output-on-failure
```

JUCE and Catch2 are downloaded automatically on the first configure. To use a local
JUCE checkout instead, add `-DRAIN_JUCE_DIR=/path/to/JUCE`.

The build produces a universal (Apple Silicon + Intel) VST3, AU and Standalone app in
`build/Rain_artefacts/Release/`, and copies the VST3 and AU into
`~/Library/Audio/Plug-Ins/` so your DAW can find them. In Logic, rescan with
Settings › Plug-in Manager if Rain doesn't appear.

To work in Xcode: `cmake -S . -B build-xcode -G Xcode`.

## Layout

```
source/
  PluginProcessor.*   audio callback, parameter tree, state save/load
  Parameters.*        parameter IDs and ranges (IDs must never change)
  dsp/                plain C++ DSP with no JUCE dependency
    RainEngine.*      runs Beads at its own rate inside the host, maps the controls
    InputGain.*       Beads' input gain and soft clipping (adapted)
    Resampler.h       streaming cubic resampler for host rate <-> Beads rate
    Blocks.h          filters and smoothers shared with Overcast
third_party/mutable/  Beads and stmlib, unchanged from upstream (MIT)
tests/                Catch2 tests for the DSP
docs/DESIGN.md        what every control does
```

## What the tests check

- Beads runs at the right internal rate for each quality mode, with small, reported latency
- At 0% wet with no reverb the dry signal comes through at unity (44.1, 48 and 96 kHz)
- Silence in gives silence out in every mode
- Grains play back the input when Density is up
- Freeze holds the buffer after the input stops; without it the buffer empties
- No NaNs and no output above full scale under 8 seconds of random automation, at 44.1 to 96 kHz
- The resampler is exact at 1:1 and keeps a tone's level through a 44.1 → 24 → 44.1 kHz round trip

CI also runs `auval` and `pluginval` (strictness 10) on every pull request.

## Credits

Beads DSP and stmlib © Émilie Gillet, MIT License. See
[third_party/mutable](third_party/mutable/README.md). Rain is not affiliated with
Mutable Instruments.
