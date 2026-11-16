# -*- coding: utf-8 -*-
"""
Text -> paths converter for the Chinese ZH10x11 silkscreen, without Inkscape.

`createFrontSVG_ZH.py` produces an editable SVG whose characters are <text>
elements. Laser/CNC shops need outlines, so the usual step is Inkscape's
`--export-text-to-path`. This script does the same conversion with fontTools
and any CJK font file, which keeps the pipeline working on machines that have
no Inkscape (and no rsvg-convert).

Each character is centered on its cell by its ink bounding box, which matches
the `text-anchor:middle; dominant-baseline:central` of the text version closely
enough for a front panel.

Usage:
    python svg/text_to_paths_zh.py
    python svg/text_to_paths_zh.py --font "C:/Windows/Fonts/msyh.ttc" --mm 300
"""

import argparse
import os
import sys

from fontTools.misc.transform import Transform
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import createFrontSVG_ZH as front  # noqa: E402

DEFAULT_FONTS = [
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\simhei.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/System/Library/Fonts/PingFang.ttc",
]


def find_font(explicit):
    candidates = [explicit] if explicit else []
    candidates += DEFAULT_FONTS
    for path in candidates:
        if path and os.path.exists(path):
            return path
    raise SystemExit("no CJK font found, pass --font PATH")


def build(grid, path, size_mm, spacing, text_size, font_path, font_index=0):
    font = TTFont(font_path, fontNumber=font_index, lazy=True)
    upem = font["head"].unitsPerEm
    cmap = font.getBestCmap()
    glyph_set = font.getGlyphSet()

    scale = text_size / upem
    start_x = -(front.COLS * spacing) / 2 + spacing / 2
    start_y = -(front.ROWS * spacing) / 2 + spacing / 2
    svg_w = svg_h = front.CANVAS_UNITS

    written = missing = 0
    with open(path, "w", encoding="utf-8") as f:
        f.write(front.SVG_HEADER.format(
            height_mm=size_mm, width_mm=size_mm,
            vx=-svg_w / 2, vy=-svg_h / 2, vw=svg_w, vh=svg_h))
        for row, chars in enumerate(grid):
            for col, ch in enumerate(chars):
                name = cmap.get(ord(ch))
                if name is None:
                    missing += 1
                    continue
                glyph = glyph_set[name]
                bounds = BoundsPen(glyph_set)
                glyph.draw(bounds)
                if bounds.bounds is None:      # space or other blank glyph
                    continue
                x_min, y_min, x_max, y_max = bounds.bounds
                cx = (x_min + x_max) / 2.0
                cy = (y_min + y_max) / 2.0
                x = start_x + col * spacing
                y = start_y + row * spacing
                transform = Transform(scale, 0, 0, -scale,
                                      x - scale * cx, y + scale * cy)
                pen = SVGPathPen(glyph_set)
                glyph.draw(TransformPen(pen, transform))
                d = pen.getCommands()
                if not d:
                    continue
                f.write('    <path\n'
                        '       style="fill:#000000;fill-opacity:1;'
                        'stroke:none"\n'
                        '       d="%s" id="p%d_%d">\n'
                        '      <title>%s</title>\n'
                        '    </path>\n' % (d, row, col, ch))
                written += 1
        f.write("  </g>\n</svg>\n")

    if missing:
        print("WARNING: %d characters are not in %s"
              % (missing, os.path.basename(font_path)))
    print("wrote %s (%d glyphs as paths)" % (path, written))
    return written


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(
        description="Convert the ZH10x11 silkscreen text SVG into outlines "
                    "without Inkscape (fontTools).")
    ap.add_argument("--hpp", default=front.HPP_DEFAULT)
    ap.add_argument("--out", default=os.path.join(here, "Front_ZH10x11",
                                                  "ZH10x11_paths.svg"))
    ap.add_argument("--font", default=None, help="CJK font file (.ttf/.ttc)")
    ap.add_argument("--font-index", type=int, default=0,
                    help="face index inside a .ttc")
    ap.add_argument("--mm", type=int, default=300, help="panel size in mm")
    ap.add_argument("--pitch-mm", type=float, default=None,
                    help="physical LED pitch; overrides --spacing like in "
                         "createFrontSVG_ZH.py")
    ap.add_argument("--spacing", type=float, default=front.SPACING)
    ap.add_argument("--font-mm", type=float, default=None)
    args = ap.parse_args()

    units_per_mm = front.CANVAS_UNITS / args.mm
    if args.pitch_mm is not None:
        spacing = args.pitch_mm * units_per_mm
    else:
        spacing = args.spacing
    text_size = (args.font_mm * units_per_mm if args.font_mm is not None
                 else spacing * 0.7)

    grid = front.parse_grid(args.hpp)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    build(grid, args.out, args.mm, spacing, text_size, find_font(args.font),
          args.font_index)
    return 0


if __name__ == "__main__":
    sys.exit(main())
