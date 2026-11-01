#!/usr/bin/env python3
"""The host's pack never fights a partner (BUG-0374).

Player report (0.7.3.3, host mark): "my own group can attack other players for some reason and kill
them". A guest's hit on one of the host's pack members is applied in the host's game with the guest's
puppet as the attacker; the game's TakeDamage tells the victim's AI (stimulus 0x400, the attacker
as sender) to fight back, and the pack member's hits on the puppet were forwarded to the guest as
real damage, without end.

1. Two games on the creature fixture; the host recruits a wild NPC into its pack (`coop.npc recruit`,
   what a won social game does) and stands next to it, the guest next to the host.
2. The guest hits the pack member once (`coop.npc hit`, the path of a guest's bite).
3. For 15 s, in the host's game: the pack member's health (the guest's hit is no friendly fire) and
   its AI's stimuli naming the guest's puppet; in the guest's game: its own avatar's health, sampled
   every 0.2 s, never takes a hit.

  coop_creature_party.py   (SPORE_FIX_OFF=npc.party_peace must FAIL)"""
import re
import time

from twoinst import Checks, start


def own_hp(con):
    m = re.search(r'hp=([\d.]+)/([\d.]+)', con.call('coop.avatar')['out'])
    return float(m.group(1)) if m else None


def pack(con):
    return [(m.group(1), float(m.group(2)), int(m.group(3)))
            for m in re.finditer(r'^pack (\S+) hp=([\d.]+) from_partner=(\d+)', con.call('coop.hp pack')['out'], re.M)]


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
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
    print(' ', host.call(f'coop.npc gonpc {nid} 3')['out'].strip(), flush=True)
    time.sleep(1)
    p = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.avatar')['out'])
    guest.call('coop.npc gopos %s %s %s' % p.groups())
    time.sleep(4)
    pk = pack(host)
    print('  pack:', pk, flush=True)
    ck(len(pk) == 1, 'the host\'s pack has the recruit')
    if len(pk) != 1:
        return ck.result()
    hp_pack0 = pk[0][1]
    g0 = own_hp(guest)
    print(' ', guest.call(f'coop.npc hit {nid} 0.5')['out'].strip(), flush=True)
    hits, last, low = 0, g0, g0
    from_partner = 0
    hp_pack = hp_pack0
    t0 = time.time()
    while time.time() - t0 < 15:
        g = own_hp(guest)
        if g is not None and last is not None and g < last - 0.02:
            hits += 1
        if g is not None:
            low = min(low, g)
            last = g
        pk = pack(host)
        if pk:
            from_partner = max(from_partner, pk[0][2])
            hp_pack = min(hp_pack, pk[0][1])
        time.sleep(0.2)
    print(f'  guest health {g0} -> low {low}, {hits} drops; pack member {hp_pack0} -> {hp_pack}, '
          f'stimuli from the guest\'s puppet {from_partner}', flush=True)
    print('  guest npc:', re.search(r'damage_in=\d+', guest.call('coop.npc')['out']).group(0), flush=True)
    ck(hits == 0, f'the guest\'s own game took no hit from the host\'s pack ({hits} health drops, low {low} of {g0})')
    ck(from_partner == 0, f'the pack member\'s AI does not target the guest\'s puppet ({from_partner} stimuli from it)')
    ck(abs(hp_pack - hp_pack0) < 0.01, f'the guest\'s hit does not hurt the host\'s pack member ({hp_pack0} -> {hp_pack})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
