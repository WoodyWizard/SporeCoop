#!/usr/bin/env python3
# suite: none (an RE probe run by hand on a lane: SPORE_LANE=4 python3 tools/tests/coop_pause_reasons.py; C3 of 0.7.15)
"""RE probe (C3 of docs/plan-0.7.15.md, the host-busy model): which of the game's pause reasons a player's own pause
sets in each stage, so the host can tell "the player paused" (owner decision 1: P, the pause menu, the Sporepedia, a
tutorial box) apart from the game's own stands (a caption, a stage entry, an editor coming).

One game per stage fixture (cell, creature, tribe, civ, space; no session). Once the game runs (`busy: -`), for each
key of KEYS: the key, 2 s, then `coop.pauses` (cGameTimeManager +0x48 flags and every reason counted, docs/re/
tribe.md#tribe-game-time-pauses) and `coop.status` busy; the same key again (Esc for the windows) and a wait until the
game runs again. The result table goes into docs/re/host-centric.md#host-centric-pause-reasons.

Also printed: the pause menu's window ("Resume Game") and, in Cell, whether the host's tile pass counter moves.
Checks: the time manager answers in every stage; Esc and P stand the game time (`paused=1`) in Creature, Tribe, Civ
and Space (lane 4, 2026-11-15: the Cell stage does not use cGameTimeManager's pause, docs/re/host-centric.md).

  SPORE_LANE=4 python3 tools/tests/coop_pause_reasons.py [stage ...]
"""
import os
import re
import subprocess
import sys
import time

import lane
import uifind
from fixtures import planet_of
from sporectl import Console
from twoinst import Checks, wait_up

STAGES = {  # stage -> (fixture, planet)
    'cell': ('cell', planet_of('cell')),
    'creature': ('creature', None),
    'tribe': ('tribe', planet_of('tribe')),
    'civ': ('civ', planet_of('civ')),
    'space': ('space', planet_of('space')),
}
# (key, how it is closed, what it is)
KEYS = [('esc', 'esc', 'pause menu'), ('p', 'p', 'P'), ('f1', 'esc', 'F1'), ('f2', 'esc', 'F2')]
# Creature: the HUD's Sporepedia and My Collections buttons (docs/re/creature.md, 1440x1080: 84 1054 and 114 1054)
CLICKS = {'creature': [((84, 1054), 'Sporepedia button'), ((114, 1054), 'My Collections button')]}


def start(fixture, planet):
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.sh('fixture', 'restore', fixture, lane.HOST_PROFILE)
    lane.run_host()
    assert wait_up(lane.HOST), 'the game did not start'
    c = Console(lane.HOST)
    if planet:
        for _ in range(180):
            if 'GalaxyGameEntry' in c.call('game.mode')['out']:
                break
            time.sleep(1)
        c.call('wait.frames 240 90000')
        slot, name = planet.split('/')
        c.call(f'coop.load {slot} {name}')
    else:
        subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), 'load_game.py'),
                        '-i', str(lane.HOST)], capture_output=True, text=True, timeout=600)
    t0 = time.time()
    while time.time() - t0 < 240 and not c.call('coop.avatar')['ok'] and 'Galaxy' in c.call('game.mode')['out']:
        time.sleep(2)
    return c


def pauses(c):
    out = c.call('coop.pauses')['out']
    m = re.search(r'flags=([0-9A-Fa-f]+) paused=(\d) reasons:(.*)', out)
    if not m:
        return None
    return int(m.group(1), 16), m.group(2) == '1', m.group(3).strip()


def busy(c):
    m = re.search(r'^busy: (\S+)', c.call('coop.status')['out'], re.M)
    return m.group(1) if m else '?'


def tile_passes(c):
    m = re.search(r'tile_passes=(\d+)', c.call('coop.pool')['out'])
    return int(m.group(1)) if m else None


def stands(c, st):
    """what else says the game stands: the pause menu's window (Resume Game) and, in Cell (no cGameTimeManager
    pause there), whether the tile pass counter moves over 2 s"""
    menu = bool(uifind.shown(c, 'Resume Game'))
    if st != 'cell':
        return f'menu={int(menu)}'
    a = tile_passes(c)
    time.sleep(2.0)
    b = tile_passes(c)
    return f'menu={int(menu)} tile_passes +{(b - a) if a is not None and b is not None else "?"}'


def runs(c, secs):
    """the game runs (busy '-', nothing paused) within secs"""
    t0 = time.time()
    while time.time() - t0 < secs:
        if busy(c) == '-':
            return True
        time.sleep(1)
    return False


def main():
    stages = [a for a in sys.argv[1:] if a in STAGES] or list(STAGES)
    ck = Checks()
    table = []
    for st in stages:
        fx, planet = STAGES[st]
        print(f'== {st}', flush=True)
        c = start(fx, planet)
        mode = c.call('game.mode')['out'].strip()
        ran = runs(c, 120)  # the stage's entry stand, intro cinematics
        base = pauses(c)
        print(f'  mode {mode}, runs={ran}, idle: {base}, busy {busy(c)}, {stands(c, st)}', flush=True)
        if not ck(base is not None, f'{st}: the game time manager answers (coop.pauses)'):
            continue
        table.append((st, 'idle', base, busy(c)))
        for key, close, what in KEYS:
            if not runs(c, 30):
                print(f'  {what}: skipped, the game does not run ({busy(c)}: {pauses(c)})', flush=True)
                table.append((st, what, None, busy(c)))
                continue
            c.call(f'key {key}')
            time.sleep(2.0)
            p, b = pauses(c), busy(c)
            print(f'  {what}: {p}, busy {b}, {stands(c, st)}', flush=True)
            table.append((st, what, p, b))
            if key in ('esc', 'p') and st != 'cell':  # (Cell: no cGameTimeManager pause, see the doc)
                ck(p is not None and p[1], f'{st}: {what} stands the game time (paused=1)')
            c.call(f'key {close}')
            time.sleep(1.5)
            if not runs(c, 10):  # a window that needs another Esc (Sporepedia -> pause menu)
                c.call('key esc')
                time.sleep(1.5)
        for (x, y), what in CLICKS.get(st, []):
            if not runs(c, 30):
                continue
            c.call(f'move {x} {y}')
            time.sleep(0.3)
            c.call(f'click {x} {y}')
            time.sleep(3.0)
            p, b = pauses(c), busy(c)
            print(f'  {what}: {p}, busy {b}, {stands(c, st)}', flush=True)
            table.append((st, what, p, b))
            c.call('key esc')
            time.sleep(1.5)
            if not runs(c, 10):
                c.call('key esc')
                time.sleep(1.5)
        ck.faults(game=c)
    lane.sh('kill')
    print('pause reasons (stage | what | flags | paused | reasons | busy):', flush=True)
    for st, what, p, b in table:
        print(f'  {st} | {what} | ' + (f'{p[0]:X} | {int(p[1])} | {p[2]}' if p else '- | - | -') + f' | {b}', flush=True)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
