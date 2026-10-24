Hey, I think it goes without saying that please proceed at your own risk with trying this firmware out. I have NOT tried out the Legio version yet (time constraints). Also, please note that this is entirely "vibe-coded" as the kids say. 100% developed with AI assistance from Claude. I have played out live with the Pod version and its nutso great. I'm really pleased. 

# DroneVoice

A slow-moving stereo drone voice for Daisy hardware. It plays continuously from power-up: no notes and no envelopes, just a sustained tone that drifts and breathes. It has eight patch slots that survive power cycles.

It runs on two boards:

- **Electrosmith Daisy Pod.** Tested and used live.
- **Noise Engineering Legio.** Adds a 1 V/oct pitch input, a gate input and audio-input processing. This version compiles, but it has not been tried on real hardware yet.

## What's in the repo

| Path | Contents |
| --- | --- |
| `bin/` | Ready-built firmware images: `DroneVoice-Pod.bin` and `DroneVoice-Legio.bin` |
| `firmware/common/drone_engine.h` | The sound, shared by both boards |
| `firmware/pod/` | Daisy Pod controls, LEDs and patch storage |
| `firmware/legio/` | Legio controls, LEDs, pitch input and patch storage |
| `panel/` | Starter artwork for a custom 6 HP Legio panel or overlay (see [panel/README.md](panel/README.md)) |
| `docs/` | A browser version of the Pod build. It can be served with GitHub Pages (see [Browser version](#browser-version)) |

## Sound

- **Oscillators:** six oscillators, four at the root and two at a selectable interval, spread by a detune control and panned across the stereo field. Each one also drifts slightly in pitch on its own.
- **Sub:** an optional sine an octave below the root, centred.
- **Filter:** stereo 24 dB/oct ladder lowpass. A slow LFO sweeps it, a quarter-cycle apart between left and right, so the sound moves across the stereo image.
- **Motion:** the same control sets the filter sweep, the amount of pitch drift and a gentle swell in volume.
- **Reverb:** ReverbSc with an equal-power dry/wet mix.
- **Glide:** root changes are quantised to semitones but glide slowly, so a new note swells in instead of jumping.
- **Audio input (Legio only):** the stereo input can be mixed in with the drone, or processed on its own, through the same filter, motion and reverb.

```
6 oscillators + sub  ->  ladder filter L/R  ->  ReverbSc  ->  out L/R
  (+ audio in, Legio)         ^
                       motion LFO + drift
```

The settings are spread over pages. The encoder changes page, and the two knobs (or two of the knobs, on Legio) control the two settings on the current page:

| Setting | Range |
| --- | --- |
| Root | C1 to C4, in semitones |
| Detune | 0 to 30 cents |
| Cutoff | 40 Hz to 8 kHz |
| Resonance | self-oscillates near the top |
| Motion rate | about 0.005 to 0.5 Hz (one sweep every 200 s to every 2 s) |
| Motion depth | 0 to full |
| Reverb mix | dry to fully wet |
| Reverb size | short to near-infinite |
| Waveform | saw, square, triangle, sine |
| Interval of the upper pair | unison, 5th, octave, 4th, minor 3rd, major 3rd, minor 7th |
| Sub | on or off |

### Knob pickup

On both boards the knobs use soft takeover. After a page change or a patch load, a knob does nothing until you turn it past the value stored for that page, so parameters never jump. This also applies at power-up. The page LED is dim until both knobs on the page have been picked up, and bright after that.

## Daisy Pod

| Control | Action |
| --- | --- |
| Encoder turn | Change page |
| Encoder press | Cycle waveform |
| Button 1 | Cycle interval. On the Patch page: load the selected slot |
| Button 2 | Sub on/off. On the Patch page: hold for 1 second to save |

| Page | LED 1 | Knob 1 | Knob 2 |
| --- | --- | --- | --- |
| 0 | Red | Root | Detune |
| 1 | Green | Cutoff | Resonance |
| 2 | Blue | Motion rate | Motion depth |
| 3 | Purple | Reverb mix | Reverb size |
| 4 | White | Patch slot, 1 to 8 | Not used |

- **LED 1** shows the page. After a patch load it blinks green, and after a save it blinks red.
- **LED 2** breathes with the motion LFO: white when the sub is on, amber when it is off. On the Patch page it shows the selected slot's colour instead.

The Pod's audio input is not used.

## Legio

Legio has an encoder, two knobs, two three-way switches, a pitch input, a gate input and two LEDs. The layout follows the Pod's as closely as it can.

| Control | Action |
| --- | --- |
| Encoder turn | Change page |
| Encoder press | Sub on/off. On the Patch page: press to load, hold 1 second to save |
| Top and bottom knobs | The two settings on the current page |
| Left switch | Pitch input mode (see below) |
| Right switch | Audio input mode (see below) |
| Pitch input | 1 V/oct, added to the root |
| Gate input | Left switch down: samples the pitch input. Otherwise: steps to the next interval |

| Page | Left LED | Top knob | Bottom knob |
| --- | --- | --- | --- |
| 0 | Red | Root | Detune |
| 1 | Green | Cutoff | Resonance |
| 2 | Blue | Motion rate | Motion depth |
| 3 | Purple | Reverb mix | Reverb size |
| 4 | Yellow | Waveform (4 zones) | Interval (7 zones) |
| 5 | White | Patch slot, 1 to 8 | Not used |

Page 4 replaces the Pod's encoder press and button 1. Each knob there is split into zones, one per setting, and picks up once it reaches the zone of the current setting. When the gate changes the interval, the bottom knob on page 4 has to be picked up again, so it doesn't snap the interval straight back.

**Left switch: pitch input**

| Position | Behaviour |
| --- | --- |
| Up | Quantised to semitones |
| Centre | Free, for smooth glides and vibrato |
| Down | Sample and hold: the pitch updates only on each gate, quantised |

The pitch input is added to the root set on page 0. The root still glides slowly when you turn the knob, but the pitch input follows within about 20 ms, so it tracks a sequencer.

**Right switch: audio input**

| Position | Behaviour |
| --- | --- |
| Up | Drone only; the input is ignored |
| Centre | Drone and input together, both through the filter, motion and reverb |
| Down | Input only. The oscillators stop and Legio becomes a stereo filter, motion and reverb effect |

**LEDs**

- **Left LED** shows the page. After a patch load it blinks green, and after a save it blinks red.
- **Right LED** breathes with the motion LFO: white when the sub is on, amber when it is off. On the Patch page it shows the selected slot's colour.

**Calibrating the pitch input**

For accurate 1 V/oct tracking, calibrate once. You'll need a source of exactly 1 V and 3 V, such as a sequencer, a precision voltage source or a calibrated quantiser.

1. Hold the encoder while powering up Legio. Release it once both LEDs blink blue.
2. Patch exactly 1 V into the pitch input and press the encoder.
3. Both LEDs blink purple. Patch exactly 3 V and press the encoder.
4. Both LEDs blink green for a second and DroneVoice starts. The calibration is saved with the patches.

If both LEDs blink red instead, the two readings were almost the same, usually because nothing was patched. The old calibration is kept.

Until you calibrate, whatever is at the pitch input at power-up is treated as 0 V, and the input is assumed to span −2 V to +5 V, which is the range Noise Engineering gives for Legio's pitch input. Octaves may not be exact in that state.

## Patches

Both boards have eight slots, stored in the module's QSPI flash. They survive power cycles and reflashing the firmware.

1. Turn the encoder to the Patch page (white): page 4 on the Pod, page 5 on Legio.
2. Turn knob 1 (Pod) or the top knob (Legio) to choose a slot. The second LED shows its colour: red, orange, yellow, green, cyan, blue, purple, white. It is steady on the slot that is currently loaded and blinks on any other slot.
3. Load or save. On the Pod, press **button 1** to load or hold **button 2** for one second to save. On Legio, press the encoder to load or hold it for one second to save.

The last slot you loaded or saved comes back at power-up.

The slots start out as eight factory presets:

| Slot | Colour | Preset |
| --- | --- | --- |
| 1 | Red | Classic fifth |
| 2 | Orange | Glass (sine, octave, no sub) |
| 3 | Yellow | Singing filter (resonance high enough to self-oscillate) |
| 4 | Green | Dark swarm (square, minor 3rd, wide detune) |
| 5 | Cyan | Tidal (triangle, 4th, very slow deep motion) |
| 6 | Blue | Bright wash (saw, octave, long reverb) |
| 7 | Purple | Minor seventh |
| 8 | White | Low rumble (square, fifth, very low root) |

To restore the factory presets, change `kStoreVersion` in the board's main `.cpp` file and reflash. Stored data with a different version number is replaced at boot. On Legio this also clears the pitch calibration.

## Flashing the prebuilt binary

You don't need the toolchain for this. Both images are built from the source in `firmware/` and run from internal flash, so no bootloader is needed.

**Daisy Pod**

It needs a browser with WebUSB, such as Chrome or Edge; Firefox and Safari don't support WebUSB.

1. Download [`bin/DroneVoice-Pod.bin`](bin/DroneVoice-Pod.bin). On GitHub, open the file and use the download button.
2. Connect the Pod over USB and put it in DFU mode: hold BOOT, tap RESET, then release BOOT.
3. Open the Daisy Web Programmer at [flash.daisy.audio](https://flash.daisy.audio), connect to the Daisy, choose the file and program it.
4. Press RESET, or power-cycle the Pod, if it doesn't start on its own.

**Legio**

Use [`bin/DroneVoice-Legio.bin`](bin/DroneVoice-Legio.bin). Put Legio into DFU mode as described in Noise Engineering's documentation for loading custom Legio firmware, then flash it with the Daisy Web Programmer or `make program-dfu`. This replaces the Noise Engineering firmware on the module; reload theirs with their own updater when you want it back.

Saved patches live in a separate part of the flash, so reflashing doesn't erase them.

## Building the firmware

Requirements:

- The Daisy toolchain (see the [Daisy C++ dev environment guide](https://docs.daisy.audio/tutorials/cpp-dev-env)).
- A recent DaisySP. `LadderFilter` is not in older versions, and the DaisySP pinned by some DaisyExamples checkouts is too old. To update it, run `git checkout master && git pull` inside `DaisySP`, then rebuild it.
- The DaisySP-LGPL submodule, for ReverbSc. If linking fails on `-ldaisysp-lgpl`, run `git submodule update --init` inside `DaisySP`, then `make`.

The Makefiles expect this repo to be cloned next to `DaisyExamples`, in the same parent folder:

```
some-folder/
  DaisyExamples/      (contains libDaisy and DaisySP)
  daisy-drone-voice/
```

Build and flash from the board's folder:

```
cd daisy-drone-voice/firmware/pod      # or firmware/legio
make
make program-dfu
```

If libDaisy and DaisySP are somewhere else, point the build at them:

```
make LIBDAISY_DIR=/path/to/libDaisy DAISYSP_DIR=/path/to/DaisySP
```

Each build uses about 80% of internal flash. The reverb buffer (about 400 KB) is placed in SDRAM.

To refresh the prebuilt images after a change, copy `firmware/pod/build/DroneVoice.bin` to `bin/DroneVoice-Pod.bin` and `firmware/legio/build/DroneVoiceLegio.bin` to `bin/DroneVoice-Legio.bin`, then commit them.

## Browser version

`docs/index.html` plays the Pod version in a web browser. The engine in `docs/dsp.js` is a line-for-line port of the firmware's DSP: the DaisySP oscillators, ladder filter, drift generator and ReverbSc. It runs in an AudioWorklet at the same 48-sample control rate as the hardware.

The page shows a virtual Pod with the same encoder, buttons, knobs and LEDs, the same pages and soft takeover, and the same eight patch slots. It adds a scope, a readout of every parameter in real units (note names, Hz, cents), and a phones level control. Patches saved in the browser stay in that browser; they are separate from the Pod's flash.

To try it locally, serve the folder rather than opening the file directly, because browsers block AudioWorklets on `file://`:

```
cd docs
python3 -m http.server
```

Then open http://localhost:8000. Opening `index.html` directly also works, but it uses a slower fallback that runs the audio on the main thread.

To put it online, open the repo's **Settings → Pages**, choose **Deploy from a branch**, then select `main` and `/docs`. The page will appear at `https://patrickfgoddard.github.io/daisy-drone-voice/`.

Keys: `[` and `]` turn the encoder, `E` presses it, `1` and `2` press the buttons (hold `2` to save). Drag or scroll on a knob to turn it, and hold Shift for fine moves.

## Tips

- **Filter as a voice:** set resonance near max and cutoff low. The ladder self-oscillates, and the motion LFO turns it into a slow gliding sine on top of the drone.
- **Beating:** low detune with the sine or triangle waveform gives slow, gentle beating. High detune with saw gives a thick, chorused wash.
- **Very slow changes:** set motion rate fully down for changes that take minutes, then raise depth until you can hear the movement.
- **Endless sustain:** reverb size near max and a high mix gives a near-infinite tail. Changing the root then crossfades one chord into the next.
- **Sequenced drone (Legio):** set the left switch down and send a sequencer's pitch and gate. The root changes only on each gate, and the slow reverb blurs one note into the next.
- **Drone and input together (Legio):** with the right switch in the centre, feed in a percussive voice. It picks up the drone's filter motion and shares its reverb.

## Licences and credits

- The firmware links against [libDaisy](https://github.com/electro-smith/libDaisy) and [DaisySP](https://github.com/electro-smith/DaisySP) (MIT).
- ReverbSc comes from [DaisySP-LGPL](https://github.com/electro-smith/DaisySP-LGPL) and is licensed under the LGPL v2.1. `docs/dsp.js` contains a JavaScript port of it, so that port is covered by the LGPL v2.1 too; see `LICENSE-LGPL`.
- The ladder filter is by Richard van Hoesel and Infrasonic Audio (MIT), from DaisySP. `docs/dsp.js` contains a port of it.
