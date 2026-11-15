#!/usr/bin/env python3
"""BUG-0893 probe: the damage factor CreatureAbilityHit gives an attacker that is not of the player species.

CreatureAbilityHit 0xC20390 (dc04): when the attacker's IsPlayerSpecies (vt 0xD0) is false and it is a cCreatureAnimal
(noun 0x18EB45E), the ability's damage is multiplied by the float at +0x158 of the attacker's herd (creature +0x1674,
mHerd; 0xC046F0 returns it). A guest playing its own creature is not the world's player species in its own game, so its
own hits get its herd's factor; on the world's species they do not.

1. Two games on the creature fixture.
2. Read, in the guest's game: its avatar's herd and the herd's +0x158 (on the world's species), the same for a few NPCs.
3. The guest plays one of its own creatures (`coop.creations choose`): the same reads.
Printed: the factors. PASS when the guest's own avatar's factor is 1.0 (its own hits are not scaled); FAIL when another
value scales them (a bug to fix: the rule should treat the player as the player).

  SPORE_LANE=7 python3 tools/tests/coop_own_damage_scale.py
"""
import re
import struct
import time

from coop_migration import eax, mem
from own_creature import back_to_world, play_own
from twoinst import Checks, start


def u32(con, a):
    b = mem(con, a, 4)
    return struct.unpack('<I', b)[0] if b and len(b) >= 4 else None


def f32(con, a):
    b = mem(con, a, 4)
    return struct.unpack('<f', b)[0] if b and len(b) >= 4 else None


def avatar(con):
    nm = eax(con, 'Simulator::cGameNounManager::Get cdecl')
    return u32(con, nm + 0x54) if nm else None


def factor(con, c):
    h = u32(con, c + 0x1674) if c else None
    return (h, f32(con, h + 0x158) if h else None)


def npcs(con, n=4):
    out = []
    for m in re.finditer(r'ptr=([0-9A-Fa-f]{8})', con.call('coop.npc list')['out']):
        out.append(int(m.group(1), 16))
        if len(out) >= n:
            break
    return out


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    av = avatar(guest)
    h0, f0 = factor(guest, av)
    print(f'  guest on the world species: avatar {av and hex(av)} herd {h0 and hex(h0)} +0x158 = {f0}', flush=True)
    for c in npcs(guest):
        h, f = factor(guest, c)
        print(f'  guest proxy {c:#x}: herd {h and hex(h)} +0x158 = {f}', flush=True)
    for c in npcs(host):
        h, f = factor(host, c)
        print(f'  host NPC {c:#x}: herd {h and hex(h)} +0x158 = {f}', flush=True)
    own = play_own(guest)
    ck(own, 'the guest plays one of its own creatures')
    av = avatar(guest)
    h1, f1 = factor(guest, av)
    print(f'  guest on its own creature: avatar {av and hex(av)} herd {h1 and hex(h1)} +0x158 = {f1}', flush=True)
    hp_line = [ln for ln in guest.call('coop.hp')['out'].splitlines() if ln.startswith('own:')]
    print('  guest:', hp_line, flush=True)
    ck(f1 is not None, "the guest's own avatar has a herd with the factor")
    if f1 is not None:
        ck(abs(f1 - 1.0) < 1e-4, f"the guest's own creature's hits are not scaled by its herd (factor {f1})")
    back_to_world(guest)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
