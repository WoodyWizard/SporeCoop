#!/usr/bin/env python3
"""BUG-9288: with separate lives the host's Cell editor (its mating call, new parts) changes only the host's cell.

The owner's test of 2026-11-20: "on mating and changing parts the partner is teleported / pulled along with me". The
host's return from its Cell editor was a world event for every guest: "evolved the species: everybody gets the new
body", the guest in the host's world marked to take the new body in place, a guest elsewhere (its own editor, a load)
sent the host's world (a reload: its cell at the level's start, next to the host).

On the guest's game (the other player):
  1. the guest plays its own cell (a carnivore model of the game's packages, `coop.creations choose cell`);
  2. the host calls its mate, meets it, edits its cell (a part moved) and leaves the editor;
  3. all the while the guest's game stays in the Cell stage (never a load), its cell stays where it swam (under 8 m
     from the spot before the host's mating call) and keeps its own model; the guest reads no "everybody gets the new
     body" notice (its log), the host's log says the evolution stayed the host's (BUG-9288 line);
  4. no faults.
--old (`coop.fix cell.own_editor off` on the host, twin cell_own_editor_off): step 3 fails (the notice).

  SPORE_LANE=N python3 tools/tests/coop_cell_own_editor.py [--old]
"""
import re
import sys
import threading
import time

import lane
from coop_editor_desync import DONE, edit_cell, hud_ready, mating_call
from twoinst import Checks, shot, start

GUEST_CELL = '252A132F'  # a carnivore cell of Spore_Content.package (docs/re/cell.md#cell-start-choice)


def out(con, line):
    return con.call(line).get('out', '')


def mode(con):
    return out(con, 'game.mode')


def pos(con):
    m = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\)', out(con, 'coop.pool'))
    return (float(m.group(1)), float(m.group(2))) if m else None


def model(con):
    m = re.search(r'diet me model=(\w{8})', out(con, 'coop.pool diet'))
    return m.group(1) if m else None


def log_text(inst):
    try:
        with open(lane.game_log(inst), encoding='utf-8', errors='replace') as f:
            return f.read()
    except OSError:
        return ''


def main():
    old = '--old' in sys.argv
    host, guest = start('cell', own_progress=True)
    ck = Checks()
    if old:
        host.call('coop.fix cell.own_editor off')
    guest.call(f'coop.creations choose cell {GUEST_CELL}')
    time.sleep(8)
    gm0 = model(guest)
    ck(gm0 and gm0 != model(host), f'1: the guest plays its own cell ({gm0}, the host {model(host)})')
    glog0 = len(log_text(lane.GUEST))
    p0 = pos(guest)
    modes, drift, stop = set(), [0.0], threading.Event()

    def watch():
        while not stop.is_set():
            try:
                modes.add(mode(guest).split()[-1])
                p = pos(guest)
                if p and p0:
                    drift[0] = max(drift[0], ((p[0] - p0[0]) ** 2 + (p[1] - p0[1]) ** 2) ** 0.5)
            except (OSError, ValueError, IndexError):
                pass
            time.sleep(0.5)

    t = threading.Thread(target=watch, daemon=True)
    t.start()
    alive, reached = mating_call(host, 'host', lambda: 'Editor' in mode(host))
    ck(reached, f'2: the host is in its Cell editor (mate seen {alive})')
    if reached:
        time.sleep(4)
        edit_cell(host, 'Hostcell')
        host.call('click %d %d' % DONE)
        for _ in range(60):
            if 'Cell' in mode(host) and hud_ready(host):
                break
            time.sleep(1)
        time.sleep(10)
    stop.set()
    t.join(5)
    glog = log_text(lane.GUEST)[glog0:]
    hlog = log_text(lane.HOST)
    print(f'  guest modes {sorted(modes)}, drift {drift[0]:.1f} m, model {gm0} -> {model(guest)}', flush=True)
    ck(modes == {'Cell'}, f"3: the guest's game stayed in the Cell stage (modes {sorted(modes)})")
    ck(drift[0] < 8, f"3: the guest's cell was not moved ({drift[0]:.1f} m from where it was)")
    ck(model(guest) == gm0, f"3: the guest keeps its own cell ({model(guest)})")
    ck('everybody gets the new body' not in glog, '3: the guest reads no "everybody gets the new body"')
    ck('the host\'s own cell only, no share' in hlog, "3: the host's evolution stayed the host's (its log)")
    shot(guest, 'cell_own_editor_guest')
    shot(host, 'cell_own_editor_host')
    host.call('coop.fix cell.own_editor on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
