#!/usr/bin/env python3
"""BUG-0930: the NPC a partner's puppet hit last lets go of the puppet when co-op destroys it (Creature, host).

The rounds of coop_stale_refs.py: the guest hits a host NPC until the NPC attacks the guest's puppet in the host's
game (the NPC's last attacker, combatant +0x28 = creature +0x5D0, is then the puppet's combatant). The host takes the
puppet away as on the partner's death (`coop.puppet respawn`). Checked in the host's own memory, every round:
* the NPC's +0x5D0 no longer names the destroyed puppet 1 s after the destroy;
* the destroyed puppet's block is freed (its cCreatureAnimal vtable 0x146A080 gone) within 6 s.
Without the fix (SPORE_FIX_OFF=npc.stale_last_attacker) the NPC keeps its reference and the block lives on with
count 1 until that NPC is hit by somebody else (25 s and more). No fault on either game.

  SPORE_LANE=4 python3 tools/tests/coop_stale_last_attacker.py [--rounds 3]
"""
import argparse
import random
import struct
import time

from coop_creature_kills import family, npcs, proxies, read
from coop_npc_lost_target import ATTACK_NODE, engage, puppet, u32
from twoinst import Checks, start

ANIMAL_VT = 0x0146A080
LAST_ATTACKER = 0x5A8 + 0x28


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=3)
    ap.add_argument('--hide', type=float, default=4.0)
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    host, guest = start()
    ck = Checks()
    fam = family(host)
    prox = proxies(guest)
    cand = [i for _, i in sorted((n[3], i) for i, n in npcs(host).items()
                                 if i in prox and not n[4] and n[2] > 0 and n[1] != fam)]
    ck(bool(cand), f'host NPCs of other species mirrored on the guest ({len(cand)})')
    removals, held_after, kept_alive = 0, [], []
    for r in range(a.rounds * 5):  # (an NPC that does not take the bait, dies or flees: the next one)
        if not cand or removals >= a.rounds:
            break
        nid = cand[r % min(len(cand), 4)]
        n = npcs(host).get(nid)
        if not n or n[4]:
            cand.remove(nid)
            continue
        ptr, pup = n[0], puppet(host)
        if not pup:
            time.sleep(1)
            continue
        tgt, node = engage(host, guest, nid, ptr, pup)
        last = u32(host, ptr + LAST_ATTACKER)
        if not (tgt == pup + 0x5A8 and node == ATTACK_NODE and last == pup + 0x5A8):
            print(f'  round {r}: NPC #{nid} did not attack the puppet or was not hit by it last '
                  f'(target {tgt and hex(tgt)}, last attacker {last and hex(last)})', flush=True)
            continue
        time.sleep(rnd.uniform(0.1, 1.0))
        host.call(f'coop.puppet respawn {a.hide}')
        removals += 1
        t0, freed, last_now = time.time(), None, None
        while time.time() - t0 < 6:
            b = read(host, pup, 4)
            vt = struct.unpack('<I', b)[0] if len(b) == 4 else None
            if time.time() - t0 >= 1.0 and last_now is None:
                last_now = u32(host, ptr + LAST_ATTACKER)
            if vt != ANIMAL_VT:
                freed = time.time() - t0
                break
            time.sleep(0.1)
        if last_now is None:
            last_now = u32(host, ptr + LAST_ATTACKER)
        names = last_now is not None and pup <= last_now < pup + 0x1700
        print(f'  round {r}: NPC #{nid}, puppet {pup:08X} destroyed: the NPC\'s last attacker after 1 s '
              f'{last_now and hex(last_now)}{" (the puppet)" if names else ""}, the block freed after '
              f'{freed and round(freed, 2)} s', flush=True)
        if names:
            held_after.append(nid)
        if freed is None:
            kept_alive.append(nid)
        time.sleep(a.hide + 1.0)
    ck(removals >= a.rounds, f'the puppet was removed in a fight while an NPC held it as its last attacker '
                             f'{removals} times (wanted {a.rounds})')
    ck(not held_after, f'no NPC names the destroyed puppet as its last attacker 1 s after the destroy '
                       f'({len(held_after)} of {removals} did: {held_after})')
    ck(not kept_alive, f'the destroyed puppet\'s block is freed within 6 s ({len(kept_alive)} of {removals} '
                       f'were still the creature)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
