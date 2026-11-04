#!/usr/bin/env python3
"""BUG-0343: Cell: the view is off centre after the co-op respawn next to the partner (and after a
guest is put next to the host after a load or an editor).

The cell camera (cCellUI, update 0xE600A0, docs/re/cell.md#cell-camera) chases a target point that
moves towards the avatar only at 1.5 x the avatar's speed, and keeps the drawn point within a box
around the avatar. The mod moves a stopped cell (cellsync stop_at: the respawn after a knockout, the
guest's move next to the host after a load / an editor / an own-cell rebuild): before the fix the
drawn point stayed at the box's edge, the player's cell off centre ("in the lower left of my screen")
until it swam a while.

Two games in the Cell fixture, in each game's own state (the camera point cCellUI+0x48 against the
own cell's position +0x4C, `coop.pool cam`):
1. the guest is moved 40 m from the host (`coop.pool tp`, the same stop_at as the join after a load):
   3 s later the guest's view is centred on its cell (under 1.5 m);
2. the host is knocked out (`coop.pool selfhit 250`) with the guest 40 m away, nobody comes, it
   respawns small next to the guest: 3 s later the host's view is centred on its cell.

  coop_cell_camera.py   (SPORE_FIX_OFF=cell.camera_snap: the view stays 4-8 m off, it FAILS)"""
import re
import time

from twoinst import Checks, start

LIMIT = 1.5


def cam(con):
    out = con.call('coop.pool cam')['out']
    m = re.search(r'off=([\d.]+)', out)
    return (float(m.group(1)) if m else None), out.strip()


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def own_pos(con):
    m = re.search(r'^avatar .*?pos=\((-?[\d.]+) (-?[\d.]+)\)', con.call('coop.pool')['out'], re.MULTILINE)
    return (float(m.group(1)), float(m.group(2))) if m else None


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def playing(con):
    """no caption, no input lock (the hatch after the load puts the cell back at its start)"""
    m = re.search(r'input_lock=(-?\d+)', con.call('coop.uistate')['out'])
    return m and int(m.group(1)) == 0 and 'cine=0' in con.call('coop.pool')['out']


def watch(con, name, secs):
    """the camera offset over <secs> seconds (printed), the last one returned"""
    off = None
    for i in range(int(secs / 0.5)):
        off, line = cam(con)
        if i % 2 == 0 or i == int(secs / 0.5) - 1:
            print(f'  {name} t+{i * 0.5:.1f}s {line}', flush=True)
        time.sleep(0.5)
    return off


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: own_pos(host) and own_pos(guest), 20), 'both games have their own cell')
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    time.sleep(1)
    for con, n in ((host, 'host'), (guest, 'guest')):
        print(f'  {n} before: {cam(con)[1]}', flush=True)

    # 1. the guest moved 40 m (the join path's stop_at)
    r = guest.call('coop.pool tp 40 0')['out'].strip()
    print(f'  guest: tp 40 0 -> {r}', flush=True)
    off = watch(guest, 'guest', 3.0)
    ck(off is not None and off < LIMIT, f'the guest\'s view is centred on its cell 3 s after the move ({off} m)')

    # 2. the host knocked out with the guest away, respawn next to the guest
    hp, gp = own_pos(host), own_pos(guest)
    d = ((hp[0] - gp[0]) ** 2 + (hp[1] - gp[1]) ** 2) ** 0.5 if hp and gp else None
    print(f'  host {hp} guest {gp}: {d} m apart', flush=True)
    r = host.call('coop.pool selfhit 250')['out']
    print(f'  host: {r.strip()}', flush=True)
    ck(num(r, 'downed') == 1, 'the host is knocked out (its partner is up)')
    up = wait(lambda: num(host.call('coop.pool')['out'], 'downed') == 0, 20, 0.2)
    ck(up, 'the host respawns after its knockout')
    hp, gp = own_pos(host), own_pos(guest)
    d = ((hp[0] - gp[0]) ** 2 + (hp[1] - gp[1]) ** 2) ** 0.5 if hp and gp else None
    print(f'  after the respawn: host {hp} guest {gp}: {d} m apart', flush=True)
    ck(d is not None and d < 6, f'the host respawned next to the guest ({d} m)')
    off = watch(host, 'host', 3.0)
    ck(off is not None and off < LIMIT, f'the host\'s view is centred on its cell 3 s after the respawn ({off} m)')
    time.sleep(2)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
