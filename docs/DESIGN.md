# Rain: controls and behaviour

Rain is a granular texture processor built on the DSP of Mutable Instruments Beads.
Everything below describes what the Beads code does with each control, so Rain
behaves like the module. Where a plugin needs something the module does not have,
it is marked **Rain**.

## Signal path

6 dB pad → input gain → recording buffer (with feedback) → grains (or delay taps) →
dry/wet → tape wow and flutter (Dirty Tape only) → limiter → reverb → limiter →
6 dB make-up → sub-sonic filter → output gain → look-ahead limiter.

**Rain** level handling:

- **Headroom.** Audio enters Beads 6 dB down, about where a typical Eurorack signal
  sits against the module's input range, and comes back up by the same amount. Host
  audio at full scale therefore never clips inside Beads' buffer.
- **Sub-sonic filter.** A 12 Hz high-pass removes the DC and sub-sonic thumps that
  cutting low notes into short grains produces.
- **Look-ahead limiter.** Stereo-linked, 1.5 ms look-ahead, 80 ms release, ceiling
  -0.2 dBFS. It ramps the gain down ahead of a peak instead of clipping it, so loud
  grains and feedback don't crackle. Below the ceiling it does nothing.
- **Seamless changes.** Grain controls (Time, Size, Shape, Pitch, Density, the
  randoms) glide like the module's pot readers (about 5 ms) and only shape grains as
  they start: a grain plays out with the length, position, envelope and pitch it
  started with. Mix controls (Dry/Wet, Feedback, Cloud, Soften) change the sound
  already playing, so they glide for about 20 ms; Output glides 20 ms in the gain
  domain. Changing Quality fades the grains out over 8 ms, switches, and fades them
  back in, while the dry signal carries straight on. Latency is the same in every
  quality, so it never changes under the host.
