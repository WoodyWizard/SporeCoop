#!/usr/bin/env python3
"""Creature: an NPC stunned in the host's game is stunned on the guest's screen too (BUG-0700; class BUG-0419: state
of a mirror not applied after its spawn).

1. Two games on the creature fixture; a wild NPC the guest mirrors (`coop.npc list`: spawned on the guest).
2. The host's game stuns it (`coop.rules stun 3 <id>`: the game's CreatureStun through the hook, as a charge's effect
   calls it): the host's NPC holds the stun stimulus 0x1000 (its behaviour tree's 8 stimulus slots, AI block +0xB4C).
3. Within 2 s the guest's proxy of it holds the stimulus 0x1000 too (the game's own stun on the guest: its stars).
0.7.10 (SPORE_FIX_OFF=creature.stun_proxy): the stun stays in the host's game, the proxy never has it.
"""
import re
import time

from coop_creature_stun import stunned
from twoinst import Checks, start


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team off')
    mirrored = {}
    for _ in range(10):
        mirrored = {m.group(1): int(m.group(2), 16) for m in
                    re.finditer(r'#(\d+) species=\S+ spawned hp=\S+ dist=\S+.* ptr=([0-9A-F]+)', guest.call('coop.npc list')['out'])}
        if mirrored:
            break
        time.sleep(1)
    hsp = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    hsp = hsp.group(1).zfill(8) if hsp else ''
    npcs = []
    for line in host.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) ([0-9A-F]+) species=(\S+) hp=', line)
        if m and m.group(3) != hsp and 'DEAD' not in line and m.group(1) in mirrored:
            npcs.append((m.group(1), int(m.group(2), 16)))
    if not ck(bool(npcs), f'a wild NPC the guest mirrors ({len(mirrored)} mirrored)'):
        return ck.result()
    nid, hptr = npcs[0]
    gptr = mirrored[nid]
    before = stunned(guest, gptr)
    print('  ' + host.call(f'coop.rules stun 3 {nid}')['out'].strip(), flush=True)
    t0, h_got, g_got = time.time(), False, False
    while time.time() - t0 < 2 and not (h_got and g_got):
        h_got = h_got or stunned(host, hptr)
        g_got = g_got or stunned(guest, gptr)
        time.sleep(0.1)
    print('  host ' + host.call('coop.rules')['out'].splitlines()[0], flush=True)
    print('  guest ' + guest.call('coop.rules')['out'].splitlines()[0], flush=True)
    ck(h_got, f"the host's NPC #{nid} is stunned in the host's game")
    ck(g_got and not before, f"the guest's proxy of #{nid} is stunned on the guest's screen (before {before}, after {g_got})")
    for c in (host, guest):
        c.call('coop.team on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
