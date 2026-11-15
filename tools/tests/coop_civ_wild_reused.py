#!/usr/bin/env python3
"""Civilization: an NPC object the host's game builds another creature in while it stands is a new NPC at the guest
(BUG-1100, the frozen side of BUG-0044).

The 0.7.14 final check (civ_wild_pooled, lab 20261114-005006): while the host's game stood in its pause menu, its
pool gave the object of wild creature #129 (species 06538C80) out again as a creature of species 065CBF12, 517 m from
the planet's centre. host_tick's reuse check (npc.reuse) does not run while no creature is updated, and the frozen
pass (npc.frozen_pooled_rts, BUG-1089) only retired objects at the centre or gone from the list: #129 kept its id,
the guest kept the old creature where it had stood, 690.9 m from the host's object (sync-check MISMATCH n129).

A natural reuse cannot be forced (docs/re/creature.md, BUG-0044's debug_reuse): the test makes the same state the
way npc_reuse does: a host NPC is killed (`coop.npc kill`: the death flag; the guest shows it dead under its id), the host's game stands (pause
menu), then `coop.npc reuse <id>` makes the dead object alive again (a new creature in the same object, the state
npc_object_reused names). Checks, while the host still stands: the guest no longer shows the old NPC under that id
(the host retired it). SPORE_FIX_OFF=npc.frozen_reused: the guest keeps it until the host's game goes on: FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_civ_wild_reused.py
"""
import re
import time

import census
from coop_civ_wild_pooled import close_pause, frozen_s
from coop_civ_wild_sync import agree
from play_civ import dist, frame_city
from twoinst import Checks, keyframe, start


def rows(con, own):
    """{net id: row} of every creature with a net id (dead ones too)"""
    return census.Census(con.call('coop.census creatures')['out']).by_id('c', (own,))


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(5)
    h = {i: r for i, r in rows(host, 'npc').items() if r['alive']}
    if not ck(len(h) >= 2, f'wild creatures the host\'s game sends ({len(h)})'):
        return ck.result()
    pts = [r['pos'] for r in h.values()]
    c0 = max(pts, key=lambda p: sum(1 for q in pts if dist(p, q) < 30))
    frame_city({'host': host, 'guest': guest}, c0, answer=False)
    time.sleep(4)
    agree(host, guest, 10, 'both cameras on the herd')
    h, g = rows(host, 'npc'), rows(guest, 'proxy')
    cand = sorted((dist(r['pos'], c0), i) for i, r in h.items() if r['alive'] and i in g and g[i]['alive'])
    if not ck(cand, f'a host NPC the guest shows ({len(cand)})'):
        return ck.result()
    vid = cand[0][1]
    # 1. the host's NPC dies (the guest shows it dead under its id)
    dead_g = False
    t0 = time.time()
    while time.time() - t0 < 20 and not dead_g:
        print('  host: ' + host.call(f'coop.npc kill {vid}')['out'].strip(), flush=True)
        time.sleep(1.5)
        hr, gr = rows(host, 'npc').get(vid), rows(guest, 'proxy').get(vid)
        dead_g = bool(hr and not hr['alive'] and gr and not gr['alive'])
    if not ck(dead_g, f'NPC #{vid} is dead on the host and on the guest'):
        return ck.result()
    # 2. the host's game stands (pause menu)
    host.call('key esc')
    t0 = time.time()
    while time.time() - t0 < 15 and frozen_s(host) < 1.5:
        time.sleep(0.5)
    if not ck(frozen_s(host) >= 1.5, f'the host\'s game stands still (pause menu, frozen {frozen_s(host):.1f} s)'):
        close_pause(host)
        return ck.result()
    # 3. another creature in the dead one's object while it stands
    out = host.call(f'coop.npc reuse {vid}')['out'].strip()
    print(f'  host: {out}', flush=True)
    if not ck('alive again' in out, f'the host\'s object of #{vid} holds a living creature again'):
        close_pause(host)
        return ck.result()
    # 4. the guest while the host stands
    kept, t0 = None, time.time()
    while time.time() - t0 < 6:
        time.sleep(2)
        kept = vid in rows(guest, 'proxy')
        print(f'  {time.time() - t0:3.0f} s: host frozen {frozen_s(host):.0f} s, the guest shows #{vid}: {kept}', flush=True)
    m = re.search(r'frozen_reused=(\d+)', host.call('coop.npc')['out'])
    print(f'  host: frozen_reused={m.group(1) if m else "?"}', flush=True)
    keyframe('reused', 'the guest while the host is paused: the old NPC is gone')
    ck(frozen_s(host) > 6, 'the host\'s game stood still for the whole check')
    ck(not kept, f'the guest no longer shows the old NPC #{vid} the host\'s game built another creature in ({kept})')
    close_pause(host)
    time.sleep(2)
    ck(frozen_s(host) == 0, 'the host\'s game goes on after the pause menu')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
