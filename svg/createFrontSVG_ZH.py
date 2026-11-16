# -*- coding: utf-8 -*-
"""
Silkscreen SVG generator for the Chinese ZH10x11 word clock front.

Same idea as svg/createFrontSVG.py, but for full width Chinese characters:
square grid cells, a CJK font stack and Inkscape (instead of rsvg-convert)
for the text-to-path conversion, because rsvg-convert is not available on
most Windows machines.

Outputs (next to this script, in svg/Front_ZH10x11/):

    ZH10x11.svg         editable text version (needs a CJK font to display)
    ZH10x11_paths.svg   text converted to paths, ready for laser/CNC,
                        independent of installed fonts

Usage:
    python svg/createFrontSVG_ZH.py
    # optional: --hpp PATH --outdir PATH --mm 300
"""

import argparse
import os
import re
import subprocess
import sys

ROWS, COLS = 10, 11

# Defaults for the cell pitch / font size in SVG user units. The canvas is
# fixed at 1133.8582 units; with the default 300 mm panel that means
# 1 unit = 0.2646 mm, so SPACING 63 units = 16.7 mm cell pitch (matching the
# original latin layout). Use --pitch-mm to derive SPACING from the real
# LED-to-LED distance of your strip instead of editing these numbers.
SPACING = 63.0
TEXT_SIZE = 44.0

CANVAS_UNITS = 1133.8582          # 300 mm @ 96 dpi, like the original
UNITS_PER_MM_AT_DEFAULT = CANVAS_UNITS / 300.0   # 3.7795 units per mm

FONT_STACK = ("Noto Sans SC, Noto Sans CJK SC, Source Han Sans SC, "
              "Microsoft YaHei, SimHei, sans-serif")

HPP_DEFAULT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "include", "WordClockTypes", "ZH10x11.hpp")

SVG_HEADER = '''<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   xmlns="http://www.w3.org/2000/svg"
   height="{height_mm}mm"
   viewBox="{vx} {vy} {vw} {vh}"
   width="{width_mm}mm"
   version="1.1"
   id="svg_zh10x11">
  <defs id="defs_zh10x11" />
  <g id="g_zh10x11">
'''


def parse_grid(hpp_path):
    grid = []
    in_grid = False
    with open(hpp_path, "r", encoding="utf-8") as f:
        for line in f:
            if "Layout Front" in line:
                in_grid = True
                continue
            if in_grid:
                if "*/" in line or "class" in line:
                    break
                m = re.match(r"^\s*\*\s*\d+\s*\|\s*(.*)", line)
                if m:
                    grid.append(m.group(1).strip().split())
    if len(grid) != ROWS or any(len(row) != COLS for row in grid):
        raise ValueError("expected a %dx%d grid in %s" % (ROWS, COLS, hpp_path))
    return grid


def generate_text_svg(grid, path, size_mm, spacing, text_size):
    """Editable SVG with the characters as <text> elements, origin centered."""
    svg_w = svg_h = CANVAS_UNITS
    vx, vy = -svg_w / 2, -svg_h / 2
    start_x = -(COLS * spacing) / 2 + spacing / 2
    start_y = -(ROWS * spacing) / 2 + spacing / 2

    with open(path, "w", encoding="utf-8") as f:
        f.write(SVG_HEADER.format(height_mm=size_mm, width_mm=size_mm,
                                  vx=vx, vy=vy, vw=svg_w, vh=svg_h))
        for row, chars in enumerate(grid):
            for col, ch in enumerate(chars):
                x = start_x + col * spacing
                y = start_y + row * spacing
                f.write(
                    '    <text\n'
                    '       xml:space="preserve"\n'
                    '       style="font-style:normal;font-weight:500;'
                    'font-size:%.1fpx;line-height:1;font-family:%s;'
                    'text-anchor:middle;dominant-baseline:central;'
                    'fill:#000000;fill-opacity:1;stroke:none"\n'
                    '       x="%.2f" y="%.2f" id="t%d_%d">%s</text>\n'
                    % (text_size, FONT_STACK, x, y, row, col, ch))
        f.write("  </g>\n</svg>\n")


