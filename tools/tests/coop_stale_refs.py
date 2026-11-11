#!/usr/bin/env python3
"""BUG-0926 (RE): who still names the partner's puppet after it is destroyed in a fight (Creature, host).

The rounds of coop_npc_lost_target.py: the guest stands next to a host NPC and hits it until the NPC attacks the
guest's puppet in the host's game; the host then takes the puppet away as on the partner's death (`coop.puppet
respawn <s>`). 0.3 s and 2 s later `coop.staleref <puppet>` on the host lists every pointer into the destroyed
puppet's block that the process still holds, by where it sits (a listed object and the offset, a creature's AI
block, a module's data, elsewhere). Report only (--strict: a pointer inside a listed object or an AI block fails).

  SPORE_LANE=2 python3 tools/tests/coop_stale_refs.py [--rounds 8] [--strict]
"""
import argparse
import random
import re
import struct
import time

from coop_creature_kills import family, npcs, proxies, read
from coop_npc_lost_target import ATTACK_NODE, engage, puppet
from twoinst import Checks, start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=8)
    ap.add_argument('--hide', type=float, default=4.0)
    ap.add_argument('--strict', action='store_true')
    ap.add_argument('--host-hit', type=int, default=0, help='the host hits the NPC this many times after the removal')
    ap.add_argument('--kill-first', action='store_true', help='the host kills the NPC right before the removal')
    ap.add_argument('--watch', type=float, default=0, help='watch the destroyed block for this many seconds first')
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    host, guest = start()
    ck = Checks()
    print('  host:', host.call('coop.staleref on')['out'].strip(), flush=True)
    pup = puppet(host)
    if pup:  # a living puppet's vtables, to compare
        b = read(host, pup, 0x5B0)
        if len(b) == 0x5B0:
            w = lambda o: struct.unpack_from('<I', b, o)[0]  # noqa: E731
            print(f'  a living puppet: vt {w(0):08X} +4 {w(4):08X} +8 {w(8):08X}, +0xC0 vt {w(0xC0):08X} +0xC4 {w(0xC4):08X}, '
                  f'+0x5A8 vt {w(0x5A8):08X} +0x5AC {w(0x5AC):08X}', flush=True)
    fam = family(host)
    prox = proxies(guest)
    cand = [i for _, i in sorted((n[3], i) for i, n in npcs(host).items()
                                 if i in prox and not n[4] and n[2] > 0 and n[1] != fam)]
    ck(bool(cand), f'host NPCs of other species mirrored on the guest ({len(cand)})')
    removals, held = 0, {}
    for r in range(a.rounds * 3):
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
        if not (tgt == pup + 0x5A8 and node == ATTACK_NODE):
            print(f'  round {r}: NPC #{nid} did not attack the puppet', flush=True)
            continue
        time.sleep(rnd.uniform(0.1, 1.6))
        if a.kill_first:  # the host's creature kills the NPC right before the partner's puppet goes
            print('  host kills it:', host.call(f'coop.npc hit {nid} 1000')['out'].strip()[:60], flush=True)
            time.sleep(0.1)
        host.call(f'coop.puppet respawn {a.hide}')
        removals += 1
        if a.host_hit:  # the host's own creature hits the NPC: the NPC's last attacker is no longer the puppet
            for _ in range(a.host_hit):
                print('  host:', host.call(f'coop.npc hit {nid} 0.2')['out'].strip()[:60], flush=True)
                time.sleep(0.15)
        if a.watch:
            # the destroyed puppet's block over time: its vtable and reference count (+8) until it is freed, then who
            # still names it
            t0, last, freed = time.time(), None, None
            while time.time() - t0 < a.watch:
                b = read(host, pup, 12)
                cur = struct.unpack('<III', b) if len(b) == 12 else None
                if cur != last:
                    print(f'  round {r} +{time.time() - t0:5.2f} s: block vt {cur[0]:08X} +4 {cur[1]:08X} refs {cur[2]}' if cur
                          else f'  round {r} +{time.time() - t0:5.2f} s: block unreadable', flush=True)
                    last = cur
                if cur and cur[0] != 0x0146A080 and freed is None:
                    freed = time.time() - t0
                    break
                time.sleep(0.1)
            print(f'  round {r}: the puppet\'s block was freed after {freed and round(freed, 2)} s', flush=True)
        for wait, label in ((0.3, '0.3 s'),) if a.watch else ((0.3, '0.3 s'), (1.7, '2 s')):
            time.sleep(wait)
            out = host.call(f'coop.staleref {pup:X}')['out']
            lines = out.splitlines()
            print(f'  round {r} NPC #{nid}, {label}: {lines[0].strip() if lines else "-"}', flush=True)
            for l in lines[1:]:
                if l.startswith('  other'):
                    continue
                if l.startswith('  block now'):
                    print('     ' + l.strip(), flush=True)
                    continue
                print('     ' + l.strip(), flush=True)
                m = re.match(r'\s*(object|ai|module) (?:noun=(\w+) )?(\S*\+0x[0-9A-F]+)', l)
                if m and m.group(1) != 'module':
                    held[(m.group(1), m.group(2), m.group(3), label)] = held.get((m.group(1), m.group(2), m.group(3), label), 0) + 1
            print(f'     ({sum(1 for l in lines[1:] if l.startswith("  other"))} groups elsewhere on the heap)', flush=True)
        time.sleep(a.hide + 1.0)
    for k, v in sorted(held.items()):
        print(f'  held in {v} of {removals} removals: {k[0]} noun={k[1]} {k[2]} at {k[3]}', flush=True)
    ck(removals >= min(4, a.rounds), f'the puppet was removed in a fight {removals} times')
    if a.strict:
        ck(not held, f'no listed object or AI block names the destroyed puppet ({len(held)} places did)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
