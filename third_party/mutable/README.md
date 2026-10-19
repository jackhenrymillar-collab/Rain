# Mutable Instruments code

Rain's sound comes from the DSP of **Beads** by Mutable Instruments (Émilie Gillet),
published at <https://github.com/pichenettes/eurorack>.

| Folder | Upstream | Commit |
| --- | --- | --- |
| `beads/dsp/`, `beads/resources.*` | `pichenettes/eurorack`, `beads/` | latest `master` on 2026-10-19 |
| `stmlib/dsp/`, `stmlib/utils/`, `stmlib/stmlib.h` | `pichenettes/stmlib` | `e3bd7c9` (the eurorack submodule pin) |

The files are unchanged from upstream, except that:

- Beads' wavetable synth mode (`synth.*`, `wavetable_*.h`) is left out because Rain
  does not use it.
- `beads/dsp/grain_envelope.h` reads its minimum grain attack from
  `BEADS_MIN_ATTACK_SECONDS` (default 0.5 ms, as upstream). Rain sets 1.5 ms in
  `CMakeLists.txt` so square and very short grains don't pop on low notes.
- `beads/dsp/granular_player.h` and `delay_granular_player.h` add
  `Parameters::reverse_probability`. Left at its default (-1) Beads behaves as
  upstream; Rain sets it so each grain's direction is drawn at random and SIZE only
  sets the length.
- `beads/dsp/processor.h/.cc` add `MixParameters::soften` (default 0, no effect): a
  light saturation and 12 dB/octave low-pass on the grains before the dry/wet mix.

## License

The code is © Émilie Gillet and released under the MIT License (see `LICENSE`). Each
file keeps its original copyright header. Keep it that way when updating.

Mutable Instruments' hardware and panel designs are CC-BY-SA 3.0 and are not used here.
"Beads" and "Mutable Instruments" are their names, so Rain's product name, look and
marketing must stay its own.

## Updating

Copy the same folders from a newer upstream checkout, keep the file list in
`CMakeLists.txt` (`mutable_beads`) in sync, and run the tests.
