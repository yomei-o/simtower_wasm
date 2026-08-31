"""Build a tower, look at it from several angles, and measure what came out.

The complaint that started this was that building things turned the picture
white - unrendered raster showing through as the white it was cleared to.  That
is measurable, so it is measured rather than looked for: the harness is driven
through every tool in the palette at several places and floors, then the canvas
is examined region by region for pure white and for flatness, which is what a
region that was never drawn looks like.

    python tools/paint_sweep.py [SIMTOWER.EXE] [outdir]

Every step writes a PNG and a line.  A region that goes white, or that collapses
to one colour, is the thing to look at.
"""
import json
import os
import subprocess
import sys

from PIL import Image

EXE = sys.argv[1] if len(sys.argv) > 1 else 'C:/prog/claude/SIMTOWER.EXE'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'C:/prog/claude/sweep'
MODULE = 'build/node/simtower_game_node.js'

# Canvas regions worth judging separately, in canvas pixels.
REGIONS = {
    'world': (208, 95, 780, 578),      # the main window's client
    'map': (3, 5, 203, 319),           # the map window
    'info': (205, 5, 636, 54),         # the info bar
    'palette': (135, 179, 196, 334),   # the tool palette
}

# The palette's cells.  Row 0 is the pause button; the rest are the facilities,
# three to a row, and which is which depends on the tower's rating.
PALETTE = [
    ('bulldozer', 145, 228), ('hand', 165, 228), ('zoom', 185, 228),
    ('cell4', 145, 258), ('lobby', 163, 258), ('cell6', 183, 258),
    ('cell7', 145, 288), ('cell8', 163, 288), ('cell9', 183, 288),
    ('cell10', 150, 318),
]

# Ground floor and the two above it, across the middle of the view.
FLOORS = [528, 492, 456]
COLUMNS = [330, 400, 470, 540, 610]


def regions(path):
    image = Image.open(path).convert('RGB')
    out = {}
    for name, box in REGIONS.items():
        crop = image.crop(box)
        pixels = list(crop.getdata())
        white = sum(1 for p in pixels if p == (255, 255, 255))
        counts = {}
        for p in pixels:
            counts[p] = counts.get(p, 0) + 1
        out[name] = {
            'white': white / len(pixels),
            'flattest': max(counts.values()) / len(pixels),
            'colours': len(counts),
        }
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    actions = ['click:400,264', 'wait:3500']
    steps = []

    def shoot(name):
        shot = '%s/%s.png' % (OUT, name)
        actions.extend(['shot:' + shot, 'wait:150'])
        steps.append((name, shot))

    shoot('00-new')
    for name, x, y in PALETTE:
        actions.extend(['click:%d,%d' % (x, y), 'wait:600'])
        for floor in FLOORS:
            for column in COLUMNS:
                actions.extend(['click:%d,%d' % (column, floor), 'wait:260'])
        shoot(name)

    # And then look at what was built from further up and further down: the
    # white band under the ground was only visible at one scroll position.
    actions.extend(['drag:788,300,788,160', 'wait:900'])
    shoot('90-scrolled-up')
    actions.extend(['drag:788,200,788,560', 'wait:900'])
    shoot('91-scrolled-down')
    actions.extend(['click:788,110', 'click:788,110', 'wait:900'])
    shoot('92-line-up')

    command = ['node', 'tools/node_harness.js', MODULE,
               '%s/final.png' % OUT, EXE, '8000'] + actions
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-1500:])
        print(result.stderr[-1500:])
        raise SystemExit('harness failed')

    print('%-14s %s' % ('step', '  '.join('%-22s' % r for r in REGIONS)))
    report = []
    worst = 0.0
    for name, shot in steps:
        if not os.path.exists(shot):
            print('%-14s MISSING' % name)
            continue
        measured = regions(shot)
        report.append({'step': name, 'regions': measured})
        cells = []
        for region in REGIONS:
            m = measured[region]
            worst = max(worst, m['white'])
            cells.append('white %5.1f%% flat %5.1f%%' %
                         (m['white'] * 100, m['flattest'] * 100))
        print('%-14s %s' % (name, '  '.join(cells)))
    with open('%s/report.json' % OUT, 'w') as f:
        json.dump(report, f, indent=1)
    print('\nworst white anywhere: %.2f%%' % (worst * 100))


if __name__ == '__main__':
    main()
