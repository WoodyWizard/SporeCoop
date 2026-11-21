#!/usr/bin/env python3
# suite: none (a manual driver for Windows lanes with 1024x768 windows; the lab has no new-game fixture yet)
"""A new two-player game from the galaxy menu to the guest's first Creature editor (BUG-9280, BUG-9281).

The host starts a new Cell game in an empty galaxy slot (the game's wizard: herbivore), hosts; the guest joins, picks
its own cell in the start choice (Carnivore, a name, a colour, a blue body); both fill their own bars, the host's
History check starts the vote, both reach Creature. Checked on the games:
  * BUG-9281: the host's `coop.party` has errors=0 on the new game (the party waits for the planet's first save);
    with `--party-off` (coop.fix party.wait_planet_file off on the host) it counts errors: the twin FAILs
  * BUG-9280: the guest's landfall made its own creature from its cell (`coop.creations` landfall made=1) and the guest's
    first editor opens on it (its name in the editor, screenshot), not on the host's creature; after the editor the
    guest plays it (its avatar's key) and the host's game shows the guest's creature under that name; with
    `--landfall-off` (coop.fix creations.guest_landfall off on the guest) the editor starts from the host's: FAILs

  coop_newgame_landfall.py --host 3 --guest 4 --port 47812 --slot 680,197 [--party-off] [--landfall-off]
Both games in the galaxy menu first; the slot is an empty one ("New Game" when hovered). Screenshots next to this file.
"""
import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from sporectl import Console  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
S = 1024 / 1440
fails = []


def sc(p):
    return int(p[0] * S), int(p[1] * S)


CELL_BTN, CELL_OK, CAPTION_OK = sc((1396, 1063)), sc((1415, 1055)), sc((1410, 1048))
NAME = 'Glorbo'


def c(con, line):
    try:
        r = con.call(line)
        return (r.get('out') or r.get('error') or str(r)).strip()
    except Exception as e:  # a console that does not answer: the check that uses it fails
        return f'ERR {e}'


