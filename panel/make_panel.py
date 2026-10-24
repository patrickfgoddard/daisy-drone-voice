#!/usr/bin/env python3
"""Generate DroneVoice panel artwork for a 6 HP Legio.

Hole positions and sizes come from Noise Engineering's published Legio
panel file (legio_VI_24.svg in github.com/noiseeng/curiositas, released
under the Unlicense). All text is converted to outlines, so the files
need no fonts.

Outputs (in this folder):
  dronevoice-legio-black.svg    black panel, white text, with a cut layer
  dronevoice-legio-silver.svg   silver panel, black text, with a cut layer
  dronevoice-legio-silk.svg     artwork only, for PCB silkscreen
  dronevoice-legio-cuts.dxf     outline and holes only, for laser/CNC/KiCad
  dronevoice-legio-preview.png  quick look (needs cairosvg or Chromium)

Run:  python3 make_panel.py
"""

import math
import os
from matplotlib.font_manager import FontProperties
from matplotlib.textpath import TextPath

HERE = os.path.dirname(os.path.abspath(__file__))

W, H = 30.0, 128.5  # mm, 6 HP x 3U

# ---------------------------------------------------------------- holes
# (x, y from top, diameter) in mm, from legio_VI_24.svg
MOUNT = [(7.7395, 3.0012), (22.9795, 3.0012), (7.7395, 125.4988), (22.9795, 125.4988)]
LED_L, LED_R = (4.05, 14.4025), (26.91, 14.4025)
ENCODER = (15.48, 19.165)
KNOB_A, KNOB_B = (15.48, 37.2625), (15.48, 57.5825)
SW_L, SW_R = (4.05, 65.2025), (26.91, 65.2025)
J_C1 = (15.48, 75.8705)    # centre, upper
J_L1, J_R1 = (5.1612, 82.9825), (24.8463, 82.9825)
J_C2 = (15.48, 90.3485)    # centre, lower
J_L2, J_R2 = (5.1612, 98.2225), (24.8463, 98.2225)
J_L3, J_R3 = (5.1612, 112.1925), (24.8463, 112.1925)

HOLES = (
    [(x, y, 3.2) for x, y in MOUNT]
    + [(*LED_L, 3.0), (*LED_R, 3.0)]
    + [(*ENCODER, 7.239)]
    + [(*KNOB_A, 9.0), (*KNOB_B, 9.0)]
    + [(*SW_L, 5.08), (*SW_R, 5.08)]
    + [(*p, 6.8) for p in (J_C1, J_L1, J_R1, J_C2, J_L2, J_R2, J_L3, J_R3)]
)

# ------------------------------------------------- jack labels (CHECK!)
# Noise Engineering's file has no labels, so this mapping is a best guess
# from Legio's controls. Compare with a real Legio panel and edit here.
JACKS = [
    (J_C1, "PITCH", False),
    (J_L1, "A CV", False),
    (J_R1, "B CV", False),
    (J_C2, "GATE", False),
    (J_L2, "IN L", False),
    (J_R2, "IN R", False),
    (J_L3, "OUT L", True),   # True = output: label drawn reversed
    (J_R3, "OUT R", True),
]

# Knob A (top) and knob B (bottom) functions on pages 0..5
A_FUNCS = ["ROOT", "CUTOFF", "RATE", "MIX", "WAVE", "SLOT"]
B_FUNCS = ["DETUNE", "RES", "DEPTH", "SIZE", "INTVL", "–"]

# ---------------------------------------------------------------- text
BOLD = FontProperties(fname="/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf")
BOOK = FontProperties(fname="/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf")


