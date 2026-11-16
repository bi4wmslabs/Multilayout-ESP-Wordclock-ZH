# -*- coding: utf-8 -*-
"""
Preview renderer for the Chinese 10x11+4 word clock layout (ZH10x11).

It parses the letter grid comment and the `word(row, from, to)` calls from
include/WordClockTypes/ZH10x11.hpp, simulates the Chinese display rule from
ClockWork::setChineseClock() and renders the resulting clock face as SVG (one
SVG per requested time). The SVGs can be converted to PNG with Inkscape:

    python tools/zh10x11_preview.py --out build/zh_preview
    inkscape --export-type=png build/zh_preview/*.svg

This is a design/verification helper only - the firmware itself does not use it.
"""

import argparse
import math
import os
import re
import sys

HPP_DEFAULT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "include", "WordClockTypes", "ZH10x11.hpp")

ROWS, COLS = 10, 11

# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

def parse_grid(hpp_path):
    """Extract the character grid from the 'Layout Front' comment block."""
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
                    cells = m.group(1).strip().split()
                    grid.append(cells)
    if len(grid) != ROWS or any(len(r) != COLS for r in grid):
        raise ValueError("grid comment must be %d rows x %d cols" % (ROWS, COLS))
    return grid


def parse_words(hpp_path):
    """Extract FrontWord -> list of (row, fromCol, toCol) from drawWord()."""
    with open(hpp_path, "r", encoding="utf-8") as f:
        src = f.read()
    body = src[src.index("void drawWord"):]
    words = {}
    current = None
    for m in re.finditer(
            r"case FrontWord::(\w+)\s*:"
            r"|cells\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)"
            r"|cells\(\s*(\d+)\s*,\s*(\d+)\s*\)", body):
        case, r3, a3, b3, r2, c2 = m.groups()
        if case:
            current = words.setdefault(case, [])
        elif r3 is not None:
            current.append((int(r3), int(a3), int(b3)))
        else:
            current.append((int(r2), int(c2), int(c2)))
    return words

# ---------------------------------------------------------------------------
# Simulation of ClockWork::setChineseClock() / setHour() / setDaytimeWords()
# ---------------------------------------------------------------------------

DAYTIME = [
    ("day_night", 0, 4), ("day_early", 5, 9), ("day_morning", 10, 11),
    ("day_noon", 12, 13), ("day_afternoon", 14, 17), ("day_evening", 18, 23),
]


