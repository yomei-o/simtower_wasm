"""Place things and measure whether the picture came out.

The complaint that started this was "it goes white when you build" - unrendered
raster showing through as the white it was cleared to.  That is measurable, so
it is measured here rather than looked for by eye: run the node harness through
a sequence of placements, then count, inside the main window's client area, how
much of it is pure white and how much is the single flat colour that means
nothing was drawn at all.

    python tools/paint_sweep.py <exe> [outdir]

Each step writes a PNG and a line saying what fraction of the world is white.
A run that ends with more white than it started with is the thing to look at.
"""
import json
import os
import subprocess
import sys

from PIL import Image

EXE = sys.argv[1] if len(sys.argv) > 1 else 'C:/prog/claude/SIMTOWER.EXE'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'C:/prog/claude/sweep'
MODULE = 'build/node/simtower_game_node.js'

# The main window's client, in canvas pixels: inside the frame, the caption,
# the menu bar and both scroll bars.
CLIENT = (208, 95, 780, 578)

# The tool palette's cells, as clicked on the canvas.  Row 0 is the pause
# button; the rest are the facilities, three to a row.
PALETTE = [
    ('bulldozer', 145, 228), ('hand', 165, 228), ('zoom', 185, 228),
    ('tool4', 145, 258), ('lobby', 163, 258), ('tool6', 183, 258),
    ('tool7', 145, 288), ('tool8', 163, 288), ('tool9', 183, 288),
    ('tool10', 150, 318),
]

# Where to try to place, from the ground up.
PLACES = [(400, 528), (500, 528), (450, 492), (550, 492), (450, 456)]


def measure(path):
    """White fraction of the world, and how flat it is."""
    image = Image.open(path).convert('RGB').crop(CLIENT)
    pixels = list(image.getdata())
    white = sum(1 for p in pixels if p == (255, 255, 255))
    counts = {}
    for p in pixels:
        counts[p] = counts.get(p, 0) + 1
    top = max(counts.values())
    return white / len(pixels), top / len(pixels), len(counts)


def main():
    os.makedirs(OUT, exist_ok=True)
    actions = ['click:400,264', 'wait:3500']
    steps = []
    for name, x, y in PALETTE:
        actions += ['click:%d,%d' % (x, y), 'wait:700']
        for px, py in PLACES:
            actions += ['click:%d,%d' % (px, py), 'wait:500']
        shot = '%s/%s.png' % (OUT, name)
        actions += ['shot:' + shot, 'wait:200']
        steps.append((name, shot))

    command = ['node', 'tools/node_harness.js', MODULE,
               '%s/final.png' % OUT, EXE, '8000'] + actions
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-2000:])
        print(result.stderr[-2000:])
        raise SystemExit('harness failed')

    report = []
    for name, shot in steps:
        if not os.path.exists(shot):
            print('%-10s MISSING' % name)
            continue
        white, flat, colours = measure(shot)
        report.append({'step': name, 'white': white, 'flattest': flat,
                       'colours': colours})
        print('%-10s white %5.1f%%  flattest %5.1f%%  colours %5d' %
              (name, white * 100, flat * 100, colours))
    with open('%s/report.json' % OUT, 'w') as f:
        json.dump(report, f, indent=1)


if __name__ == '__main__':
    main()
