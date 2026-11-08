#!/usr/bin/env python3
"""BUG-0734 (class BUG-0418): animals hostile to the players' species go for a guest playing its own creature.

The behaviour tree's node 0x04A1A1E6 (its condition 0xD7D9F0) makes an animal attack an adult of the world's
player species that it is hostile to (relation 2 or 5): `candidate +0xB20 == PlayerSpecies()`, a direct compare.
A guest's own creature is another species in the host's game: its puppet was never picked, the hostile animals went
past the guest (the npc.partners AI swap covers the avatar-oriented nodes, not this one).

1. Two games on the creature fixture; the guest plays its own creature.
2. The host's game lists the adult animals hostile to the guest's creature (`coop.hp hostiles 1`: the game's own
   GetRelationship(animal, puppet)); the guest goes next to the nearest one (`coop.npc gopos`) and stands still.
3. For 40 s the host's game is read: how many animals target the guest's puppet (+0xE7C, the target the attack
   node takes), without any command telling them to.

PASS: an animal of the host's game targets the guest's creature, and the node's party answer picked it.
FAIL (SPORE_FIX_OFF=creature.hostile_party): the node never picks the guest.

  SPORE_LANE=7 python3 tools/tests/coop_hostile_party.py
"""
import re
import time

from own_creature import play_own
from twoinst import Checks, start


def hostiles(host):
    out = host.call('coop.hp hostiles 1')['out']
    m = re.search(r'hostiles (\d+) targeting=(\d+) picks=(\d+)', out)
    rows = re.findall(r'hostile (\S+) d=([\d.]+) at=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    return (tuple(int(x) for x in m.groups()) if m else (0, 0, 0)), rows


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    ck(play_own(guest), 'the guest plays one of its own creatures (not the world\'s species)')
    time.sleep(4)
    (n, tg, pk), rows = hostiles(host)
    print(f'  host: {n} hostile adults to the guest, {tg} targeting it, picks {pk}; nearest {rows[:3]}', flush=True)
    ck(rows, 'the host\'s world has animals hostile to the guest\'s creature')
    if not rows:
        ck.faults(host=host, guest=guest)
        return ck.result()
    _, d, x, y, z = rows[0]
    print('  guest:', guest.call(f'coop.npc gopos {x} {y} {z}')['out'].strip()[:100], flush=True)
    best = (0, 0, 0)
    t0 = time.time()
    while time.time() - t0 < 40:
        time.sleep(2)
        (n, tg, pk), rows = hostiles(host)
        best = max(best, (tg, pk, n))
        if tg > 0 and pk > 0:
            break
    print(f'  host: targeting {best[0]}, party picks {best[1]}, hostiles {best[2]}', flush=True)
    print('  host:', [ln for ln in host.call('coop.hp')['out'].splitlines() if ln.startswith(('party', 'relations'))],
          flush=True)
    ck(best[1] > 0, f'the hostile-species node picked the guest\'s creature (party picks {best[1]})')
    ck(best[0] > 0, f'an animal of the host\'s game targets the guest\'s creature ({best[0]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