def lit_cells(words, hour, minute):
    """Return the set of lit (row, displayCol) for a given time."""
    lit = set()

    def draw(name):
        for (r, a, b) in words.get(name, []):
            for c in range(a, b + 1):
                lit.add((r, c))

    # it is
    draw("es_ist")
    # weekday (preview assumes Wednesday)
    draw("weekday_3")
    # daytime word
    for (name, lo, hi) in DAYTIME:
        if lo <= hour <= hi:
            draw(name)
            break
    # hour + 点  (hour_0..hour_12; 0 -> 十二点)
    hour12 = hour % 12
    draw("hour_%d" % (hour12 if hour12 != 0 else 12))
    # minutes
    step = (minute // 5) * 5
    if step == 0:
        draw("full_hour")          # 整
    else:
        draw("min_%d" % step)
        draw("minute")             # 分
    return lit


def minute_leds(minute):
    """1-4 remainder minute LEDs (rendered as corner dots)."""
    return minute % 5

# ---------------------------------------------------------------------------
# Weather simulation (mirrors WeatherMapping.h + ClockWork::showChineseWeather)
# ---------------------------------------------------------------------------

def condition_group(group_id):
    """The firmware already passes groups (800/801/802/200/300/500/600/700),
    so the value is used as is - grouping it again would fold 802 (阴) back
    into 801 (多云)."""
    return group_id


def temperature_celsius(celsius):
    """Whole degrees like forecast::temperatureCelsius(): rounded, -39..39."""
    rounded = int(celsius + 0.5) if celsius >= 0 else int(celsius - 0.5)
    return max(-39, min(39, rounded))


ONES = {
    1: "zh_yi", 2: "zh_er", 3: "zh_san", 4: "zh_si", 5: "zh_wu_num",
    6: "zh_liu", 7: "zh_qi", 8: "zh_ba", 9: "zh_jiu",
}


def weather_cells(weather, words):
    """Lit cells for weather=(condition_id, celsius); None = no data."""
    words_cache = words
    if weather is None:
        return set()
    cond_id, celsius = weather
    lit = set()

    def draw(name):
        for (r, a, b) in words_cache.get(name, []):
            for c in range(a, b + 1):
                lit.add((r, c))

    group = condition_group(cond_id)
    cond_words = {
        800: ["zh_qing"],
        801: ["zh_duo", "zh_yun"],
        802: ["zh_yin"],
        200: ["zh_lei"],
        300: ["zh_xiao", "zh_yu"],
        500: ["zh_da", "zh_yu"],
        600: ["zh_xue"],
        700: ["zh_wu", "zh_mai"],
    }
    for name in cond_words.get(group, []):
        draw(name)

    # Rows 8-9 spell the temperature out: tens on row 8, ones digit + 度 on
    # row 9. Mirrors ClockWork::showChineseWeather().
    temp = temperature_celsius(celsius)
    if temp < 0:
        draw("zh_ling")
        draw("zh_xia")
        temp = -temp
    tens, ones = divmod(temp, 10)
    if tens == 1:
        draw("zh_shi")
    elif tens == 2:
        draw("zh_er_ten")
        draw("zh_shi")
    elif tens == 3:
        draw("zh_san_ten")
        draw("zh_shi2")
    if tens == 0 and ones == 0:
        draw("zh_ling")
    elif ones in ONES:
        draw(ONES[ones])
    draw("zh_du")
    return lit

# ---------------------------------------------------------------------------
# SVG output
# ---------------------------------------------------------------------------

CELL = 60.0
PAD = 24.0
CORNER = 14.0

FONT = "Noto Sans SC, Microsoft YaHei, Source Han Sans SC, sans-serif"


def render_svg(grid, words, times, path, weather=None):
    width = PAD * 2 + COLS * CELL
    height = PAD * 2 + ROWS * CELL + 56
    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="%.1f" height="%.1f" '
        'viewBox="0 0 %.1f %.1f">' % (width, height, width, height),
        '<rect width="100%" height="100%" fill="#101014"/>',
    ]
    # corner minute LEDs
    mx = width - PAD / 2 - 3 * CORNER
    for i in range(4):
        for (h, m) in times:
            if minute_leds(m) > i:
                parts.append('<rect x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="#ffbe6e"/>'
                             % (mx + i * CORNER * 1.4, height - PAD - 6, CORNER, CORNER))
    wlit = weather_cells(weather, words)
    for (row, chars) in enumerate(grid):
        for (col, ch) in enumerate(chars):
            x = PAD + col * CELL + CELL / 2
            y = PAD + row * CELL + CELL / 2
            on = (row, col) in wlit or any(
                (row, col) in lit_cells(words, h, m) for (h, m) in times)
            fill = "#ffc873" if on else "#3a3a42"
            parts.append(
                '<text x="%.1f" y="%.1f" font-family="%s" font-size="%.1f" '
                'fill="%s" text-anchor="middle" dominant-baseline="central">%s</text>'
                % (x, y, FONT, CELL * 0.82, fill, ch))
    label = "  ".join("%02d:%02d" % (h, m) for (h, m) in times)
    if weather is not None:
        label += "   weather %d %.1fC" % (weather[0], weather[1])
    parts.append('<text x="%.1f" y="%.1f" font-family="%s" font-size="22" '
                 'fill="#888" text-anchor="middle">%s</text>'
                 % (width / 2, height - 18, FONT, label))
    parts.append("</svg>")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(parts))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hpp", default=HPP_DEFAULT)
    ap.add_argument("--out", default=os.path.join("build", "zh_preview"))
    ap.add_argument("--times", default="3:00,7:05,9:15,12:30,14:25,19:45,21:52,0:10")
    ap.add_argument("--weather", default=None,
                    help="simulate weather, e.g. '801,26.3' (OWM id, celsius); "
                         "'none' to preview without weather rows")
    args = ap.parse_args()

    grid = parse_grid(args.hpp)
    words = parse_words(args.hpp)
    os.makedirs(args.out, exist_ok=True)

    times = []
    for t in args.times.split(","):
        h, m = t.strip().split(":")
        times.append((int(h), int(m)))

    weather = None
    if args.weather and args.weather.lower() != "none":
        cond, cels = args.weather.split(",")
        weather = (int(cond), float(cels))

    # one SVG per time plus a combined sheet
    for (h, m) in times:
        render_svg(grid, words, [(h, m)],
                   os.path.join(args.out, "zh_%02d_%02d.svg" % (h, m)),
                   weather)
    render_svg(grid, words, times,
               os.path.join(args.out, "zh_sheet.svg"), weather)

    # sanity check: every word must light existing cells
    for name, cells in words.items():
        for (r, a, b) in cells:
            if r >= ROWS or a > b or b >= COLS:
                print("[ERROR] word %s: bad cell row=%d from=%d to=%d" % (name, r, a, b),
                      file=sys.stderr)
                sys.exit(1)
    print("OK: %d words parsed, %d previews written to %s"
          % (len(words), len(times) + 1, args.out))


if __name__ == "__main__":
    main()
