#!/usr/bin/env python3
"""BUG-0894: a NaN velocity in the host's NPC snapshots never puts a guest's proxy at a NaN place.

The guest's NPC proxies are driven from the host's states (npc.cpp): the velocity is written raw into the body
(kVelOff) or turned into the walk speed (SetDesiredSpeed), unchecked; BUG-0614 guards positions and rotations only.

1. Two games on the creature fixture, the guest next to the host (it mirrors the host's NPCs).
2. For 6 s the host's NPC states carry a NaN velocity and the in-the-air flag (`coop.npc nanvel 6 -1 air`: a host NPC
   thrown with a NaN velocity of its own game; off the ground the guest's driver writes the velocity into the body),
   then good states again.
PASS, on the guest's game: every spawned proxy keeps a finite game position (`coop.npc list` at=, the creature's
position) during and 5 s after, no fault. FAIL (SPORE_FIX_OFF=game.finite_vel): proxies at NaN.

  SPORE_LANE=7 python3 tools/tests/coop_npc_nan_vel.py
"""
import math
import re
import time

from twoinst import Checks, start

ROW = re.compile(r'^\s*#(\d+) species=\S+ spawned .*? at=\(([^ ]+) ([^ ]+) ([^)]+)\)', re.M)


def proxies(guest):
    out = {}
    for m in ROW.finditer(guest.call('coop.npc list')['out']):
        try:
            out[int(m.group(1))] = tuple(float(m.group(i)) for i in (2, 3, 4))
        except ValueError:
            out[int(m.group(1))] = (math.nan,) * 3
    return out


def main():
    host, guest = start('creature')
    ck = Checks()
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.avatar')['out'])
    if m:
        guest.call('coop.npc gopos %.2f %.2f %.2f' % (float(m.group(1)) + 3, float(m.group(2)), float(m.group(3))))
    time.sleep(6)
    p0 = proxies(guest)
    ck(len(p0) >= 3, f'the guest mirrors the host\'s NPCs ({len(p0)} proxies)')
    print('  host:', host.call('coop.npc nanvel 6 -1 air')['out'].strip(), flush=True)
    bad = set()
    seen = set()
    t0 = time.time()
    while time.time() - t0 < 11:
        for i, p in proxies(guest).items():
            seen.add(i)
            if not all(math.isfinite(v) and abs(v) < 1e6 for v in p):
                bad.add(i)
        time.sleep(0.5)
    print(f'  {len(seen)} proxies watched, at a non-finite place: {sorted(bad)}', flush=True)
    print('  host:', host.call('coop.npc nanvel 0')['out'].strip(), flush=True)
    ck(not bad, f"no proxy of the guest's game goes to a NaN place ({len(bad)} did)")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
