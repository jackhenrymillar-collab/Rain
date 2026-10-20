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
  sets the length. They also add `Parameters::random_pan` (default 0, no effect),
  which pulls each grain's pan towards a random position.
- `beads/dsp/processor.h/.cc` add `MixParameters::soften` (default 0, no effect): a
  light saturation, 12 dB/octave low-pass and gentle 12 dB/octave high-pass on the grains
  before the dry/wet mix.
- Sound quality additions, all off by default (upstream behaviour):
  - `processor.h/.cc`: `MixParameters::direct_gain` leaves the dry signal out of the
    output (it still feeds the reverb) and reports its gain per sample, so Rain can mix
    its own untouched copy; `MEDIA_STUDIO`, a 48 kHz, 32-bit float buffer (8 s, in
    buffers Rain supplies) with a 32-bit float reverb.
  - `fx/reverb.h` is templated on its storage format (`Reverb` is the upstream 16-bit
    one) and can take the direct part of its output from a separate buffer.
    `limiter.h` can report its gain per sample.
  - `delay_line.h` adds `RESOLUTION_FLOAT`, a band-limited `ReadSinc` and `ReadMono`;
    `granular_processor.h` takes the bank size as a template argument.
  - `granular_player.h` (`Parameters`), `delay_granular_player.h` and
    `delay_grain.h` add the grain options `clean_reader` (band-limited reading),
    `formant` (PSOLA pitch shifting on tracked pitch marks, slices in step with
    the previous grain), `phase_align` and `pitch_glide`.
  - `rain_sinc.h` is new (Rain's own): the band-limited interpolation tables.
  - `delay_line.h` (`History`), `granular_processor.h` and `processor.h/.cc`
    (`ExportHistory`, `BeginImport`, `ImportHistory`, `history_rate`,
    `HistoryRateFor`, `HistoryCapacityFor`) let Rain copy the recording out and back
    in, in slices, so it survives a change of quality. `fx/reverb.h`
    (`dry_level`) and `Processor::Configure` hand the reverb's direct level on, so
    the dry signal doesn't dip when quality changes.
  - `granular_player.h` (`new_grain`) starts a new grain on a voice that has
    finished, when there is one, rather than cutting off a voice still fading out.

## License

The code is © Émilie Gillet and released under the MIT License (see `LICENSE`). Each
file keeps its original copyright header. Keep it that way when updating.

Mutable Instruments' hardware and panel designs are CC-BY-SA 3.0 and are not used here.
"Beads" and "Mutable Instruments" are their names, so Rain's product name, look and
marketing must stay its own.

## Updating

Copy the same folders from a newer upstream checkout, keep the file list in
`CMakeLists.txt` (`mutable_beads`) in sync, and run the tests.
