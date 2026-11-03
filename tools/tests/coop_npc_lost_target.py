#!/usr/bin/env python3
"""BUG-0453 / BUG-0456: an NPC fighting the partner's puppet when the puppet goes (Creature, host).

Two host crashes on 0.7.3.3 / 0.7.3.4 at 0xD65D23 (read of 0) inside the creature attack node
0x02D852E6, and one abort (pure virtual call, 0x40000015 at 0xD8F2AD) in the herd-help node: both
right after the partner's puppet was taken away while NPCs fought it (the partner died, was hidden,
shown again). The game itself clears the target of every NPC aimed at a destroyed creature
(+0xE7C = 0), but the attack node skips its own "no target" check while its attack animation plays
and then calls the missing target (BUG-0453); and the NPCs' stimuli keep naming the destroyed puppet
for seconds, the AI reads the sender later (BUG-0456).

Rounds on the host's world: the guest stands next to a host NPC (not of the host's family) and hits
it through the damage filter (`coop.npc hit`, the hit is applied on the host with the guest's puppet
as the attacker), until the NPC is in its attack node with the puppet as its target; after a short
random time the host takes the puppet away (`coop.puppet respawn 3`: as on a death or a hide, it is
shown again 3 s later). Checks:
* the host does not crash or fault (the game's own exception counter, `ck.faults`);
* right after each removal no NPC stimulus names the destroyed puppet (read from the NPCs' AI blocks
  on the host: +0xB4C, +8, 8 slots of 0x38 bytes, sender +0x2C);
* at least MIN_REMOVALS removals hit an NPC while it attacked the puppet (else the test proves nothing).
Then the state the hosts crashed in, made on purpose (BUG-0453; a removal hits it only when the attack
animation plays at that frame): `coop.attackcut` clears the target of the next NPC whose attack
animation plays, through the creature's own SetTarget (vt 0x84, null), as the game does on a destroy;
the host must go on (ARMED_CUTS times).

  coop_npc_lost_target.py [--rounds N]
  (SPORE_FIX_OFF=npc.attack_lost_target,npc.stale_sender: the host crashes or a stale sender stays, FAILS)"""
import argparse
import random
import re
import struct
import time

from coop_creature_kills import family, npcs, proxies, read
from twoinst import Checks, start

ATTACK_NODE = 0x02D852E6
MIN_REMOVALS = 4
ARMED_CUTS = 2
HIDE_S = 3.0  # s the puppet stays away (a partner's death hides it for seconds)


def u32(con, a):
    b = read(con, a, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else None


def puppet(con):
    m = re.search(r'puppet=([0-9A-F]{8})', con.call('coop.status')['out'])
    return int(m.group(1), 16) if m else 0


def ai_state(con, ptr):
    """(target, node) of a host NPC"""
    ai = u32(con, ptr + 0xB4C)
    return u32(con, ptr + 0xE7C), (u32(con, ai + 0x1D8) if ai else None)


def senders(con, ptr):
    """the senders of the NPC's live stimuli"""
    ai = u32(con, ptr + 0xB4C)
    b = read(con, ai + 8, 8 * 0x38) if ai else b''
    out = []
    for i in range(len(b) // 0x38):
        lo, hi = struct.unpack_from('<II', b, i * 0x38)
        if lo or hi:
            out.append(struct.unpack_from('<I', b, i * 0x38 + 0x2C)[0])
    return out


def engage(host, guest, nid, ptr, pup):
    """the guest hits host NPC nid until it attacks the puppet: (target, node) at the end"""
    guest.call(f'coop.social goto {nid}')
    time.sleep(0.8)
    tgt = node = None
    for _ in range(6):
        guest.call(f'coop.npc hit {nid} 0.3')
        time.sleep(0.4)
        tgt, node = ai_state(host, ptr)
        if tgt == pup + 0x5A8 and node == ATTACK_NODE:
            break
    return tgt, node


def test_cuts(host):
    m = re.search(r'test cuts (\d+)', host.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=24)
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
    removals = stale_total = armed_cuts = 0
    for rnd_i in range(a.rounds):
        if not cand or (removals >= 6 and armed_cuts >= ARMED_CUTS):
            break
        nid = cand[rnd_i % min(len(cand), 4)]
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
            print(f'  round {rnd_i}: NPC #{nid} did not attack the puppet (target {tgt and hex(tgt)}, node '
                  f'{node and hex(node)})', flush=True)
            continue
        if removals >= 6:
            # 2. the crash state made on purpose: the target cleared while the attack animation plays
            c0 = test_cuts(host)
            host.call('coop.attackcut')
            t0 = time.time()
            while time.time() - t0 < 4 and test_cuts(host) == c0:
                guest.call(f'coop.npc hit {nid} 0.3')
                time.sleep(0.3)
            done = test_cuts(host) > c0
            armed_cuts += done
            print(f'  round {rnd_i}: NPC #{nid} lost its target in its attack animation: {done}', flush=True)
            time.sleep(1.5)
            continue
        # 1. the puppet goes while an NPC attacks it (as on the partner's death)
        time.sleep(rnd.uniform(0.1, 1.6))  # into its attack
        host.call(f'coop.puppet respawn {HIDE_S}')
        time.sleep(0.05)
        stale = sum(s == pup for i in cand[:6] if npcs(host).get(i) for s in senders(host, npcs(host)[i][0]))
        tgt2, node2 = ai_state(host, ptr)
        removals += 1
        stale_total += stale
        print(f'  round {rnd_i}: NPC #{nid} attacked the puppet; removed: target {tgt2 and hex(tgt2)} node '
              f'{node2 and hex(node2)}, stimuli naming it {stale}', flush=True)
        time.sleep(HIDE_S + 1.0)  # the puppet is back
    st = host.call('coop.status')['out']
    print('  host', host.call('exc')['out'].splitlines()[0], '|',
          re.search(r'attacks ended without a target.*?\(destroys \d+\)', st).group(0), flush=True)
    ck(removals >= MIN_REMOVALS, f'the puppet was removed while an NPC attacked it {removals} times (>= {MIN_REMOVALS})')
    ck(stale_total == 0, f'no NPC stimulus named the removed puppet ({stale_total} did)')
    ck(armed_cuts >= ARMED_CUTS, f'NPCs lost their target in their attack animation {armed_cuts} times (>= {ARMED_CUTS}) '
                                 'and the host went on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
