#!/usr/bin/env python3
"""BUG-0512: the host's pack never takes a partner as its target.

Player reports (0.7.3.6): "my pack mate attacked the guest when I accidentally targeted him"; the host's
own hits on the guest do nothing, the pack chased and bit the guest. npc.party_peace (BUG-0374) drops the
pack's damage on a puppet; the pack still held the partner's puppet as its target (+0xE7C) and fought it on
both screens. Not reproduced by the host's own targeting in the lab (a click on the puppet sets the avatar's
+0xE7C to the puppet; the pack did not follow within 15 s, see docs/re/creature.md#creature-pack-target), so
the pack's state is also made as the game makes it in a fight: the creature's own SetTarget (vt 0x84) with
the puppet's combatant, as the attack node's enter does.

1. Two games on the creature fixture; the host recruits a wild NPC (`coop.npc recruit`), the guest stands
   next to the host.
2. The host clicks the guest's creature (a real click at its screen position): the avatar's target is the
   puppet; for 8 s, in the host's game, no pack member's target (+0xE7C) is the puppet, and the guest's own
   game takes no hit (its health sampled, it never drops).
3. ROUNDS times every pack member gets the puppet as its target (`coop.hp packtarget 1 combatant`); 0.5 s
   later and for 4 s the host's game must hold no pack member aimed at the puppet.

  coop_creature_pack_target.py   (SPORE_FIX_OFF=npc.pack_spares_partners: the pack keeps the target, FAILS)"""
import re
import struct
import time

from coop_creature_kills import read
from coop_creature_party import own_hp, pack
from twoinst import Checks, start

ROUNDS = 3


def u32(con, a):
    b = read(con, a, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else None


def on_puppet(host, pup):
    """pack members of the host whose target is the puppet (a combatant inside it)"""
    return [m for m, _, _ in pack(host) if pup <= (u32(host, int(m, 16) + 0xE7C) or 0) < pup + 0x1700]


def main():
    host, guest = start('creature')
    ck = Checks()
    hsp = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    hsp = hsp.group(1).zfill(8) if hsp else ''
    nid = None
    for _ in range(20):  # a living wild NPC (not the host's family) that the guest mirrors
        mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
        for line in host.call('coop.npc list')['out'].splitlines():
            m = re.match(r'\s*#(\d+) \S+ species=(\S+) hp=([\d.]+) dist=([\d.]+)', line)
            if m and m.group(1) in mirrored and m.group(2) != hsp and float(m.group(3)) > 3 and 'DEAD' not in line:
                nid = m.group(1)
                break
        if nid:
            break
        time.sleep(1)
    ck(nid is not None, f'a wild NPC the guest mirrors (#{nid})')
    if not nid:
        return ck.result()
    print(' ', host.call(f'coop.npc recruit {nid}')['out'].strip(), flush=True)
    host.call(f'coop.npc gonpc {nid} 3')
    time.sleep(1)
    p = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.avatar')['out'])
    guest.call('coop.npc gopos %s %s %s' % p.groups())
    time.sleep(4)
    ck(len(pack(host)) == 1, f'the host\'s pack has the recruit ({pack(host)})')
    st = host.call('coop.status')['out']
    m = re.search(r"remote 1 '[^']*' puppet=([0-9A-F]+)", st)
    a = re.search(r'local=([0-9A-F]+)', st)
    pup, av = (int(m.group(1), 16) if m else 0), (int(a.group(1), 16) if a else 0)
    ck(pup and av, f'the guest\'s puppet {pup:08X} and the host\'s avatar {av:08X}')
    if not (pup and av and pack(host)):
        return ck.result()

    # 2. the host targets the guest (a click on its creature)
    host.call('coop.track start')
    time.sleep(0.5)
    rows = [r.split() for r in host.call('coop.track dump 0 100000')['out'].splitlines()]
    rows = [r for r in rows if len(r) > 22 and r[1] == '1' and r[20] == '1']
    host.call('coop.track stop')
    targeted = False
    if rows:
        host.call(f'click {int(float(rows[-1][21]))} {int(float(rows[-1][22]))}')
        time.sleep(0.5)
        targeted = pup <= (u32(host, av + 0xE7C) or 0) < pup + 0x1700
    print(f'  the host\'s avatar targets the guest\'s creature: {targeted}', flush=True)
    g0 = last = own_hp(guest)
    drops = aimed = 0
    t0 = time.time()
    while time.time() - t0 < 8:
        g = own_hp(guest)
        if g is not None and last is not None and g < last - 0.02:
            drops += 1
        last = g if g is not None else last
        aimed = max(aimed, len(on_puppet(host, pup)))
        time.sleep(0.25)
    ck(aimed == 0, f'after the host targeted the guest no pack member aims at the guest ({aimed})')
    ck(drops == 0, f'the guest\'s own game took no hit meanwhile ({drops} health drops from {g0})')

    # 3. the pack in a fight with the guest's creature (its target set as the game sets it)
    for i in range(ROUNDS):
        print('  ' + host.call('coop.hp packtarget 1 combatant')['out'].strip(), flush=True)
        time.sleep(0.5)
        held = 0
        t0 = time.time()
        while time.time() - t0 < 4:
            held += bool(on_puppet(host, pup))
            time.sleep(0.25)
        ck(held == 0, f'round {i + 1}: no pack member of the host keeps the guest as its target ({held} of 16 samples)')
    print('  ' + re.search(r'npc\(host\) pack: [^\n]*', host.call('coop.npc')['out']).group(0), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