def text_path(s, x, y, cap, anchor="middle", font=BOLD, spacing=0.0):
    """Outlined text as SVG path data. (x, y) is the baseline point; cap is
    the capital height in mm."""
    probe = TextPath((0, 0), "H", size=1, prop=font)
    scale = cap / probe.get_extents().height
    # Whole-string layout keeps the font's own spacing and kerning;
    # extra letter-spacing is applied per glyph only when asked for.
    if spacing == 0.0:
        parts = [TextPath((0, 0), s, size=1, prop=font)] if s.strip() else []
        width = (parts[0].get_extents().x1 if parts else 0.0) * scale
    else:
        # Place each glyph at the advance of the text before it, plus spacing.
        parts, prefix = [], ""
        for ch in s:
            off = TextPath((0, 0), prefix + "|", size=1, prop=font).get_extents().x1 - \
                  TextPath((0, 0), "|", size=1, prop=font).get_extents().x1 if prefix else 0.0
            off += len(prefix) * spacing / scale
            if ch != " ":
                parts.append(TextPath((off, 0), ch, size=1, prop=font))
            prefix += ch
        width = (TextPath((0, 0), s, size=1, prop=font).get_extents().x1 + (len(s) - 1) * spacing / scale) * scale
    if anchor == "middle":
        x0 = x - width / 2
    elif anchor == "end":
        x0 = x - width
    else:
        x0 = x
    d = []
    for tp in parts:
        for verts, code in tp.iter_segments(curves=True, simplify=False):
            pts = [(x0 + verts[i] * scale, y - verts[i + 1] * scale) for i in range(0, len(verts), 2)]
            if code == 1:
                d.append("M%.3f %.3f" % pts[0])
            elif code == 2:
                d.append("L%.3f %.3f" % pts[0])
            elif code == 3:
                d.append("Q%.3f %.3f %.3f %.3f" % (*pts[0], *pts[1]))
            elif code == 4:
                d.append("C%.3f %.3f %.3f %.3f %.3f %.3f" % (*pts[0], *pts[1], *pts[2]))
            elif code == 79:
                d.append("Z")
    return " ".join(d), width


# ---------------------------------------------------------------- build
# One-colour themes, matching Noise Engineering's overlay styles
THEMES = {
    "black":  {"bg": "#111312", "ink": "#f2f2ee"},   # black, white text
    "silver": {"bg": "#d8dad7", "ink": "#141414"},   # silver, black text
    "silk":   {"bg": None,      "ink": "#ffffff"},   # artwork only, for PCB silkscreen
}

# Initials of the page LED colours: red, green, blue, purple, yellow, white
PAGE_INITIAL = ["R", "G", "B", "P", "Y", "W"]


def build(ink, bg):
    els = []

    def txt(s, x, y, cap, anchor="middle", fill=None, font=BOLD, spacing=0.0):
        d, w = text_path(s, x, y, cap, anchor, font, spacing)
        els.append('<path d="%s" fill="%s"/>' % (d, fill or ink))
        return w

    # Title
    txt("DRONEVOICE", W / 2, 9.4, 2.5, spacing=0.25)

    # LEDs
    txt("PAGE", LED_L[0], 19.3, 1.15)
    txt("LFO", LED_R[0], 19.3, 1.15)

    # Encoder
    txt("TURN PAGE \u00b7 PUSH SUB", W / 2, 28.2, 1.1, font=BOOK)

    # Knob legends: A beside the top knob, B beside the bottom. Each word is
    # tagged with the initial of the page LED colour that selects it.
    def legend(funcs, cy, letter):
        ys = [cy - 3.7, cy, cy + 3.7] if letter == "A" else [cy - 4.8, cy - 1.6, cy + 1.6]
        for i, (f, y) in enumerate(zip(funcs[:3], ys)):
            w = txt(f, 8.6, y + 0.6, 1.2, anchor="end")
            txt(PAGE_INITIAL[i], 8.6 - w - 0.55, y + 0.6, 0.85, anchor="end", font=BOOK)
        for i, (f, y) in enumerate(zip(funcs[3:], ys)):
            if not f:
                continue
            w0 = txt(PAGE_INITIAL[i + 3], 22.2, y + 0.6, 0.85, anchor="start", font=BOOK)
            txt(f, 22.2 + w0 + 0.55, y + 0.6, 1.2, anchor="start")
        # knob letter just above the knob, small
        txt(letter, W / 2, cy - 6.9, 1.2)

    legend(A_FUNCS, KNOB_A[1], "A")
    legend(B_FUNCS, KNOB_B[1], "B")

    # Switch positions (up / centre / down) and titles
    for label, y in zip(["QNT", "FREE", "S&H"], [-2.3, 0.0, 2.3]):
        txt(label, 7.3, SW_L[1] + y + 0.45, 0.95, anchor="start", font=BOOK)
    for label, y in zip(["DRONE", "+IN", "IN"], [-2.3, 0.0, 2.3]):
        txt(label, 23.7, SW_R[1] + y + 0.45, 0.95, anchor="end", font=BOOK)
    txt("TRACK", SW_L[0], 71.2, 1.1)
    txt("INPUT", SW_R[0], 71.2, 1.1)

    # Jacks: label above each one; outputs reversed out of a solid box
    knockout = bg or "#000000"
    for (x, y), name, is_out in JACKS:
        by = y - 4.75
        if is_out:
            _, w = text_path(name, x, by, 1.15)
            els.append('<rect x="%.3f" y="%.3f" width="%.3f" height="%.3f" rx="0.4" fill="%s"/>'
                       % (x - w / 2 - 0.7, by - 1.15 - 0.55, w + 1.4, 1.15 + 1.1, ink))
            txt(name, x, by, 1.15, fill=knockout)
        else:
            txt(name, x, by, 1.15)

    # A slow, detuned double wave across the foot of the panel
    for amp, cyc, phase in [(0.9, 2.0, 0.0), (0.9, 2.15, 0.9)]:
        pts = []
        for i in range(121):
            t = i / 120
            xx = 2.5 + t * (W - 5)
            yy = 119.8 + amp * math.sin(2 * math.pi * cyc * t + phase) * math.sin(math.pi * t)
            pts.append("%.3f %.3f" % (xx, yy))
        els.append('<path d="M%s" fill="none" stroke="%s" stroke-width="0.25" stroke-linecap="round"/>'
                   % (" L".join(pts), ink))

    return els


