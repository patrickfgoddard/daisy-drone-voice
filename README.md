# DroneVoice

A slow-moving stereo drone voice for the Electrosmith Daisy Pod. It plays continuously from power-up: no notes and no envelopes, just a sustained tone that drifts and breathes. It also has eight patch slots that survive power cycles.

The repo has three parts:

- **`bin/DroneVoice.bin`** is a ready-built firmware image. You can flash it straight from a browser without installing anything (see [Flashing the prebuilt binary](#flashing-the-prebuilt-binary)).
- **`firmware/`** contains the C++ firmware for the Daisy Pod.
- **`docs/`** contains a browser version that runs the same DSP, ported to JavaScript, with the same controls and patch slots. It can be served with GitHub Pages (see [Browser version](#browser-version)).

## Sound

- **Oscillators:** six oscillators, four at the root and two at a selectable interval, spread by a detune control and panned across the stereo field. Each one also drifts slightly in pitch on its own.
- **Sub:** an optional sine an octave below the root, centred.
- **Filter:** stereo 24 dB/oct ladder lowpass. A slow LFO sweeps it, a quarter-cycle apart between left and right, so the sound moves across the stereo image.
- **Motion:** the same control sets the filter sweep, the amount of pitch drift and a gentle swell in volume.
- **Reverb:** ReverbSc with an equal-power dry/wet mix.
- **Glide:** root changes are quantised to semitones but glide slowly, so a new note swells in instead of jumping.

```
6 oscillators + sub  ->  ladder filter L/R  ->  ReverbSc  ->  out L/R
                              ^
                       motion LFO + drift
```

## Controls

| Control | Action |
| --- | --- |
| Encoder turn | Change page |
| Encoder press | Cycle waveform: saw, square, triangle, sine |
| Button 1 | Cycle interval of the upper pair: 5th, octave, 4th, minor 3rd, major 3rd, minor 7th, unison. On the Patch page: load the selected slot |
| Button 2 | Sub oscillator on/off. On the Patch page: hold for 1 second to save |

### Pages

| Page | LED 1 | Knob 1 | Knob 2 |
| --- | --- | --- | --- |
| 0 | Red | Root pitch, C1 to C4 | Detune spread, 0 to 30 cents |
| 1 | Green | Filter cutoff, 40 Hz to 8 kHz | Resonance (self-oscillates near max) |
| 2 | Blue | Motion rate, about 0.005 to 0.5 Hz | Motion depth |
| 3 | Purple | Reverb mix | Reverb size |
| 4 | White | Patch slot, 1 to 8 | Not used |

### LEDs

- **LED 1** shows the current page. It is dim until both knobs on the page have been picked up (see below), and bright after that. After a load it blinks green, and after a save it blinks red.
- **LED 2** breathes with the motion LFO: white when the sub is on, amber when it is off. On the Patch page it shows the selected slot's colour instead.

### Knob pickup

The knobs use soft takeover. After a page change or a patch load, a knob does nothing until you turn it past the value stored for that page, so parameters never jump. This also applies at power-up.

## Patches

There are eight slots, stored in the Pod's QSPI flash, so they survive power cycles and reflashing the firmware.

1. Turn the encoder to page 4. LED 1 turns white.
2. Turn knob 1 to choose a slot. LED 2 shows its colour: red, orange, yellow, green, cyan, blue, purple, white. It is steady on the slot that is currently loaded and blinks on any other slot.
3. Press **button 1** to load the slot, or hold **button 2** for one second to save the current sound into it.

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

To restore the factory presets, change `kStoreVersion` in `DroneVoice.cpp` and reflash. Stored data with a different version number is replaced at boot.

## Flashing the prebuilt binary

You don't need the toolchain for this. It needs a browser with WebUSB, such as Chrome or Edge; Firefox and Safari don't support WebUSB.

1. Download [`bin/DroneVoice.bin`](bin/DroneVoice.bin). On GitHub, open the file and use the download button.
2. Connect the Pod over USB and put it in DFU mode: hold BOOT, tap RESET, then release BOOT.
3. Open the Daisy Web Programmer at [flash.daisy.audio](https://flash.daisy.audio), connect to the Daisy, choose `DroneVoice.bin` as the file to flash, and program it.
4. Press RESET (or power-cycle the Pod) if it doesn't start on its own.

Saved patches live in a separate part of the flash, so reflashing doesn't erase them.

The binary is built from the source in `firmware/` and runs from the Pod's internal flash, so no bootloader is needed. If you change the code, rebuild it as described below.

## Building the firmware

Requirements:

- The Daisy toolchain (see the [Daisy C++ dev environment guide](https://docs.daisy.audio/tutorials/cpp-dev-env)).
- A recent DaisySP. `LadderFilter` is not in older versions, and the DaisySP pinned by some DaisyExamples checkouts is too old. To update it, run `git checkout master && git pull` inside `DaisySP`, then rebuild it.
- The DaisySP-LGPL submodule, for ReverbSc. If linking fails on `-ldaisysp-lgpl`, run `git submodule update --init` inside `DaisySP`, then `make`.

The Makefile expects this repo to be cloned next to `DaisyExamples`, in the same parent folder:

```
some-folder/
  DaisyExamples/      (contains libDaisy and DaisySP)
  daisy-drone-voice/
```

Then build and flash:

```
cd daisy-drone-voice/firmware
make
make program-dfu
```

To flash, hold BOOT, tap RESET, then release BOOT so the Daisy enters DFU mode.

If libDaisy and DaisySP are somewhere else, point the build at them:

```
make LIBDAISY_DIR=/path/to/libDaisy DAISYSP_DIR=/path/to/DaisySP
```

The build uses about 80% of internal flash. The reverb buffer (about 400 KB) is placed in SDRAM.

To refresh the prebuilt image after a change, copy `firmware/build/DroneVoice.bin` to `bin/DroneVoice.bin` and commit it.

## Browser version

`docs/index.html` plays DroneVoice in a web browser. The engine in `docs/dsp.js` is a line-for-line port of the firmware's DSP: the DaisySP oscillators, ladder filter, drift generator and ReverbSc. It runs in an AudioWorklet at the same 48-sample control rate as the Pod.

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

## Licences and credits

- The firmware links against [libDaisy](https://github.com/electro-smith/libDaisy) and [DaisySP](https://github.com/electro-smith/DaisySP) (MIT).
- ReverbSc comes from [DaisySP-LGPL](https://github.com/electro-smith/DaisySP-LGPL) and is licensed under the LGPL v2.1. `docs/dsp.js` contains a JavaScript port of it, so that port is covered by the LGPL v2.1 too; see `LICENSE-LGPL`.
- The ladder filter is by Richard van Hoesel and Infrasonic Audio (MIT), from DaisySP. `docs/dsp.js` contains a port of it.
