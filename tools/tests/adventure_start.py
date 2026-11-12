#!/usr/bin/env python3
"""From the galaxy menu: Galactic Adventures -> play the first (tutorial) adventure with a random
captain. Coordinates for the 1440x1080 test profile.  usage: adventure_start.py [-i N]"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from sporectl import Console  # noqa: E402
import lane  # noqa: E402
import uifind  # noqa: E402

STEPS = [(566, 542, 300), (892, 496, 90), (1070, 626, 150), (1074, 506, 60), (1072, 626, 200)]


def click(c, x, y):
    """hover first: the menu's Galactic Adventures entry ignored a click without it (2026-11-12)"""
    c.call(f'move {x} {y}')
    time.sleep(0.4)
    c.call(f'click {x} {y}')


def main():
    inst = int(sys.argv[sys.argv.index('-i') + 1]) if '-i' in sys.argv else lane.HOST
    c = Console(inst)
    for _ in range(180):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 1300 90000')
    # the menu's intro cinematic hides the menu for longer than 1300 frames at a high frame rate (2026-11-12):
    # the menu's own "Galactic Adventures" entry must be shown first
    for _ in range(90):
        if 'id=' in c.call('coop.ui find "Galactic Adventures"').get('out', '') and \
                'menu intro' not in c.call('coop.status').get('out', ''):
            break
        time.sleep(1)
    time.sleep(3)
    # the menu entry by its caption, hovered and clicked like a player (uifind.press clicks again when nothing
    # happened); then the Galactic Adventures screen's play button and the captain panel by their places
    if not uifind.press(c, 'Galactic Adventures', timeout=20):
        print('FAILED: the menu entry Galactic Adventures did not react')
    c.call('wait.frames 300 60000')
    for x, y, f in STEPS:
        click(c, x, y)
        c.call(f'wait.frames {f} 60000')
    t0 = time.time()
    retry = time.time() + 40
    while time.time() - t0 < 300:
        mode = c.call('game.mode')['out']
        if 'Scenario' in mode and c.call('coop.avatar')['ok']:
            c.call('wait.frames 600 90000')
            print('adventure started')
            return 0
        if 'GalaxyGameEntry' in mode and time.time() > retry:  # a click was lost: redo the captain panel
            for x, y, f in STEPS[2:]:
                click(c, x, y)
                c.call(f'wait.frames {f} 60000')
            retry = time.time() + 40
        time.sleep(2)
    print('FAILED:', c.call('game.mode')['out'])
    return 1


if __name__ == '__main__':
    sys.exit(main())
