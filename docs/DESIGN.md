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

## Grain controls

| Control | Range | What it does |
| --- | --- | --- |
| **Time** | 0 to 100% | Where in the buffer new grains start. Left is the most recent audio, right is the oldest. |
| **Size** | 0 to 100% | Grain length and direction. Left of about 1/3, grains play **backwards**: fully left they are long (up to 4 s), getting shorter towards 1/3. Right of 1/3 they play forwards, getting longer again towards the right. At the very end of the travel (above 99%) Rain switches into Beads' **delay mode**: Time sets the delay, Shape the tap pattern, Density the clock. |
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

- The CV inputs and the assignable CV. A future modulation section could feed them.
- The wavetable synth mode the module falls into when nothing is patched.
- The SEED gate input and clock divider. A host-tempo sync could drive it.
- Mono mode (double buffer length when only one input is patched).

## Front panel

Built in `source/PluginEditor.cpp` and `source/ui/`. Presets, A/B and Save from the
mockup are not built yet; the preset bar shows "Init" for now. The mockup is at <https://claude.ai/artifact/RHF73eDwpbg3wDwLzexysG>. It follows
Overcast's night-sky style: serif wordmark with a raindrop, preset bar with A/B and
Save, glowing navy knobs (Time, Size, Shape, Pitch, Density, Feedback, Reverb,
Dry/Wet), four small Random knobs, Input and Output, Quality chips that change the big
title, and Freeze and Seed pill buttons.
