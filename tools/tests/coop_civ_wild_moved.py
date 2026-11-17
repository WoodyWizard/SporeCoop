#!/usr/bin/env python3
"""Civilization: a herd the host's game moves elsewhere while it stands leaves its old place on the guest too
(BUG-1216, the moved side of BUG-1089 / BUG-1100).

The campaign's Civ segment (lab 20261117-130736): while the host stood 24 s in its pause menu, its game's pool gave a
herd of 7 out again 392-398 m away, the same species; host_tick sends nothing while no creature is updated and the
frozen pass (npc.frozen_pooled_rts, npc.frozen_reused) only retired objects at the centre or reused as another
creature, so both guests kept the herd where it had stood (states 20 s old, sync-check `n: 7 apart`) until the game
went on.

A natural move cannot be forced: the test makes the same state. Two games on civ_npc, both cameras on the herd, the
guest shows the host's wild creatures (census by net id). The host opens its pause menu (its game updates no
creature), then its game sets the herd 60 m aside (`coop.civ creatureshift`, set_object_position, as the pool's
move). Checks, for 6 s while the host still stands: no copy of a moved creature on the guest more than 8 m from where
the host's game has it (the host retired them); after the pause the guest shows the host's wild creatures where they
are again.
SPORE_FIX_OFF=npc.frozen_moved: the guest keeps the herd 60 m off until the host's game goes on: FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_civ_wild_moved.py
"""
import re
import time

from coop_civ_wild_pooled import close_pause, frozen_s
from coop_civ_wild_reused import rows
from coop_civ_wild_sync import agree
from play_civ import dist, frame_city
from twoinst import Checks, keyframe, start

SHIFT = 60  # m
NEAR = 8.0  # m


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(5)
    h = {i: r for i, r in rows(host, 'npc').items() if r['alive']}
    if not ck(len(h) >= 3, f'wild creatures the host\'s game sends ({len(h)})'):
        return ck.result()
    pts = [r['pos'] for r in h.values()]
    c0 = max(pts, key=lambda p: sum(1 for q in pts if dist(p, q) < 30))
    frame_city({'host': host, 'guest': guest}, c0, answer=False)
    time.sleep(4)
    agree(host, guest, 10, 'both cameras on the herd')
    h, g = rows(host, 'npc'), rows(guest, 'proxy')
    herd = sorted(i for i, r in h.items() if r['alive'] and dist(r['pos'], c0) < 30 and i in g and g[i]['alive'])
    if not ck(len(herd) >= 2, f'the herd\'s creatures the guest shows ({herd})'):
        return ck.result()
    # 1. the host's game stands (pause menu)
    host.call('key esc')
    t0 = time.time()
    while time.time() - t0 < 15 and frozen_s(host) < 1.5:
        time.sleep(0.5)
    if not ck(frozen_s(host) >= 1.5, f'the host\'s game stands still (pause menu, frozen {frozen_s(host):.1f} s)'):
        close_pause(host)
        return ck.result()
    # 2. its game moves the herd while it stands
    x, y, z = c0
    print('  host:', host.call(f'coop.civ creatureshift {x:.1f} {y:.1f} {z:.1f} 30 {SHIFT}')['out'].strip(), flush=True)
    # 3. the guest while the host stands
    worst, t0 = {}, time.time()
    while time.time() - t0 < 6:
        time.sleep(1)
        hr, gr = rows(host, 'npc'), rows(guest, 'proxy')
        for i in herd:
            if i in gr and gr[i]['alive']:
                hp = hr[i]['pos'] if i in hr else None
                # (the host retired it: no host row under that id; its creature is at the herd's new place)
                d = dist(gr[i]['pos'], hp) if hp else min(dist(gr[i]['pos'], r['pos']) for r in hr.values()) if hr else 1e9
                worst[i] = max(worst.get(i, 0.0), d)
        print(f'  {time.time() - t0:3.0f} s: host frozen {frozen_s(host):.0f} s, the guest shows '
              f'{sum(1 for i in herd if i in gr and gr[i]["alive"])} of the herd\'s {len(herd)} old ids', flush=True)
    m = re.search(r'frozen_moved=(\d+)', host.call('coop.npc')['out'])
    print(f'  host: frozen_moved={m.group(1) if m else "?"}', flush=True)
    keyframe('moved', 'the guest while the host is paused: the herd is not left at its old place')
    ck(frozen_s(host) > 6, 'the host\'s game stood still for the whole check')
    far = {i: d for i, d in worst.items() if d > NEAR}
    ck(not far, f'no copy of the moved herd more than {NEAR} m from the host\'s creatures while the host stands '
       f'({" ".join(f"#{i} {d:.0f} m" for i, d in sorted(far.items())) or "none"})')
    close_pause(host)
    time.sleep(4)
    ck(frozen_s(host) == 0, 'the host\'s game goes on after the pause menu')
    _, _, worst_after, live, _ = agree(host, guest, 15, 'after the pause')
    ck(worst_after < 4.0 and live[0] == live[1],
       f'after the pause the guest shows the host\'s wild creatures where they are (worst {worst_after:.1f} m, live {live})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
