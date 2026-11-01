#!/usr/bin/env python3
"""Tribe: an NPC tribe that forms on the host during play is on the guest too (BUG-0381).

During Tribe the host's game forms new NPC tribes (cNpcTribeController: a 4-player campaign went from
2 to 5 tribes in a few minutes). The guests' games never formed them, and the guest had no tribe to
mirror the host's members of these tribes into: the sync-check reported "t: 27 missing on the
guest" for minutes; the players on the guests did not see these tribes at all.

1. Two games on the tribe fixture (cold: the new tribe stays in the host's world). The host's game
   forms an NPC tribe of 4 members 70 m from the village (Simulator::SpawnNpcTribe, the function the
   game's tribe controller forms them with; `coop.tribe npc_tribe 4 70`).
2. Within 15 s the guest's game lists a tribe within 10 m of the host's new tribe with the host's
   members of it (the same number, the same species), each within 8 m of the host's member: the
   game's own tribe list and member vectors on the guest, not a counter of the mod.

SPORE_FIX_OFF=tribe.npc_tribes (the guest has only the tribes its own game formed): must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_tribe_npc_form.py
"""
import math
import re
import time

from twoinst import Checks, start


def tribes(con):
    """[(player, (x, y, z), species, [(id, (x, y, z))])] from the game's tribe list"""
    out = []
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe \d+( \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\): (\d+) members species=([0-9A-F]+)', line)
        if m:
            out.append((m.group(1) is not None, tuple(float(m.group(i)) for i in (2, 3, 4)), m.group(6), []))
            continue
        m = re.match(r'\s+#(\d+) \S+ .*? pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and out:
            out[-1][3].append((int(m.group(1)), tuple(float(m.group(i)) for i in (2, 3, 4))))
    return out


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(3)
    before = tribes(host)
    print(f'  host: {len(before)} tribes, guest: {len(tribes(guest))} tribes', flush=True)
    out = host.call('coop.tribe npc_tribe 4 70')['out'].strip()
    print('  host:', out, flush=True)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\): (\d+) members', out)
    ck(m and int(m.group(4)) > 0, 'the host\'s game formed an NPC tribe with members')
    if not m:
        return ck.result()
    at = tuple(float(m.group(i)) for i in (1, 2, 3))
    time.sleep(2)
    hnew = [t for t in tribes(host) if not t[0] and dist(t[1], at) < 5]
    ck(hnew and hnew[0][3], f'the host lists its new tribe with members ({hnew[0][2] if hnew else "-"})')
    if not hnew:
        return ck.result()
    ht = hnew[0]
    best, t0 = None, time.time()
    while time.time() - t0 < 15:
        g = [t for t in tribes(guest) if not t[0] and dist(t[1], ht[1]) < 10]
        best = g[0] if g else None
        if best and len(best[3]) >= len(ht[3]):
            break
        time.sleep(1)
    ht = [t for t in tribes(host) if not t[0] and dist(t[1], at) < 5][0]  # its members now
    print(f'  host tribe at {tuple(round(v) for v in ht[1])} species {ht[2]} members {[i for i, _ in ht[3]]}', flush=True)
    print(f'  guest tribe: {(tuple(round(v) for v in best[1]), best[2], [i for i, _ in best[3]]) if best else None}',
          flush=True)
    ck(best is not None, 'the guest\'s game has a tribe where the host\'s new tribe is (within 10 m)')
    if best:
        ck(best[2] == ht[2], f'of the same species (host {ht[2]}, guest {best[2]})')
        gm = dict(best[3])
        missing = [i for i, _ in ht[3] if i not in gm]
        ck(not missing, f'with every member of the host\'s tribe (missing on the guest: {missing})')
        far = [(i, round(dist(p, gm[i]), 1)) for i, p in ht[3] if i in gm and dist(p, gm[i]) > 8]
        ck(not far, f'each within 8 m of the host\'s member (farther: {far})')
    for name, con in (('host', host), ('guest', guest)):  # every tribe (the game may form more meanwhile)
        print(f'  {name} tribes:', [(tuple(round(v) for v in t[1]), t[2], len(t[3])) for t in tribes(con)], flush=True)
    st = [l for l in guest.call('coop.tribe')['out'].splitlines() if 'npc_tribes_made' in l]
    print(' ', st[0][-60:] if st else '(no guest status line)', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
