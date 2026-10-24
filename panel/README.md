# DroneVoice Legio panel

<img src="dronevoice-legio-preview.png" alt="DroneVoice Legio panel preview" width="150" align="right">

Starter artwork for a 6 HP panel that labels Legio's controls for DroneVoice. The hole positions and sizes come from Noise Engineering's published Legio panel file (`legio_VI_24.svg` in [noiseeng/curiositas](https://github.com/noiseeng/curiositas/tree/main/panels), public domain).

**Before you order anything, check the jack labels.** Noise Engineering's file has holes but no labels, so the jack names here are a best guess. Compare them with a photo of a real Legio panel, fix the `JACKS` list in `make_panel.py`, and regenerate.

| Jack position | Label here |
| --- | --- |
| Centre, upper | PITCH (1 V/oct) |
| Left and right, first row | A CV, B CV (summed with the top and bottom knobs) |
| Centre, lower | GATE |
| Left and right, second row | IN L, IN R |
| Left and right, bottom row | OUT L, OUT R |

## Files

| File | Use |
| --- | --- |
| `dronevoice-legio-overlay.svg` | Full-colour artwork. The `cut` layer (red hairlines) has the outline and holes |
| `dronevoice-legio-silk.svg` | One-colour version, for a PCB's silkscreen layer |
| `dronevoice-legio-cuts.dxf` | Outline and holes only, in mm, for laser cutting, CNC or a PCB's board outline |
| `dronevoice-legio-preview.png` | Preview image |
| `make_panel.py` | Generates all of the above; edit labels and layout here |

All text is converted to outlines, so the files don't depend on fonts. To regenerate, run `python3 make_panel.py`; it needs matplotlib.

## What's on the panel

- **Top:** the title, the two LEDs (PAGE and LFO) and the encoder (turn for page, push for sub).
- **Beside each knob:** what the knob does on each page, coloured to match the page LED: red, green and blue on the left, then purple, yellow and white on the right. Knob A is the top knob and knob B the bottom one.
- **Switches:** TRACK (pitch input: quantised, free, sample and hold) and INPUT (drone, drone and input, input only), with the three positions labelled.
- **Jacks:** the outputs use reversed labels, so they stand out.

The colour coding only works on a printed panel or overlay. On a PCB panel, the silkscreen is a single colour.

## Ways to make it

**An overlay over the existing panel.** This is the easiest route, with no disassembly. Noise Engineering sells laser-etched acrylic overlays for Legio that fit over the stock panel; a custom one works the same way.
- Laser-cut and engraved acrylic: send the SVG to a laser-cutting service or a makerspace, cutting on the red layer.
- Printed: print the colour SVG on adhesive vinyl or photo paper, laminate it, then cut the outline and punch the holes.

Either way, remove the knobs first if the overlay's holes are smaller than the knobs.

**A replacement panel.** This replaces the stock panel, so the module has to come apart.
- **PCB panel (FR-4):** cheap, typically ordered in fives. Black solder mask with white silkscreen suits this design. Import the DXF as the board outline and the silk SVG as silkscreen, for example in KiCad. 1.6 mm boards are fine in practice; the Eurorack specification calls for 2 mm.
- **Aluminium:** engraved or UV-printed panel services take the DXF for the holes and the SVG for the artwork. They're sturdier and can print in full colour.

**Fit.** Noise Engineering says their panel files are "use at your own risk", that measurements can change between revisions, and that they aren't responsible for fit. Check the hole spacing against your module before ordering, or test with a paper printout at 100% scale first. Their trademarks can't be used commercially, so this artwork carries no Noise Engineering names or logos.
