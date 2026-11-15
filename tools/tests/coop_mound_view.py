#!/usr/bin/env python3
"""BUG-0434: the partner's creature standing on a nest mound stays on it in the observer's game while the observer
turns its camera away and back.

The host stands on the fixture's nest mound (dropped from 1.3 m over the mound's place: it lands on the mound,
~1 m over GetHeightAt); the guest stands 10 m away and turns its camera with right-drags for --secs s, so the
puppet leaves and enters its screen again and again. The game put a creature off the screen on the plain terrain
and lifted it one update after it was back in view: one frame drawn ~1 m low each time it came into view
(26-28 in 70 s, docs/re/creature.md#creature-out-of-view-ground).
Checks on the guest's own drawn puppet (coop.track: its height over this game's GetHeightAt, hd, against the
host's height there, ht): no frame on the screen drawn low (hd < 0.1 m while ht > 0.5 m); the puppet was off and on
the screen often enough. (SPORE_FIX_OFF=creature.culled_ground must FAIL)"""
import argparse
import math
import re
import time

import coop_puppet_walk as pw
from twoinst import Checks, start

MOUND = (-125.79, -489.25, -64.02)  # the fixture's nest mound (docs/re/creature.md#creature-out-of-view-ground)
LIFT = 1.3


def at_radius(p, dr):
    r = math.sqrt(sum(x * x for x in p))
    return tuple(x * (r + dr) / r for x in p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=60)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    pw.settle((host, guest))
    print('  host onto the mound: ' + host.call('coop.npc gopos %.2f %.2f %.2f' % at_radius(MOUND, LIFT))['out'].strip(), flush=True)
    g = (MOUND[0] + 10, MOUND[1], MOUND[2])
    print('  guest 10 m away: ' + guest.call('coop.npc gopos %.2f %.2f %.2f' % at_radius(g, 1.0))['out'].strip(), flush=True)
    time.sleep(4)
    guest.call('coop.track start')
    W, H = pw.screen(guest)
    t0 = time.time()
    k = 0
    while time.time() - t0 < a.secs:
        dx = 420 if (k // 3) % 2 == 0 else -420
        guest.call(f'drag {W // 2} {H // 2} {W // 2 + dx} {H // 2} right')
        k += 1
        time.sleep(0.5)
    guest.call('coop.track stop')
    rows = [r for r in pw.dump(guest) if r['who'] != 255 and (r['flags'] & 1)]
    mound = [r for r in rows if r['ht'] is not None and r['ht'] > 0.5]
    off = [r for r in mound if not r['vis']]
    low_on = [r for r in mound if r['vis'] and r['hd'] < 0.1]
    low_off = [r for r in off if r['hd'] < 0.1]
    entries = sum(1 for x, y in zip(rows, rows[1:]) if not x['vis'] and y['vis'] and y['ht'] is not None and y['ht'] > 0.5)
    print(f'  {len(rows)} puppet rows, {len(mound)} with the host on raised ground (ht p50 '
          f'{sorted(r["ht"] for r in mound)[len(mound) // 2] if mound else 0:.2f} m), {len(off)} of them off the screen '
          f'({len(low_off)} drawn low there), {entries} entries into the view, {len(low_on)} frames on the screen drawn low'
          + (': ' + ', '.join(f'{r["t"]:.2f} s hd {r["hd"]:.2f}' for r in low_on[:5]) if low_on else ''), flush=True)
    print('  guest: ' + ' '.join(ln for ln in guest.call('coop.track air')['out'].splitlines() if 'mound' in ln)[:200], flush=True)
    ck(len(mound) > 300 and len(off) > 50 and entries >= 5,
       f'the host stood on the mound while the guest looked away and back ({len(mound)} rows, {len(off)} off the screen, '
       f'{entries} entries)')
    ck(not low_on, f'the puppet on the mound is never drawn on the plain terrain on the guest\'s screen ({len(low_on)} frames)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
