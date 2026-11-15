#!/usr/bin/env python3
"""BUG-1006: Cell: a guest's mirror whose soft body went NaN stayed NaN for good.

Audit 0.7.14 (WLD-19). On a guest every mirror (a copy of a host cell) is moved each tick by
`want - pos` of its particles (modes::cell_move) and turned after the soft-body step (h_Soft) by an
angle from its own matrix: once its particles were NaN, every later move and turn added NaN, and the
mirror never came back (it drifts or vanishes on the guest, and its contacts make the own cell NaN,
BUG-0292).

Two games in the Cell fixture. The guest writes NaN into the particles of its nearest soft mirror
(`coop.pool nanmirror`: what the game's own cell step did to the own cell in BUG-0307's 4-player run).
Checked in the guest's game 1 s and 3 s later (`coop.pool mirrornf c<key>`: the game's cell +0x4C and
the mean of its soft-body particles): no non-finite field, and the particles' mean within 2 m of where
the host's game has that cell (`coop.pool cell c<key>` on the host).

  coop_cell_mirror_nan.py   (SPORE_FIX_OFF=cell.mirror_nan_heal: the mirror stays NaN, it FAILS)
"""
import math
import re
import time

from twoinst import Checks, start


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def pair(out, key):
    m = re.search(r'\b' + key + r'=\(([^ )]+) ([^ )]+)\)', out)
    if not m:
        return None
    try:
        return float(m.group(1)), float(m.group(2))
    except ValueError:
        return math.nan, math.nan


def finite(p):
    return p is not None and all(math.isfinite(v) for v in p)


def host_pos(host, key):
    return pair(host.call(f'coop.pool cell {key}')['out'], 'pos')


def main():
    host, guest = start('cell')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 30 and 'mirror_local' not in guest.call('coop.pool mirrors')['out']:
        time.sleep(1)
    time.sleep(3)
    key, r = None, ''
    for _ in range(10):
        r = guest.call('coop.pool nanmirror')['out'].strip()
        m = re.match(r'nanmirror (c\d+):', r)
        if m:
            key = m.group(1)
            break
        time.sleep(1)
    print('  guest: ' + r, flush=True)
    if not key:
        print('RESULT: SKIP (no soft mirror on the guest)')
        return 0
    bad = []
    for wait in (1.0, 2.0):
        time.sleep(wait)
        g = guest.call(f'coop.pool mirrornf {key}')['out'].strip()
        hp = host_pos(host, key)
        print(f'  guest: {g} | host: {hp}', flush=True)
        if 'gone' in g:
            bad.append(f'the mirror is gone on the guest while the host has it at {hp}' if finite(hp) else None)
            continue
        nf, pm = num(g, 'nf'), pair(g, 'pmean')
        nf = int(re.search(r'nf=0x([0-9A-Fa-f]+)', g).group(1), 16) if 'nf=0x' in g else nf
        if nf:
            bad.append(f'non-finite fields 0x{nf:X}')
        elif finite(hp) and finite(pm) and math.dist(pm, hp) > 2.0:
            bad.append(f'its particles {math.dist(pm, hp):.1f} m from the host\'s pose')
    bad = [b for b in bad if b]
    ck(not bad, f'the guest\'s game shows the NaN mirror {key} finite and in the host\'s place within 1 s and 3 s'
                + (f' ({"; ".join(bad)})' if bad else ''))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
