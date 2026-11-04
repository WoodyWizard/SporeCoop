#!/usr/bin/env python3
"""BUG-0519: Cell: the partner's own cell (its puppet) is drawn off while a level change rescales the
worlds.

A level change rescales each game's world over ~3.5 s (WorldScaleStep: every position times old / new
world scale G+0x514C about the origin, docs/re/cell.md#cell-world-scale), on each game at its own
moment. The players' own cells travel in MSG_PLAYER_STATE in the sender's scale; before the fix the
receiver drew them unchanged, so for those seconds the partner's cell was drawn where the sender's
scale put it, not where it is in the receiver's world.

Two games in the Cell fixture, both cells idle near (100, 100) (an idle cell's position times the
world scale stays the same through the change). The host's food goes up until its level changes,
twice; every 0.25 s during each change both games print their scale, their own cell and the
partner's puppet in one frame (`coop.pool scalepos`). The error is the distance between the puppet
and the partner's own cell in the drawing game's metres (both points times their game's scale,
divided by the drawing game's scale). Checks: under 1.5 m in 90 % of the samples of each direction
(host draws guest, guest draws host).

  coop_cell_puppet_scale.py   (SPORE_FIX_OFF=cell.puppet_scale: tens of metres, it FAILS)"""
import math
import re
import time

from twoinst import Checks, start

LIMIT = 1.5


def scalepos(con):
    out = con.call('coop.pool scalepos')['out']
    sc = re.search(r'scale=([\d.]+)', out)
    me = re.search(r'me=\((-?[\d.]+) (-?[\d.]+)\)', out)
    pp = {int(i): (float(x), float(y)) for i, x, y in re.findall(r'p(\d)=\((-?[\d.]+) (-?[\d.]+)\)', out)}
    if not sc or not me:
        return None
    return float(sc.group(1)), (float(me.group(1)), float(me.group(2))), pp


def level(con):
    m = re.search(r'level=(-?\d+)', con.call('coop.pool')['out'])
    return int(m.group(1)) if m else None


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


def errors(host, guest):
    """(host draws the guest, guest draws the host) in the drawing game's metres, None when missing"""
    h, g = scalepos(host), scalepos(guest)
    if not h or not g:
        return None, None, h, g
    hs, hme, hp = h
    gs, gme, gp = g
    e_h = math.dist([v * hs for v in hp[1]], [v * gs for v in gme]) / hs if 1 in hp else None
    e_g = math.dist([v * gs for v in gp[0]], [v * hs for v in hme]) / gs if 0 in gp else None
    return e_h, e_g, h, g


def p90(v):
    v = sorted(v)
    return v[int(0.9 * (len(v) - 1))] if v else None


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    guest.call('coop.pool tp 6 3')  # apart from the host, both still
    time.sleep(3)
    e_h, e_g, h, g = errors(host, guest)
    print(f'  before: host draws the guest {e_h} m off, guest draws the host {e_g} m off  {h} {g}', flush=True)
    ck(e_h is not None and e_g is not None and e_h < LIMIT and e_g < LIMIT, 'both puppets in place before a level change')
    all_h, all_g = [], []
    for k in range(2):
        l0 = level(host)
        for _ in range(8):  # food until the level changes (the total is shared)
            host.call('coop.pool food 30')
            time.sleep(0.4)
            if level(host) != l0:
                break
        l1 = level(host)
        ck(l0 is not None and l1 is not None and l1 > l0, f'level change {k + 1}: the host grew ({l0} -> {l1})')
        t0 = time.time()
        eh, eg = [], []
        while time.time() - t0 < 6.0:
            e_h, e_g, h, g = errors(host, guest)
            if e_h is not None:
                eh.append(e_h)
            if e_g is not None:
                eg.append(e_g)
            if h and g:
                print(f'  level {l1} t+{time.time() - t0:.1f}: scale host {h[0]:.3f} guest {g[0]:.3f}  '
                      f'host draws the guest {e_h if e_h is None else round(e_h, 2)} m off, '
                      f'guest draws the host {e_g if e_g is None else round(e_g, 2)} m off', flush=True)
            time.sleep(0.25)
        print(f'  level change {k + 1}: p90 host draws the guest {p90(eh)}, guest draws the host {p90(eg)}', flush=True)
        all_h += eh
        all_g += eg
        time.sleep(2)
    ph, pg = p90(all_h), p90(all_g)
    ck(ph is not None and ph < LIMIT, f'the host draws the guest\'s cell where it is while the worlds rescale (p90 {ph} m)')
    ck(pg is not None and pg < LIMIT, f'the guest draws the host\'s cell where it is while the worlds rescale (p90 {pg} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
