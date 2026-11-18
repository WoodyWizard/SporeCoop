"""Targeted native Cell checks on two already joined disposable QA profiles.

No stage replay. Tests the actual respawn camera, authoritative scaled food and
partner marker rendering. Use the separate editor check for world refreshes.
"""
import argparse
from pathlib import Path
import re
import time
from duo_villages import Console, command


def wait_until(check, seconds=30):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = check()
        if value:
            return value
        time.sleep(.2)
    raise AssertionError('Timed out waiting for native Cell state')


def values(c, request):
    return {k: float(v) for k, v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)', command(c, request))}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--host', type=int, default=4)
    ap.add_argument('--guest', type=int, default=5)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--food-only', action='store_true')
    ap.add_argument('--camera-only', action='store_true')
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        assert 'duoqareportsession' in command(c, 'coop.savedb').lower()
        assert 'Cell:' in command(c, 'coop.avatar')
        assert 'total faults: 0' in command(c, 'exc')
    command(h, 'coop.pool invulnfor 120')
    status = command(h, 'coop.status')
    seats, scaling = map(int, re.search(r'Player slots: (\d+), difficulty scaling (\d+)%', status).groups())
    work = 100+(seats-1)*scaling
    food = lambda c: values(c, 'coop.pool dna')['food']
    wait_until(lambda: food(h) == food(g))
    before = values(h, 'coop.pool dna')
    # A whole divisor avoids dependence on the saved fractional remainder.
    award = 0 if args.camera_only else 100
    if award: command(g, f'coop.pool food {work}')
    wait_until(lambda: food(h) == before['food']+award and food(g) == food(h))
    after = values(h, 'coop.pool dna')
    assert after['spent'] == before['spent']
    assert after['budget']-before['budget'] == after['earned']-before['earned']
    if award: print(f'PASS raw meal {work} pays 100 shared food; native budget delta {after["budget"]-before["budget"]}', flush=True)
    # Pause native gameplay on both sides, leaving network/frame processing live.
    # This prevents autonomous eating from being mistaken for duplicated awards.
    for c in (h, g):
        command(c, 'key esc')
        wait_until(lambda: c.call('coop.ui find "Resume Game"')['ok'])
    baseline = food(h)
    try:
        for _ in range(5):
            time.sleep(2.1)
            assert food(h) == baseline and food(g) == baseline
    finally:
        for c in (h, g): command(c, 'key esc')
    print('PASS repeated authoritative totals do not grant extra food', flush=True)
    if args.food_only:
        return
    for enabled in (False, True, True):
        command(g, 'coop.fix cell.camera_snap on')
        command(g, 'coop.pool tp 35 0')
        command(g, 'wait.frames 10 3000')
        command(g, 'coop.pool invulnfor 0')
        command(g, 'wait.frames 2 1000')
        command(g, 'coop.pool hp 6')
        command(g, 'coop.fix cell.camera_snap ' + ('on' if enabled else 'off'))
        count = values(g, 'coop.pool')['respawns']
        command(h, 'coop.pool hurt 1 50')
        wait_until(lambda: values(g, 'coop.pool')['downed'] == 1, 8)
        wait_until(lambda: values(g, 'coop.pool')['respawns'] > count, 18)
        command(g, 'wait.frames 20 3000')
        camera = values(g, 'coop.pool camera')
        if enabled:
            assert camera['off'] < 3, camera
            print('PASS native respawn camera centered', camera, flush=True)
        else:
            assert camera['off'] > 3, camera
            print('EXPECTED FAIL old respawn camera remains displaced', camera, flush=True)
    command(g, 'coop.fix cell.camera_snap on')
    command(g, 'coop.pool tp 60 0')
    command(h, 'wait.frames 20 3000')
    assert values(h, 'coop.partner_markers')['markers'] >= 1
    command(h, 'shot "' + (args.output/'partner-arrow.png').resolve().as_posix() + '"')
    print('PASS off-screen partner indicator rendered', flush=True)
    for c in (h, g):
        command(c, 'wait.frames 20 3000')
        assert 'total faults: 0' in command(c, 'exc')


if __name__ == '__main__':
    main()
