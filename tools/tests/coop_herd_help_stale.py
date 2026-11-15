#!/usr/bin/env python3
"""BUG-0926: the herd mates of an NPC the partner fought name the partner's destroyed puppet as the attacker (Creature, host).

The 0.7.12 final check's long_creature host and lab 20261111-064457 stopped in the C runtime's error box (R6025, a
pure virtual function call) 0.4-2 s after the partner's creature died in a hunt: the herd-help node of an NPC's
behaviour tree (BT_HerdHelpEval 0xD8F1C0) called the attacker named by an "attacked" stimulus (slot +0x10, no
reference), and that was the partner's puppet, destroyed and freed. The puppet's block lives while references to it
remain; an NPC it hit keeps one (its last attacker), so it is freed at once only when that NPC is no more: the
hunt's prey killed just before the partner dies.

Rounds on the host's world, the order of the hunt: the guest stands next to a host NPC (not of the host's family) and
hits it until it attacks the guest's puppet (its herd mates get the "attacked" stimulus with the puppet as the
attacker); the host's own creature kills that NPC; the host takes the puppet away as on the partner's death
(`coop.puppet respawn`). Checks:
* right after the removal no NPC stimulus of the host's game names the removed puppet, neither as its sender (+0x2C)
  nor as its second creature (+0x10): read from the NPCs' AI blocks (+0xB4C, +8, 8 slots of 0x38 bytes);
* the host goes on drawing for 6 s after every removal, shows no dialog and has no fault.
SPORE_FIX_OFF=npc.stale_attacker must FAIL (stimuli name the puppet as the attacker; the host stops in the runtime's
error box within seconds: R6025, BT_HerdHelpEval in the main thread's chain).

  SPORE_LANE=2 python3 tools/tests/coop_herd_help_stale.py [--rounds 6]
"""
import argparse
import random
import re
import struct
import time

from coop_creature_kills import family, npcs, proxies, read
from coop_npc_lost_target import ATTACK_NODE, engage, puppet, u32
from twoinst import Checks, start
import lane

SPAN = 0x1700  # a creature's block (the sender is a sub-object of it)


def named(con, ptr, pup):
    """(as sender, as the second creature) counts of the NPC's live stimuli that name the creature at pup"""
    ai = u32(con, ptr + 0xB4C)
    b = read(con, ai + 8, 8 * 0x38) if ai else b''
    snd = oth = 0
    for i in range(len(b) // 0x38):
        lo, hi = struct.unpack_from('<II', b, i * 0x38)
        if not (lo or hi):
            continue
        o, s = struct.unpack_from('<I', b, i * 0x38 + 0x10)[0], struct.unpack_from('<I', b, i * 0x38 + 0x2C)[0]
        snd += pup <= s < pup + SPAN
        oth += pup <= o < pup + SPAN
    return snd, oth


def frame(con):
    m = re.search(r'frame=(\d+)', con.call('frame').get('out', ''))
    return int(m.group(1)) if m else -1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=6)
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    host, guest = start()
    ck = Checks()
    path = lane.game_log(lane.HOST)
    size0 = len(open(path, errors='replace').read())
    fam = family(host)
    prox = proxies(guest)
    cand = [i for _, i in sorted((n[3], i) for i, n in npcs(host).items()
                                 if i in prox and not n[4] and n[2] > 0 and n[1] != fam)]
    ck(bool(cand), f'host NPCs of other species mirrored on the guest ({len(cand)})')
    removals = stale_snd = stale_oth = before_oth = 0
    stopped = None
    for r in range(a.rounds * 3):
        if not cand or removals >= a.rounds or stopped:
            break
        nid = cand[0]
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
            cand.remove(nid)
            continue
        time.sleep(rnd.uniform(0.1, 0.8))
        others = {i: m[0] for i, m in npcs(host).items() if not m[4]}
        b_oth = sum(named(host, p, pup)[1] for p in others.values())
        host.call(f'coop.npc hit {nid} 1000')  # the hunt's kill, by the host's creature
        time.sleep(0.1)
        f0 = frame(host)
        host.call('coop.puppet respawn 7')
        time.sleep(0.05)
        try:
            cnt = [named(host, p, pup) for p in others.values()]
        except (OSError, ValueError) as e:
            cnt = []
            print(f'  round {r}: the host did not answer right after the removal ({e!r})', flush=True)
        snd, oth = sum(c[0] for c in cnt), sum(c[1] for c in cnt)
        removals += 1
        stale_snd += snd
        stale_oth += oth
        before_oth += b_oth
        cand.remove(nid)
        t0, f1 = time.time(), f0
        while time.time() - t0 < 6 and not stopped:
            time.sleep(1)
            try:
                f2 = frame(host)
            except (OSError, ValueError):
                f2 = f1
            if f2 == f1:
                stopped = f'round {r}, {time.time() - t0:.0f} s after the removal'
            f1 = f2
        print(f'  round {r}: NPC #{nid} fought the puppet {pup:08X} ({b_oth} stimuli of {len(others)} NPCs named it as the '
              f'attacker), killed by the host, the puppet removed: stimuli naming it as sender {snd}, as attacker {oth}; '
              f'host frames {f0} -> {f1}' + (f' STOPPED ({stopped})' if stopped else ''), flush=True)
        if not stopped:
            time.sleep(3.0)  # the puppet is back
    new = open(path, errors='replace').read()[size0:]
    box = [l for l in new.splitlines() if 'the main thread shows a dialog' in l]
    for l in box:
        print('  host log: ' + l[:300], flush=True)
    if 'BT_HerdHelpEval' in new:
        print('  host log: BT_HerdHelpEval is in the main thread\'s call chain', flush=True)
    ck(removals >= min(4, a.rounds) or bool(stopped), f'the puppet was removed after the kill of the NPC it fought {removals} times')
    ck(before_oth > 0, f'before the removals NPC stimuli named the puppet as the attacker ({before_oth}: else the test proves nothing)')
    ck(stale_snd == 0 and stale_oth == 0,
       f'no NPC stimulus named the removed puppet (as sender {stale_snd}, as the attacker {stale_oth})')
    ck(not stopped and not box, f'the host went on drawing after every removal and showed no error box ({stopped or "ok"})')
    if not stopped:
        ck.faults(host=host, guest=guest)
    else:
        ck.faults(guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
