"""Open everything on the menus and look at what appears.

The dialogs are the least-travelled part of the port: 51 templates, and only
the ones startup goes through had been opened.  This walks the menus, gives
each item a screenshot, and measures the same regions paint_sweep does - a
dialog that comes up blank shows as a flat region, and one that does not come
up at all shows as no change.

    python tools/menu_sweep.py [SIMTOWER.EXE] [outdir]

Destructive items are left alone: Exit, and the ones that would replace the
tower.  Save As and Open have their own round-trip test.
"""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paint_sweep import regions, REGIONS          # noqa: E402

EXE = sys.argv[1] if len(sys.argv) > 1 else 'C:/prog/claude/SIMTOWER.EXE'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'C:/prog/claude/menus'
MODULE = 'build/node/simtower_game_node.js'

# Where the menu bar's items are, and where each popup's lines fall.
BAR = {'file': 224, 'options': 274, 'windows': 340, 'help': 396}
BAR_Y = 87

# name, bar, popup x, popup y, and how to get rid of what it opens.
ITEMS = [
    ('options-animation', 'options', 300, 108, None),
    ('options-sound',     'options', 300, 127, None),
    ('options-fastmode',  'options', 300, 146, None),
    ('windows-toolbar',   'windows', 380, 108, None),
    ('windows-toolbar-back', 'windows', 380, 108, None),
    ('windows-infobar',   'windows', 380, 127, None),
    ('windows-infobar-back', 'windows', 380, 127, None),
    ('windows-map',       'windows', 380, 146, None),
    ('windows-map-back',  'windows', 380, 146, None),
    ('windows-finance',   'windows', 380, 183, 'click:404,454'),
    ('help-about',        'help',    430, 127, 'click:400,300'),
]


def main():
    os.makedirs(OUT, exist_ok=True)
    actions = ['click:400,264', 'wait:3500',
               # A tower with something in it, so the windows have something
               # to show.
               'click:163,258', 'wait:600',
               'click:400,528', 'wait:400', 'click:470,528', 'wait:600']
    steps = []
    for name, bar, x, y, dismiss in ITEMS:
        actions.extend(['click:%d,%d' % (BAR[bar], BAR_Y), 'wait:400',
                        'click:%d,%d' % (x, y), 'wait:1400'])
        shot = '%s/%s.png' % (OUT, name)
        actions.extend(['shot:' + shot, 'wait:150'])
        steps.append((name, shot))
        if dismiss:
            actions.extend([dismiss, 'wait:800'])

    command = ['node', 'tools/node_harness.js', MODULE,
               '%s/final.png' % OUT, EXE, '8000'] + actions
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-1500:])
        print(result.stderr[-1500:])
        raise SystemExit('harness failed')

    report = []
    for name, shot in steps:
        if not os.path.exists(shot):
            print('%-24s MISSING' % name)
            continue
        measured = regions(shot)
        report.append({'step': name, 'regions': measured})
        cells = ['%s white %4.1f%% flat %4.1f%%' %
                 (r, measured[r]['white'] * 100, measured[r]['flattest'] * 100)
                 for r in REGIONS]
        print('%-24s %s' % (name, '  '.join(cells)))
    with open('%s/report.json' % OUT, 'w') as f:
        json.dump(report, f, indent=1)


if __name__ == '__main__':
    main()