def svg(theme):
    t = THEMES[theme]
    els = build(t["ink"], t["bg"])
    holes = "".join('<circle cx="%.4f" cy="%.4f" r="%.4f"/>' % (x, y, d / 2) for x, y, d in HOLES)
    out = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>')
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" viewBox="0 0 %g %g">' % (W, H, W, H))
    out.append('<title>DroneVoice Legio panel (%s)</title>' % theme)
    if t["bg"]:
        out.append('<g id="background"><rect width="%g" height="%g" fill="%s"/></g>' % (W, H, t["bg"]))
    out.append('<g id="artwork">%s</g>' % "".join(els))
    # Holes shown punched out for viewing, plus a hairline cut layer
    if t["bg"]:
        out.append('<g id="holes-preview" fill="#000">%s</g>' % holes)
    out.append('<g id="cut" fill="none" stroke="#ff0000" stroke-width="0.05">'
               '<rect x="0" y="0" width="%g" height="%g"/>%s</g>' % (W, H, holes))
    out.append("</svg>")
    return "\n".join(out)


def dxf():
    """Minimal R12 DXF: outline + holes, in mm, origin bottom-left (y up)."""
    L = ["0", "SECTION", "2", "HEADER", "9", "$INSUNITS", "70", "4", "0", "ENDSEC",
         "0", "SECTION", "2", "ENTITIES"]
    corners = [(0, 0), (W, 0), (W, H), (0, H), (0, 0)]
    for (x1, y1), (x2, y2) in zip(corners, corners[1:]):
        L += ["0", "LINE", "8", "OUTLINE", "10", "%.4f" % x1, "20", "%.4f" % y1,
              "11", "%.4f" % x2, "21", "%.4f" % y2]
    for x, y, d in HOLES:
        L += ["0", "CIRCLE", "8", "HOLES", "10", "%.4f" % x, "20", "%.4f" % (H - y), "40", "%.4f" % (d / 2)]
    L += ["0", "ENDSEC", "0", "EOF"]
    return "\n".join(L) + "\n"


if __name__ == "__main__":
    for name, content in [("dronevoice-legio-black.svg", svg("black")),
                          ("dronevoice-legio-silver.svg", svg("silver")),
                          ("dronevoice-legio-silk.svg", svg("silk")),
                          ("dronevoice-legio-cuts.dxf", dxf())]:
        with open(os.path.join(HERE, name), "w") as f:
            f.write(content)
        print("wrote", name)
    try:
        import cairosvg
        cairosvg.svg2png(url=os.path.join(HERE, "dronevoice-legio-black.svg"),
                         write_to=os.path.join(HERE, "dronevoice-legio-preview.png"),
                         output_width=600)
        print("wrote dronevoice-legio-preview.png")
    except ImportError:
        print("(install cairosvg for a PNG preview)")
