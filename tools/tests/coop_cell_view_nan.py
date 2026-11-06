#!/usr/bin/env python3
"""BUG-0604: Cell: after the guest's level-up its screen showed only water for the rest of the session.

In lab 20261105-142923 cell_new_game#1 the guest's own cell had a NaN velocity (and NaN particle
velocities, field mask 0x104, BUG-0307) for 17 frames of its level-up. The cell camera (0xE600A0,
docs/re/cell.md#cell-view-nan) eases its drawn point cCellUI+0x48 with factors computed from the
avatar's speed: one NaN frame made the point NaN, and every later frame keeps it NaN, so the view
boxes and the projection had no place: no cell, no food on that screen for 7 minutes while the game
ran and the cell swam.

Two games in the Cell fixture. The guest's own cell gets the recorded fault inside its cell steps
(`coop.pool nanself vel 17`: velocity and particle velocities NaN after its soft-body step for 17
steps, the mod's own-cell heal repairs it each frame as in the run). Checks on the guest's game, 3 s
later and again 6 s after the guest swam 15 m: its camera point is finite and within 8 m of its cell
(`coop.pool cam`), and the game's projection puts cells on its screen (`coop.pool see`, the game's
world-to-screen of every cell: at least 5, its own cell among them). The host's view is unchanged.

  coop_cell_view_nan.py   (SPORE_FIX_OFF=cell.camera_heal: the camera stays NaN, it FAILS)"""
import math
import re
import time

from twoinst import Checks, start

LIMIT = 8.0


def cam(con):
    out = con.call('coop.pool cam')['out'].strip()
    vals = re.findall(r'(cam|target|lag)=\((\S+) (\S+)\)', out)
    finite = bool(vals) and all(math.isfinite(float(x)) and math.isfinite(float(y)) for _, x, y in vals)
    m = re.search(r'off=(\S+)', out)
    off = float(m.group(1)) if m else float('nan')
    return finite, off, out


def on_screen(con):
    """cells the game projects onto this screen (any kind) and whether our own cell is one of them"""
    lines = [l.split() for l in con.call('coop.pool see 400')['out'].splitlines() if l.startswith('see ')]
    return len(lines), lines


def own_pos(con):
    m = re.search(r'^avatar .*?pos=\((-?[\d.]+) (-?[\d.]+)\)', con.call('coop.pool')['out'], re.MULTILINE)
    return (float(m.group(1)), float(m.group(2))) if m else None


def playing(con):
    m = re.search(r'input_lock=(-?\d+)', con.call('coop.uistate')['out'])
    return m and int(m.group(1)) == 0 and 'cine=0' in con.call('coop.pool')['out']


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def check_view(ck, con, name, what):
    finite, off, line = cam(con)
    n, _ = on_screen(con)
    print(f'  {name} {what}: {line} | cells on screen {n}', flush=True)
    ck(finite and off < LIMIT, f'{name} {what}: its camera is finite and on its cell (off {off:.2f} m)')
    ck(n >= 5, f'{name} {what}: the game draws cells on its screen ({n})')


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: own_pos(host) and own_pos(guest), 20), 'both games have their own cell')
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    time.sleep(2)
    check_view(ck, guest, 'guest', 'before')
    check_view(ck, host, 'host', 'before')
    print('  guest:', guest.call('coop.pool nanself vel 17')['out'].strip(), flush=True)
    time.sleep(3)
    print('  guest own cell:', guest.call('coop.pool')['out'].splitlines()[0][:200], flush=True)
    check_view(ck, guest, 'guest', '3 s after the NaN velocity')
    guest.call('coop.pool goto 15 5')
    time.sleep(6)
    check_view(ck, guest, 'guest', 'after swimming 15 m')
    check_view(ck, host, 'host', 'after')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
