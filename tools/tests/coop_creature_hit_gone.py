#!/usr/bin/env python3
"""Creature: a guest's hit on an animal the host's game removed meanwhile is dropped, not applied to the freed creature
(CR-2 of the external audit, BUG-1428).

The host reads a guest's hit (MSG_DAMAGE_REQ) in frame_receive, before this frame's NPC pass rebuilt the list of live
creatures, and with no pass at all while the host has no avatar or is in its editor. The handler asked that list only:
an animal the host's game removed since (a decayed or eaten body, a far animal it culls) still counted, and the hit
went to the game's TakeDamage on the freed creature (a crash, or a hit on whatever reused the memory).

1. Creature fixture. On the guest a spawned proxy of a host NPC outside the host's pack and family.
2. The host's game removes that NPC (`coop.npc gone <id> 6`: the game's own DestroyInstance, no bookkeeping of the mod)
   and its NPC pass holds 6 s (as in its editor): the guest still has the proxy, the host's tables still name it.
3. The guest hits the proxy (`coop.npc hit <id> 1`: the game's damage filter sends MSG_DAMAGE_REQ).
4. PASS: the host applied no hit (hits_applied unchanged), it refused one (msg_gone_refused + 1), no game crashed.
   SPORE_FIX_OFF=npc.msg_live: the hit goes to the freed creature (hits_applied + 1, or the host crashes): FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_creature_hit_gone.py
"""
import re
import time

from harness import waiter
from twoinst import Checks, start

wait = waiter(step=0.5)


def counter(con, name):
    m = re.search(name + r'=(\d+)', con.call('coop.npc')['out'])
    return int(m.group(1)) if m else None


def target(host, guest):
    """the id of a proxy the guest has spawned of a host NPC outside the host's pack and family, or None"""
    party = set()
    for ln in host.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) .*? (pack|family)\b', ln)
        if m:
            party.add(m.group(1))
    for ln in guest.call('coop.npc list')['out'].splitlines():
        s = ln.strip()
        if s.startswith('#') and 'spawned' in s and 'DEAD' not in s:
            nid = s.split()[0].lstrip('#')
            if nid not in party:
                return nid
    return None


def main():
    host, guest = start('creature')
    ck = Checks()
    nid = wait(lambda: target(host, guest), 40)
    if not ck(nid, 'the guest has a proxy of a host NPC outside the host\'s pack and family'):
        return ck.result()
    a0, r0 = counter(host, 'hits_applied'), counter(host, 'msg_gone_refused')
    out = host.call(f'coop.npc gone {nid} 6')['out'].strip()
    print(f'  host: {out}', flush=True)
    if not ck('removed by the game' in out and 'alive now 0' in out, f'the host\'s game removed NPC #{nid} ({out})'):
        return ck.result()
    time.sleep(0.5)
    print('  guest: ' + guest.call(f'coop.npc hit {nid} 1')['out'].strip(), flush=True)
    time.sleep(2)
    a1, r1 = counter(host, 'hits_applied'), counter(host, 'msg_gone_refused')
    print(f'  host: hits_applied {a0} -> {a1}, msg_gone_refused {r0} -> {r1}', flush=True)
    ck(a1 is not None and a1 == a0, f'the host applied no hit to the removed NPC (hits_applied {a0} -> {a1})')
    ck(r1 is not None and r0 is not None and r1 == r0 + 1, f'the host refused the hit (msg_gone_refused {r0} -> {r1})')
    time.sleep(6)  # the pass runs again: the despawn goes out, both games go on
    ck(wait(lambda: host.call('coop.avatar')['ok'] and guest.call('coop.avatar')['ok'], 15), 'both games go on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