def wait(f, secs, step=2.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except Exception:
            pass
        time.sleep(step)
    return False


def ck(ok, what):
    print(('  ok   ' if ok else '  FAIL ') + what, flush=True)
    if not ok:
        fails.append(what)


def click(con, x, y, settle=1.0):
    con.call(f'move {x} {y}')
    time.sleep(0.4)
    con.call('wait.frames 2 10000')
    con.call(f'click {x} {y}')
    time.sleep(settle)


def shot(inst, name):
    subprocess.run([sys.executable, os.path.join(HERE, '..', 'sporectl.py'), '-i', str(inst), 'shot',
                    os.path.join(HERE, name + '.png')])


def widgets(con, cap):
    out = []
    for ln in c(con, 'coop.uistate widgets').splitlines():
        m = re.match(r"widget (\d+) (\d+) (\d+) (\d+) \w -?\d+ '(.*)'$", ln)
        if m and m[5] == cap:
            out.append(((int(m[1]) + int(m[3])) // 2, (int(m[2]) + int(m[4])) // 2))
    return out


def prog(con):
    m = re.search(r'full=(\d) pct=(\d+) all_full=(\d)', c(con, 'coop.progress'))
    return (int(m[1]), int(m[3])) if m else (0, 0)


def cell_caption(con):
    for _ in range(15):
        if 'cine=1' not in c(con, 'coop.pool'):
            return
        click(con, *CAPTION_OK, 3)


def cell_check(con):
    click(con, *CELL_BTN, 3)
    click(con, *CELL_OK, 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', type=int, default=3)
    ap.add_argument('--guest', type=int, default=4)
    ap.add_argument('--port', type=int, default=47812)
    ap.add_argument('--slot', default='680,197')
    ap.add_argument('--party-off', action='store_true')
    ap.add_argument('--landfall-off', action='store_true')
    a = ap.parse_args()
    h, g = Console(a.host, 60), Console(a.guest, 60)
    if a.party_off:
        print(c(h, 'coop.fix party.wait_planet_file off'))
    if a.landfall_off:
        print(c(g, 'coop.fix creations.guest_landfall off'))
    # 1. a new Cell game (the host's wizard), hosted; the guest joins and picks its own cell
    sx, sy = (int(v) for v in a.slot.split(','))
    click(h, sx, sy, 4)
    click(h, 512, 235, 4)  # the Cell stage
    for x, y, t in ((692, 382, 1.5), (877, 462, 2), (877, 462, 2), (882, 452, 1)):  # herbivore, next, next, play
        click(h, x, y, t)
    ck(wait(lambda: 'Cell' in c(h, 'game.mode'), 180), 'the host plays a new Cell game')
    c(h, 'wait.frames 300 90000')
    cell_caption(h)
    c(h, f'coop.host {a.port}')
    time.sleep(2)
    c(g, f'coop.join 127.0.0.1:{a.port} Bob')
    ck(wait(lambda: 'Cell' in c(g, 'game.mode') and 'synced=1' in c(g, 'coop.progress'), 300, 3), 'the guest joined')
    ck(wait(lambda: widgets(g, 'Your own cell'), 90, 1), 'the guest is asked for its own cell')
    for p in widgets(g, 'Carnivore')[:1]:
        click(g, *p)
    for p in widgets(g, 'Bobite')[:1]:
        click(g, *p, 0.5)
        c(g, 'key end tap')
        for _ in range(30):
            c(g, 'key backspace tap')
        c(g, 'text ' + NAME)
    for p in widgets(g, '>')[:1]:
        click(g, *p)
    for _ in range(5):
        arrows = widgets(g, '>')
        if len(arrows) > 1:
            click(g, *arrows[-1], 0.6)
    for p in widgets(g, 'Start')[:1]:
        click(g, *p)
    ck(wait(lambda: 'records=1' in c(h, 'coop.start'), 30), 'the host recorded the guest\'s start choice')
    party = c(h, 'coop.party').splitlines()[0]
    m = re.search(r'errors=(\d+)', party)
    ck(m and m[1] == '0', f'BUG-9281: no party errors on the new game ({party})')  # counter: the bug is this count
    # 2. both bars full, the host's check starts the vote, both in Creature
    time.sleep(10)
    cell_caption(g)
    c(g, 'coop.pool meal 1000')
    c(h, 'coop.pool meal 1000')
    time.sleep(10)
    cell_caption(g)  # the guest's "bar full" caption: its game reports its bar to the host once it is closed
    ck(wait(lambda: prog(h)[1] == 1 and prog(g)[1] == 1, 60), 'every bar full on both games')
    time.sleep(10)
    cell_caption(h)
    t0 = time.time()
    while time.time() - t0 < 120 and not c(h, 'coop.vote').startswith('vote active') and 'Cell' in c(h, 'game.mode'):
        cell_check(h)
        time.sleep(6)
    c(g, 'click 271 583')  # the guest's I'm ready
    ck(wait(lambda: 'Creature' in c(h, 'game.mode'), 180), 'the host reaches Creature')
    # 3. the guest's landfall and its first editor
    ck(wait(lambda: 'Editor' in c(g, 'game.mode'), 240, 3), 'the guest\'s own landfall editor opens')
    time.sleep(8)
    shot(a.guest, 'landfall_editor_guest')
    cr = c(g, 'coop.creations')
    land = re.search(r'landfall: .*', cr)
    mine = re.search(r"mine creature: (\w+) '([^']*)'", cr)
    print(' ', land[0] if land else 'landfall: ?', '|', mine[0] if mine else 'mine: ?', flush=True)
    ck(land and 'made=1' in land[0], 'BUG-9280: the guest\'s cell became its own land creature (landfall made=1)')
    ck(mine and mine[2] == NAME, f'BUG-9280: the guest\'s editor starts from its own creature "{NAME}" '
                                 f'(the editor screenshot shows it; {mine and mine[0]})')
    for con in (h, g):
        click(con, *sc((1416, 1062)), 3)
    ck(wait(lambda: 'Creature' in c(h, 'game.mode') and 'Creature' in c(g, 'game.mode'), 120, 3), 'both back in Creature')
    time.sleep(30)
    gkey = re.search(r'key=0x(\w+)', c(g, 'coop.avatar'))
    seen = re.search(r"player 1 creature: (\w+)/\w+/\w+ '([^']*)'", c(h, 'coop.creations'))
    ck(gkey and seen and seen[1] == gkey[1].zfill(8) and seen[2] == NAME,
       f'the host\'s game shows the guest\'s own creature ({seen and seen[0]}, guest avatar {gkey and gkey[1]})')
    shot(a.host, 'landfall_after_host')
    shot(a.guest, 'landfall_after_guest')
    print('RESULT', 'PASS' if not fails else 'FAIL', fails, flush=True)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
