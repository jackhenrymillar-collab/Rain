# Rain: controls and behaviour

Rain is a granular texture processor built on the DSP of Mutable Instruments Beads.
Everything below describes what the Beads code does with each control, so Rain
behaves like the module. Where a plugin needs something the module does not have,
it is marked **Rain**.

## Signal path

Input gain → recording buffer (with feedback) → grains (or delay taps) → dry/wet →
tape wow and flutter (Dirty Tape only) → limiter → reverb → limiter → output gain.

Beads runs at a fixed rate per quality mode (24, 32 or 48 kHz). Rain converts the
host's audio to that rate and back (`RainEngine`), processing in the module's own
blocks of rate ÷ 4000 frames. This adds about 20 to 40 samples of latency, which is
reported to the host.

## Mode (Rain)

| Mode | What it is |
| --- | --- |
| **Grains** | Beads' granular mode. |
| **Delay** | Beads' delay mode, which the module reaches by turning SIZE fully clockwise. Density sets the delay time (longest at noon, shorter either side; right of noon adds a second tap), Feedback the repeats, Shape the envelope of the delay taps. While frozen, Time picks which slice of the buffer loops. Synced, Density picks a ratio of the clock. |

## Grain controls

| Control | Range | What it does |
| --- | --- | --- |
| **Time** | 0 to 100% | Where in the buffer new grains start. Left is the most recent audio, right is the oldest. |
| **Size** | 0 to 100% | Grain length and direction. Left of about 1/3, grains play **backwards**: fully left they are long (up to 4 s), getting shorter towards 1/3. Right of 1/3 they play forwards, getting longer again towards the right. Not used in Delay mode. |
| **Shape** | 0 to 100% | Grain envelope: square at the left, a sharp attack with a decay at 1/3, a smooth bell around 2/3, and reversed (slow swell, sharp end) at the right. |
| **Pitch** | −24 to +24 st | Grain playback pitch. **Rain:** continuous; on the module the knob snaps to 0, 1, 3, 4, 7, 12 and 24 semitones. |
| **Random** (×4) | −100 to +100% | The small knobs under Time, Size, Shape and Pitch. On Beads these are attenuverters; with nothing patched they set randomness. Clockwise: a new random value for every grain. Counter-clockwise: the value jumps only occasionally. Centre: off. |

## Texture controls

| Control | Range | What it does |
| --- | --- | --- |
| **Density** | 0 to 100% | Centre (48 to 52%) is off. Left of centre, grains fire at regular intervals, faster as you turn left. Right of centre, they fire at random, denser as you turn right. Up to about 130 grains per second. |
| **Feedback** | 0 to 100% | How much of the grain output is recorded back into the buffer. Soft-limited, so it can run hot. |
| **Dry/Wet** | 0 to 100% | Beads' crossfade law (dry 0.75×, wet 1.5× at the extremes). |
| **Reverb** | 0 to 100% | Amount and decay together. The reverb sits after the dry/wet mix, so it also works on the dry signal. |

## Buttons

| Control | What it does |
| --- | --- |
| **Freeze** | Stops recording. Grains keep playing the held buffer. |
| **Seed** | Fires a grain while held. |
| **Seed Lock** | **Rain:** on by default, which is the module's power-up state: grains run on their own at the Density rate. Turn it off and grains only play while Seed is held. (On the module, a long press of SEED toggles this.) |

## Motion (Rain)

Two modulators stand in for the module's CV inputs. Each has a **Rate** (0.01 to
10 Hz, shown as a period below 1 Hz), a bipolar **Depth** and a **Target**: Time,
Size, Shape, Pitch, Density, Feedback, Dry/Wet or Reverb. Full depth sweeps half the
knob's travel either way (an octave either way for Pitch).

- **Wave**: a sine LFO.
- **Drift**: smooth random movement, a new random level each cycle, eased between.

The panel shows a gold ring on any knob a modulator is moving, from its setting to
where it is now, and a small trace of each modulator.

## Tempo sync (Rain)

**Sync** patches a clock from the host tempo into Beads' SEED input, as you would on
the module with a clock in the SEED jack. The clock note length is set beside it
(1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T). With the host stopped, the clock free-runs at
the host's tempo.

- Grains: Density divides the clock left of noon (every 1 to 16 pulses; the panel
  names the resulting note), fires on every pulse at noon, and picks pulses at
  random right of noon. Time snaps to whole clock steps back in the buffer.
- Delay: Density picks a ratio of the clock for the delay time.

## Scale (Rain)

Snaps grain pitch to a scale: Chromatic, Major, Minor, Pentatonic, Minor Pentatonic,
Fifths or Octaves, with the unshifted input as the root. It applies after Pitch
Random and modulation, so random pitch spreads and pitch modulation stay in key. To
do this Rain draws the per-grain random pitch itself (the same law as Beads) and
hands Beads the snapped value.

## Presets (Rain)

19 factory presets, plus your own. Click the preset name for the list, the arrows
to step through, and Save to store the current sound. User presets are XML files
in `~/Library/Audio/Presets/Overcast Audio/Rain`. A gold dot by the name means the
sound has been edited since the preset was loaded. Presets leave Freeze, Seed,
Input and Auto alone. Hosts also see the factory presets as programs.

## Quality

| Mode | Beads media | Rate and depth | Buffer (stereo) | Character |
| --- | --- | --- | --- | --- |
| Digital | MEDIA_DIGITAL | 48 kHz, 16-bit | 4 s | Clean, bright reverb |
| Vintage | MEDIA_VINTAGE_DIGITAL | 32 kHz, 12-bit | 8 s | Early-sampler grit, darker reverb |
| Clean Tape | MEDIA_CLEAN_TAPE | 24 kHz inside, 12-bit | about 10 s | Soft saturation that rises with input gain |
| Dirty Tape | MEDIA_DIRTY_TAPE | 24 kHz, 8-bit μ-law | about 16 s | Hiss, wow and flutter, always soft clipped |

## Levels (Rain)

| Control | Range | What it does |
| --- | --- | --- |
| **Auto Gain** | on/off | Beads' automatic input level (off by default so the dry signal stays put in a mix). |
| **Input Gain** | 0 to +32 dB | Beads' manual input gain, 1× to 40×. |
| **Output Gain** | −24 to +12 dB | Default +2.5 dB, which makes up Beads' 0.75 dry gain so 0% wet with no reverb is unity. |

## Not ported (yet)

- The CV inputs and the assignable CV (the Motion modulators cover the common uses).
- The wavetable synth mode the module falls into when nothing is patched.
- Mono mode (double buffer length when only one input is patched).

## Front panel

Built in `source/PluginEditor.cpp` and `source/ui/`, in Overcast's night-sky style:
preset bar, Grains/Delay switch, the grain display (`ui/GrainDisplay`: the buffer
waveform, Time marker, grain envelopes and the density lane, drawn with Beads' own
laws for grain length, envelope and trigger rate), eight large knobs, the Random
row with Input, Output and Auto, the Motion and Tempo & Scale panels, and Freeze,
Seed, Seed Lock and Quality along the bottom. The layout is a fixed 1170 x 860
design scaled to the window.

`tools/Snapshot.cpp` (`-DRAIN_BUILD_SNAPSHOT=ON`) renders the panel to PNG frames
while feeding the plugin audio, for screenshots without a host.
