#!/usr/bin/env python3
"""BUG-0734 (class BUG-0418): animals hostile to the players' species go for a guest playing its own creature.

The behaviour tree's node 0x04A1A1E6 (its condition 0xD7D9F0) makes an animal attack an adult of the world's
player species that it is hostile to (relation 2 or 5): `candidate +0xB20 == PlayerSpecies()`, a direct compare.
A guest's own creature is another species in the host's game: its puppet was never picked, the hostile animals went
past the guest (the npc.partners AI swap covers the avatar-oriented nodes, not this one).

1. Two games on the creature fixture; the guest plays its own creature.
2. The host hits the nearest animal three times (`coop.npc hosthit`): the species' relation to the players becomes
   "attacked" (2; the guest's game follows the host's table). The host's game lists the adult animals hostile to the guest's creature
   (`coop.hp hostiles 1`: the game's own GetRelationship(animal, puppet)).
3. For 40 s the host's game is read: which animals target the guest's puppet (+0xE7C, the creature or its combatant),
   without any command telling them to; the animal that was hit is left out (it fights back by itself).

PASS: another animal of the hostile species targets the guest's creature, and the node's party answer picked it.
FAIL (SPORE_FIX_OFF=creature.hostile_party): the node never picks the guest.

  SPORE_LANE=7 python3 tools/tests/coop_hostile_party.py
"""
import re
import time

from own_creature import back_to_world, play_own
from twoinst import Checks, start


def hostiles(host):
    out = host.call('coop.hp hostiles 1')['out']
    m = re.search(r'hostiles (\d+) targeting=(\d+) picks=(\d+)', out)
    rows = re.findall(r'hostile (\S+) d=([\d.]+) at=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    who = re.findall(r'targeting (\S+)', out)
    return (tuple(int(x) for x in m.groups()) if m else (0, 0, 0)), rows, who


def npcs(con):
    """host: {id: (ptr, species, dist, dead)}"""
    return {int(m.group(1)): (m.group(2), m.group(3), float(m.group(5)), bool(m.group(6)))
            for m in re.finditer(r'#(\d+) ([0-9A-F]{8}) species=([0-9A-F]+) hp=([\d.]+) dist=([\d.]+)( DEAD)?',
                                 con.call('coop.npc list')['out'])}


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    ck(play_own(guest), 'the guest plays one of its own creatures (not the world\'s species)')
    time.sleep(4)
    # the guest attacks one animal: its species' relation to the players becomes "attacked" (2, ApplyEvent 2 in the
    # host's TakeDamage), the relation node 0x04A1A1E6 reads
    m = re.search(r'world player species ([0-9A-F]{8})', host.call('coop.creations')['out'])
    own = m.group(1) if m else ''
    # an animal of another species than the host's (its family is the host's party)
    ns = sorted(((v[2], k, v) for k, v in npcs(host).items() if not v[3] and v[1] != own), key=lambda x: x[0])
    print(f'  host species {own}', flush=True)
    ck(ns, 'the host has animals near the players')
    if not ns:
        ck.faults(host=host, guest=guest)
        return ck.result()
    _, nid, (ptr, species, _, _) = ns[0]
    for _ in range(3):  # (a guest's hit lands as the puppet's: the record is the host avatar's attack, BUG-0462 table)
        print('  host:', host.call(f'coop.npc hosthit {nid} 0.5')['out'].strip(), flush=True)
        time.sleep(1)
    (n, tg, pk), rows, who = hostiles(host)
    print('  host:', host.call('coop.hp hostiles 1')['out'].splitlines()[0], flush=True)
    if not rows:  # no species of the fixture is hostile (relation 2 / 5) by its tuning: an epic is
        print('  host:', host.call('coop.npc epic 1 25')['out'].strip(), flush=True)
        time.sleep(3)
        (n, tg, pk), rows, who = hostiles(host)
        print('  host:', host.call('coop.hp hostiles 1')['out'].splitlines()[0], flush=True)
    print(f'  host: hit #{nid} {ptr} ({species}); {n} hostile adults to the guest now, nearest {rows[:3]}', flush=True)
    ck(rows, 'animals of the host\'s world are hostile to the guest\'s creature after its attack')
    best, others = (0, 0, 0), []
    t0 = time.time()
    while rows and time.time() - t0 < 40:
        time.sleep(2)
        (n, tg, pk), rows2, who = hostiles(host)
        others = [w for w in who if w != ptr]  # (the attacked animal fights back by itself: TakeDamage)
        best = max(best, (len(others), pk, n))
        if others and pk > 0:
            break
    print(f'  host: other animals targeting the guest {best[0]}, party picks {best[1]}, hostiles {best[2]}', flush=True)
    print('  host:', [ln for ln in host.call('coop.hp')['out'].splitlines() if ln.startswith(('party', 'relations'))],
          flush=True)
    ck(best[1] > 0, f'the hostile-species node picked the guest\'s creature (party picks {best[1]})')
    ck(best[0] > 0, f'an animal of the hostile species that the guest did not hit targets the guest ({best[0]})')
    back_to_world(guest)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