- **Grain attack.** The shortest grain attack is 1.5 ms (the module's is 0.5 ms),
  which stops square and very short grains popping on low notes.

Beads runs at a fixed rate per quality mode (24, 32 or 48 kHz). Rain converts the
host's audio to that rate and back (`RainEngine`, `Resampler`) with a polyphase
windowed-sinc converter (Kaiser window, about 100 dB stopband, flat to 19 kHz at
44.1 kHz), the same class of converter used in mastering tools. When the host runs
at Beads' rate, audio passes straight through. Processing runs in the module's own
blocks of rate ÷ 4000 frames. Latency is 1.5 to 6 ms depending on the rates, and is
reported to the host.

## Grains only (Rain)

Rain is purely granular. Beads' delay mode (SIZE fully clockwise on the module) is
left out: Rain's SIZE stops just short of it.

## Grain controls

| Control | Range | What it does |
| --- | --- | --- |
| **Time** | 0 to 100% | Where in the buffer new grains start. Left is the most recent audio, right is the oldest. |
| **Size** | 0 to 100% | Grain length, 8 ms to about 3 s. **Rain:** forward lengths only, since direction has its own control (Reverse); on the module the left third of SIZE plays every grain backwards. |
| **Overlap** | on/off | **Rain:** fills the gaps. Grains keep the length Size sets; when Density makes them too sparse to overlap, they come more often (enough for two at a time, three for random grains), so the cloud never drops out. Density at noon still stops grains, and synced grains stay on the clock. |
| **Formant** | on/off | **Rain:** pitched grains keep their tone colour, so voices don't go chipmunk going up or monster going down. Each grain finds the pitch period of its source, follows its pulses cycle by cycle (so vibrato is tracked), and lays down Hann-windowed slices two periods long, centred on those pulses and read at normal speed, at the new period (PSOLA). Overlapping grains lay their slices down in step, so they add up in phase. Noisy material, where no pitch is found, is played as short slices at jittered spacing, so its colour stays put too. |
| **Reverse** | 0 to 100% | **Rain** (Grain Character): the chance each grain plays backwards. 50% mixes swells and forward grains; 100% is fully backwards. |
| **Random Pan** | 0 to 100% | **Rain** (Grain Character): pulls each grain towards a random point in the stereo field. At 0 grains spread as on the module (wider with more randomness and more overlapping grains); at 100% each lands anywhere from hard left to hard right. |
| **Shape** | 0 to 100% | Grain envelope: square at the left, a sharp attack with a decay at 1/3, a smooth bell around 2/3, and reversed (slow swell, sharp end) at the right. On the panel, the moon's glow blooms, becoming a wide halo ring near the top of the range. |
| **Pitch** | −24 to +24 st | Grain playback pitch, with the module's knob law: each way from noon the knob steps through 0, 1, 3, 4, 7, 12 and 24 semitones, holding each interval for the first third of its sixth of the travel and gliding smoothly to the next. Pitch modulation and Pitch Random are continuous on top. |
| **Random** (×4) | −100 to +100% | The small knobs under Time, Size, Shape and Pitch. On Beads these are attenuverters; with nothing patched they set randomness. Clockwise: random grains; counter-clockwise: the value jumps only occasionally; centre: off. **Rain:** the randomness is correlated over time. Each control follows a slowly wandering random value (a new level every 4 to 7 seconds, eased between) and each grain adds a small variation of its own (a fifth of the spread), so random clouds evolve rather than flicker. On the module every grain draws a fresh value. |

## Texture controls

| Control | Range | What it does |
| --- | --- | --- |
| **Density** | 0 to 100% | Centre (48 to 52%) is off. Left of centre, grains fire at regular intervals, faster as you turn left. Right of centre, they fire at random, denser as you turn right. Up to about 130 grains per second. On the panel, the rain falls harder and more stars come out the further Density is from noon. |
| **Feedback** | 0 to 100% | How much of the grain output is recorded back into the buffer. Soft-limited, so it can run hot. On the panel, it brings forked lightning (rare at first, about one strike every 3 to 4 s at full) that leaves an afterglow, and makes the stars brighter. |
| **Dry/Wet** | 0 to 100% | Beads' crossfade law (dry 0.75×, wet 1.5× at the extremes). **Rain:** the dry signal is mixed outside Beads at the host rate, so it is never resampled, quantised or band-limited, in any quality. Beads reports the gain it would have given the dry signal (including its limiters), and still feeds the dry signal to Cloud. |
| **Cloud** | 0 to 100% | Beads' reverb (Rain calls it Cloud): amount and decay together. It sits after the dry/wet mix, so it also works on the dry signal. On the panel, it takes the sky much darker, with vast nebulae, many more stars and aurora low down; the mist clears, the rain fades a little and the moon wanes to a crescent whose dark side shows its craters. |
| **Soften** | 0 to 100% | **Rain** (Grain Character): darkens and warms the grains only: light saturation, then a 12 dB/octave low-pass from 16 kHz down to about 2.5 kHz and a 12 dB/octave high-pass from 20 Hz up to about 180 Hz. Default 25% (low cut about 35 Hz). |

## Buttons

| Control | What it does |
| --- | --- |
| **Moon (bypass)** | **Rain:** click the moon to bypass Rain. The output crossfades (20 ms) to the dry input, delayed to line up with Rain's latency, while Rain keeps recording underneath. The moon wanes to a crescent and the sky goes very dark. The host's bypass switch is the same control. |
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

## Scale (Rain)

Snaps grain pitch to a scale: Chromatic, Major, Minor, Pentatonic, Minor Pentatonic,
Fifths or Octaves, with the unshifted input as the root. It applies after Pitch
Random and modulation, so random pitch spreads and pitch modulation stay in key. To
do this Rain draws the per-grain random pitch itself (the same law as Beads) and
hands Beads the snapped value. Random notes are drawn in proportion to how consonant
they are against the root (root and octaves, then the fifth, the thirds, the fourth
and sixth, then the rest), so random clouds lean towards open intervals.

## Presets (Rain)

22 factory presets, plus your own. Click the preset name for the list, the arrows
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
| Studio | **Rain:** MEDIA_STUDIO | 48 kHz, 32-bit float | 8 s | The cleanest: no noise floor or clipping in the buffer or the reverb, even with heavy feedback |

The panel shows Studio first (cleanest to dirtiest); the parameter keeps it last so
saved sessions keep their quality.

**Grain reading (Rain).** In Digital and Studio, grains are read with a band-limited
interpolator (Kaiser-windowed sinc, with the cut-off lowered in quarter-octave steps
as grains speed up), so grains pitched up two octaves don't alias. New grains also
start up to 5 ms early or late so their waveform lines up with the grain before (and
are turned down a little, since in-phase grains add up louder), and a grain whose
pitch differs from the one before glides to it over about 12 ms. Vintage and the two
tapes keep the module's 4-point Hermite reader as part of their character.

**Keeping the recording (Rain).** Changing quality carries the buffer over, frozen or
not: it is resampled to the new quality's rate (low-passed first when that is lower)
and stored in the new format, so a frozen sound keeps playing at the same pitch with
the new character. A host reset (transport stop in some hosts) or re-prepare keeps
it too; only the reverb tail is cleared.

## Levels (Rain)

| Control | Range | What it does |
| --- | --- | --- |
| **Auto Gain** | on/off | Beads' automatic input level (off by default so the dry signal stays put in a mix). |
| **Input Gain** | 0 to +32 dB | Beads' manual input gain, 1× to 40×, along the module's curve (knob position to the fourth power), so most of the travel is fine control near unity. |
| **Output Gain** | −24 to +12 dB | Default +2.5 dB, which makes up Beads' 0.75 dry gain so 0% wet with no reverb is unity. |

## Not ported (yet)

- The CV inputs and the assignable CV (the Motion modulators cover the common uses).
- The wavetable synth mode the module falls into when nothing is patched.
- Mono mode (double buffer length when only one input is patched).
- Delay mode (see above).

## Front panel

Built in `source/PluginEditor.cpp` and `source/ui/`, in Overcast's night-sky style:
preset bar, the grain display (`ui/GrainDisplay`: the buffer
waveform, Time marker and grain envelopes, drawn with Beads' own
laws for grain length, envelope and trigger rate), eight large knobs, the Random
row with Input, Output and Auto, the Motion and Tempo & Scale panels, and Freeze,
Seed, Seed Lock and Quality along the bottom. The sky behind it (`ui/Sky`) follows
Density, Shape, Feedback and Cloud, and the moon is the bypass switch. The layout is a fixed 1170 x 860
design scaled to the window.

`tools/Snapshot.cpp` (`-DRAIN_BUILD_SNAPSHOT=ON`) renders the panel to PNG frames
while feeding the plugin audio, for screenshots without a host.
