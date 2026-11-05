#!/usr/bin/env python3
"""BUG-0554: an NPC's charge stuns the guest's own creature (player report 0.7.5: "I don't get stunned by other
creatures' charge"); the guest's charge stuns the host's NPC.

The stun of a charge is not a hit: the ability hit 0xC20390 applies the damage through TakeDamage (forwarded to the
guest as MSG_DAMAGE) and then the ability's effect kind 1, the victim's CreatureStun (vt 0x94): the stimulus 0x1000
with the stun's seconds in the victim's behaviour tree (the stars, the stagger). In the host's game that victim is
the guest's puppet, which has no AI (docs/re/creature.md#creature-player-rules).

1. Two games on the creature fixture; the guest stands 4 m from a wild NPC that has a charge (ability type 0x1F,
   effect kind 1) in the host's game.
2. The host's NPC aims at the guest's puppet and plays its charge (`coop.rules charge <id> 1`: SetTarget + the
   game's PlayAbility; the hit and the stun are the game's own). Up to 4 tries until the host's damage log shows
   the charge's hit on the puppet (ret 0xC20A78).
3. The guest's own game: its avatar's behaviour tree holds the stun stimulus 0x1000 (read from its 8 stimulus
   slots) within 2 s of that hit.
4. The other way: the guest's game stuns a proxy (`coop.rules stun 2 <id>`, the game's CreatureStun through the
   hook, as its own charge's effect calls it): the host's NPC holds the stimulus 0x1000 within 2 s.

  coop_creature_stun.py   (SPORE_FIX_OFF=creature.stun_relay: the stun stays on the puppet / proxy, FAILS)"""
import re
import struct
import time

from coop_creature_kills import read
from twoinst import Checks, start


def stimuli(con, c):
    """the creature's behaviour-tree stimuli: [(mask lo, mask hi, value)]"""
    ai = struct.unpack('<I', read(con, c + 0xB4C, 4))[0]
    b = read(con, ai + 8, 8 * 0x38) if ai else b''
    out = []
    for i in range(len(b) // 0x38):
        lo, hi, v = struct.unpack_from('<IIf', b, i * 0x38)
        if lo or hi:
            out.append((lo, hi, v))
    return out


def stunned(con, c):
    return any(lo & 0x1000 for lo, _, _ in stimuli(con, c))


def avatar(con):
    m = re.search(r'local=([0-9A-F]+)', con.call('coop.avatar')['out'])
    return int(m.group(1), 16) if m else 0


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team off')  # no pack rescue / regeneration in the way
    hsp = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    hsp = hsp.group(1).zfill(8) if hsp else ''
    mirrored = set()
    for _ in range(10):
        mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
        if mirrored:
            break
        time.sleep(1)
    npcs = []
    for line in host.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) ([0-9A-F]+) species=(\S+) hp=([\d.]+) dist=[\d.]+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and m.group(3) != hsp and 'DEAD' not in line and 'family' not in line and m.group(1) in mirrored:
            if 'type=0x1F' in host.call(f'coop.social abil {m.group(1)}')['out']:
                npcs.append((m.group(1), int(m.group(2), 16), tuple(map(float, m.groups()[4:]))))
    ck(bool(npcs), f'a wild NPC with a charge the guest mirrors ({[n for n, _, _ in npcs]})')
    if not npcs:
        return ck.result()
    nid, nptr, (x, y, z) = npcs[0]
    gav = avatar(guest)
    hit = got = False
    for i in range(4):
        guest.call(f'coop.npc gopos {x + 4:.2f} {y:.2f} {z:.2f}')
        time.sleep(2)
        host.call('coop.hp dmglog clear')
        print('  ' + host.call(f'coop.rules charge {nid} 1')['out'].strip(), flush=True)
        t0 = time.time()
        while time.time() - t0 < 4 and not got:
            got = stunned(guest, gav)
            hit = hit or '0x00C20A78' in host.call('coop.hp dmglog')['out']
            time.sleep(0.2)
        print(f'  try {i + 1}: charge hit the puppet {hit}, guest stunned {got}', flush=True)
        if hit:
            break
        m = re.search(rf'#{nid} \S+ species=\S+ hp=\S+ dist=\S+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)',
                      host.call('coop.npc list')['out'])
        if m:
            x, y, z = map(float, m.groups())
    print('  host ' + host.call('coop.rules')['out'].splitlines()[0], flush=True)
    print('  guest ' + guest.call('coop.rules')['out'].splitlines()[0], flush=True)
    ck(hit, 'the host\'s NPC charged the guest\'s creature (its hit on the puppet in the host\'s game)')
    ck(got, 'the guest\'s own creature is stunned (stimulus 0x1000 in its behaviour tree)')

    # 4. the guest's stun of a proxy reaches the host's NPC
    time.sleep(4)  # the earlier charge's stimuli decay
    before = stunned(host, nptr)
    print('  ' + guest.call(f'coop.rules stun 2 {nid}')['out'].strip(), flush=True)
    npc_got = False
    t0 = time.time()
    while time.time() - t0 < 2 and not npc_got:
        npc_got = stunned(host, nptr)
        time.sleep(0.2)
    ck(npc_got and not before, f'the guest\'s stun of its proxy stuns the host\'s NPC #{nid} (before {before}, after {npc_got})')
    for c in (host, guest):
        c.call('coop.team on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
