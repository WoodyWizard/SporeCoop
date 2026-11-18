#!/usr/bin/env python3
"""BUG-1248 (players' video): the guest's cutscene of the first part pickup (the game's "Goldie's demise" tutorial)
shows its letterbox over empty water: no cell in the picture.

The tutorial (AddFoodPoints 0xE7CE10 -> 0xE79720, docs/re/cell.md#cell-goldie) runs in each game on its own when the
shared food crosses 120-150: two new cells next to the own avatar (Goldie at G+0x516C and its eater), the cell camera
aimed at Goldie (cCellUI +0xE0) and the cinematic goldies_demise. The host adds food until the guest's game starts it;
then the guest (60 m from the host, as the players were) is sampled (`coop.pool goldie`) for 6 s: Goldie and its eater must live in the guest's game and
the cell the camera follows must be a live cell on the guest's screen (the game's own picture of the cutscene).
Key frames of both games during the cutscene. Each player presses Esc at the end (the cinematic waits for its button).

  coop_cell_goldie_view.py   (SPORE_FIX_OFF=cell.goldie_keep must FAIL)
"""
import math
import re
import time

from coop_cell_part_token import playing, wait
from twoinst import Checks, keyframe, start

GOLDIE_RE = re.compile(r'goldie running=(-?\d+) idx=([0-9A-F]+) alive=(\d) onscreen=(\d) dist=([-\d.]+) eaters=(\d+) '
                       r'follow=([0-9A-F]+) follow_alive=(\d) follow_onscreen=(\d) done=(-?\d+) kept=(\d+) fix=(\d)')


def goldie(con):
    m = GOLDIE_RE.search(con.call('coop.pool goldie')['out'])
    if not m:
        return None
    g = m.groups()
    return {'running': int(g[0]), 'idx': g[1], 'alive': int(g[2]), 'onscreen': int(g[3]), 'dist': float(g[4]),
            'eaters': int(g[5]), 'follow': g[6], 'follow_alive': int(g[7]), 'follow_onscreen': int(g[8]),
            'done': int(g[9]), 'kept': int(g[10]), 'fix': int(g[11])}


def own_pos(con):
    m = re.search(r'^anim avatar \S+ anim=\S+ head=\S+ angv=\S+ pos=\(([-\d.]+) ([-\d.]+)\)', con.call('coop.pool anims')['out'], re.M)
    return (float(m.group(1)), float(m.group(2))) if m else None


def apart_of(host, guest):
    h, g = own_pos(host), own_pos(guest)
    return math.hypot(h[0] - g[0], h[1] - g[1]) if h and g else -1


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    # the players' case: the guest away from the host (near it, the mirror of the host's own tutorial cell came into the
    # guest's picture ~2 s later). The join after a load puts a new own cell back next to the host for 30 s (BUG-0352).
    apart, t0 = -1, time.time()
    while time.time() - t0 < 50:
        guest.call('coop.pool tp 60 0')
        time.sleep(4)
        apart = apart_of(host, guest)
        if apart > 40:
            time.sleep(3)
            apart = apart_of(host, guest)
            if apart > 40:
                break
    ck(apart > 40, f'the guest is away from the host ({apart:.1f} m)')
    started = None
    # small steps: the game starts it only when the food before an add is 120-150 (a guest gets the host's totals, so a
    # big step can jump over the window there)
    for _ in range(40):
        host.call('coop.pool food 10')
        t0 = time.time()
        while time.time() - t0 < 1.2:
            g = goldie(guest)
            if g and g['running'] == 1:
                started = time.time()
                break
            time.sleep(0.1)
        if started:
            break
    ck(started is not None, f"the guest's game started its first-part tutorial ({goldie(guest)})")
    if not started:
        ck.faults(host=host, guest=guest)
        return ck.result()
    rows = []
    shot = False
    while time.time() - started < 6:
        g = goldie(guest)
        if g:
            rows.append((time.time() - started, g))
        if not shot and time.time() - started > 2.5:
            keyframe('cutscene', "Cell: the first-part tutorial's cutscene on both games: the guest's letterbox shows Goldie and "
                     "its eater next to the guest's cell, not empty water", settle=0)
            shot = True
        time.sleep(0.25)
    late = [g for t, g in rows if t >= 1.0 and g['running'] == 1]
    for t, g in rows[::4]:
        print(f'  guest +{t:.1f}s {g}', flush=True)
    print('  guest now: ' + guest.call('coop.pool goldie')['out'].strip(), flush=True)
    seen = sum(1 for g in late if g['alive'] and g['onscreen'])
    follow = sum(1 for g in late if g['follow_alive'])
    eaters = sum(1 for g in late if g['eaters'] > 0)
    n = max(1, len(late))
    print(f'  guest samples during the cutscene {len(late)}: Goldie alive and on screen {seen}, the camera on a live cell {follow}, '
          f'its eater alive {eaters}; host {goldie(host)}', flush=True)
    ck(len(late) >= 10, f'the guest\'s tutorial ran for the samples ({len(late)})')
    ck(seen >= 0.8 * n, f"the guest's cutscene shows Goldie: alive and on the guest's screen in {seen} of {len(late)} samples")
    ck(follow >= 0.8 * n, f"the guest's cutscene camera follows a live cell in {follow} of {len(late)} samples")
    ck(eaters >= 0.8 * n, f"Goldie's eater lives in the guest's game in {eaters} of {len(late)} samples")
    time.sleep(4)
    for con in (guest, host):
        con.call('key esc')
    ck(wait(lambda: playing(guest), 8), 'the guest plays again after its Esc')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