def convert_to_paths(text_svg, paths_svg, grid=None, size_mm=None,
                     spacing=None, text_size=None):
    """Text -> paths, so no CJK font is needed for production.

    Inkscape's --export-text-to-path is the reference route; when Inkscape is
    not installed, svg/text_to_paths_zh.py does the same with fontTools.
    """
    cmd = ["inkscape", text_svg,
           "--export-text-to-path", "--export-plain-svg",
           "--export-filename", paths_svg]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stderr.strip()[:400])
            return False
        return True
    except FileNotFoundError:
        print("Inkscape not found in PATH - converting with fontTools instead.")
        if grid is None:
            return False
        try:
            import text_to_paths_zh
            text_to_paths_zh.build(grid, paths_svg, size_mm, spacing,
                                   text_size, text_to_paths_zh.find_font(None))
            return True
        except SystemExit as exc:
            print("text_to_paths_zh: %s" % exc)
        except Exception as exc:  # noqa: BLE001 - report, do not abort the run
            print("text_to_paths_zh failed: %s" % exc)
        print("Install Inkscape or run: python svg/text_to_paths_zh.py "
              "--font PATH")
        return False


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(
        description="Silkscreen SVG for the ZH10x11 front. Adapt to your "
                    "LED strip with --pitch-mm (measured LED centre-to-"
                    "centre distance) or --spacing (raw SVG units).")
    ap.add_argument("--hpp", default=HPP_DEFAULT)
    ap.add_argument("--outdir", default=os.path.join(here, "Front_ZH10x11"))
    ap.add_argument("--mm", type=int, default=300, help="panel size in mm")
    ap.add_argument("--pitch-mm", type=float, default=None,
                    help="physical LED pitch in mm (centre to centre); "
                         "overrides --spacing. Font size then defaults to "
                         "0.7 x pitch unless --font-mm is given")
    ap.add_argument("--spacing", type=float, default=SPACING,
                    help="cell pitch in SVG units (default %.1f = 16.7 mm "
                         "on a 300 mm panel)" % SPACING)
    ap.add_argument("--font-mm", type=float, default=None,
                    help="character height in mm (default 0.7 x cell pitch)")
    args = ap.parse_args()

    # Pitch in SVG units, so that spacing * (mm / canvas) = physical pitch.
    units_per_mm = CANVAS_UNITS / args.mm
    if args.pitch_mm is not None:
        spacing = args.pitch_mm * units_per_mm
    else:
        spacing = args.spacing

    if args.font_mm is not None:
        text_size = args.font_mm * units_per_mm
    else:
        text_size = spacing * 0.7

    matrix_w_mm = COLS * spacing / units_per_mm
    matrix_h_mm = ROWS * spacing / units_per_mm
    font_mm = text_size / units_per_mm
    if matrix_w_mm > args.mm or matrix_h_mm > args.mm:
        print("WARNING: %.1f x %.1f mm matrix does not fit the %d mm panel"
              % (matrix_w_mm, matrix_h_mm, args.mm))
    print("cell pitch : %.2f mm" % (spacing / units_per_mm))
    print("font size  : %.2f mm (glyph height of the CJK font)" % font_mm)
    print("matrix area: %.1f x %.1f mm on a %d mm panel"
          % (matrix_w_mm, matrix_h_mm, args.mm))

    grid = parse_grid(args.hpp)
    os.makedirs(args.outdir, exist_ok=True)

    text_svg = os.path.join(args.outdir, "ZH10x11.svg")
    generate_text_svg(grid, text_svg, args.mm, spacing, text_size)
    print("wrote %s (editable text)" % text_svg)

    paths_svg = os.path.join(args.outdir, "ZH10x11_paths.svg")
    if convert_to_paths(text_svg, paths_svg, grid, args.mm, spacing,
                        text_size):
        print("wrote %s (text as paths)" % paths_svg)
    else:
        print("WARNING: %s was not created" % paths_svg)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
