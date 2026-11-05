#!/usr/bin/env python3
"""BUG-0555: NPCs cannot bite or charge the guest's creature while it is in the air (player report 0.7.5: "other
creatures attack me while I am flying").

The game's rule is the attack node's ability choice 0xD666B0: against a target that is not grounded (+0x137 == 0)
1 m or more away only the ranged types 0x20 / 0x3D / 0x42 / 0x48 are allowed; the hit frame and the hit do not
look again. On the host the guest's puppet stands in for the guest; the hits on it go to the guest (MSG_DAMAGE)
(docs/re/creature.md#creature-player-rules).

1. Two games on the creature fixture; the guest stands next to a wild NPC away from the host and hits it
   (`coop.npc hit`, its hit applied by the host with the guest's puppet as the attacker): the NPC fights back.
2. On the ground for 8 s: the NPC's bites reach the guest's own game (its health drops, hits from the host).
3. In the air for 20 s (up to 3 rounds until the host's NPC struck at the airborne puppet at least once) (the guest's creature put 1.5 m up every 0.25 s, so its game says airborne: PF_JUMPING):
   (an NPC that walks off from the airborne guest without a strike: the next one, BUG-0601)
   no melee hit from the host lands on the guest's own creature while its own game has it off the ground (its
   damage log: the hits applied from the host with the avatar's grounded byte +0x137 at that moment; a hit in a
   moment it touched the ground between two lifts is the game's rule and not counted).

  coop_creature_air.py   (SPORE_FIX_OFF=creature.air_immune: the NPC's bites land in the air, FAILS)"""
import re
import time

from twoinst import Checks, start


def host_hits(con, air=None):
    """melee hits from the host applied on the guest's avatar (air=1: while its own game had it off the ground)"""
    rows = re.findall(r'type=2 ret=\S+ direct air=(\d)', con.call('coop.hp dmglog')['out'])
    return len([a for a in rows if air is None or a == str(air)])


WALKED_OFF = 15.0  # m


def npc_line(host, nid):
    line = [l.strip() for l in host.call('coop.npc list')['out'].splitlines() if l.strip().startswith(f'#{nid} ')]
    return ' '.join(line[0].split()[2:4]) if line else '-'


def npc_pos(host, nid):
    m = re.search(rf'^\s*#{nid} .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.npc list')['out'], re.M)
    return tuple(map(float, m.groups())) if m else None


def npc_from(host, nid, guest, at=None):
    """the host's NPC's distance from the guest's creature (m)"""
    n = at or npc_pos(host, nid)
    g = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', guest.call('coop.avatar')['out'])
    if not n or not g:
        return None
    return round(sum((a - float(b)) ** 2 for a, b in zip(n, g.groups())) ** 0.5, 1)


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team off')
    hsp = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    hsp = hsp.group(1).zfill(8) if hsp else ''
    mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
    cands = []
    for line in host.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) \S+ species=(\S+) hp=([\d.]+) dist=([\d.]+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and m.group(2) != hsp and float(m.group(4)) > 25 and 'family' not in line and m.group(1) in mirrored:
            cands.append((m.group(1), tuple(map(float, m.groups()[4:]))))
    ck(bool(cands), f'a wild NPC away from the host the guest mirrors ({[c for c, _ in cands]})')
    if not cands:
        return ck.result()
    m = re.search(r"remote \d+ '[^']*' puppet=([0-9A-F]+)", host.call('coop.status')['out'])
    pup = m.group(1) if m else ''
    # BUG-0601: an NPC that bites the guest on the ground does not always stay in reach once it is in the air (the
    # game's ability choice allows no melee at an airborne target 1 m or more away): it stood 12-14 m off (#51, lab
    # 20261105-130326) or walked 30-120 m away from it (#52, 20261105-124913; #7, 20261105-130042) and never struck,
    # in every species of the fixture (inferred: the game's flight of a hurt animal from its attacker); which NPC the
    # test took depended on the mod's numbering, which a warm reset after nest_heal changes.
    # Now the guest is put beside the NPC every 5 s of a round when it is 3-15 m off, and an NPC farther than that
    # after a round, or with no strike in 3 rounds, gives way to the next one. The checks are unchanged: a strike at
    # the airborne guest must be seen, and no bite may land.
    ground = airborne = samples = air = low = tried = 0
    nid, tries = None, []
    for nid, (x, y, z) in cands[:8]:
        guest.call(f'coop.npc gopos {x + 2:.2f} {y:.2f} {z:.2f}')
        time.sleep(2)
        guest.call('coop.hp dmglog clear')
        guest.call(f'coop.npc hit {nid} 0.1')
        t0 = time.time()
        while time.time() - t0 < 8:
            time.sleep(1)
            guest.call('coop.social hp 10')
        ground = host_hits(guest)
        print(f'  NPC #{nid} ({npc_line(host, nid)[:60]}): {ground} bites on the ground in 8 s', flush=True)
        if not ground:
            tries.append(f'#{nid} no bite')
            continue
        for rnd in range(3):  # the NPC does not always strike at a creature over it: up to 3 rounds of 20 s
            guest.call('coop.hp dmglog clear')
            host.call('coop.hp dmglog clear')
            t0, k, moves = time.time(), 0, 0
            while time.time() - t0 < 20:
                if k % 20 == 0:  # BUG-0601: the guest back beside the NPC (it strikes only in reach), every 5 s
                    n = npc_pos(host, nid)
                    d = npc_from(host, nid, guest, n) if n else None
                    if n and d is not None and 3 < d <= WALKED_OFF:
                        guest.call(f'coop.npc gopos {n[0] + 1.5:.2f} {n[1]:.2f} {n[2]:.2f}')
                        moves += 1
                k += 1
                guest.call('coop.abilities lift 1.5')
                guest.call(f'coop.npc hit {nid} 0.1')  # it keeps fighting the guest
                time.sleep(0.25)
                samples += 1
                airborne += 'above=' in (a := guest.call('coop.abilities')['out']) and \
                    float(re.search(r'above=([-\d.]+)', a).group(1)) > 0.5
            air, low = air + host_hits(guest, 1), low + host_hits(guest, 0)
            # the NPC's melee hits on the puppet in the host's game during the air phase (the game's own hits, never
            # applied to the puppet: `filtered`; not by its grounded byte, which the game sets back at animation starts)
            tried += len([r for r in re.findall(r'victim=([0-9A-F]+) \S+ amount=\S+ type=2 ret=\S+ filtered air=\d',
                                                host.call('coop.hp dmglog')['out']) if pup and r.upper().endswith(pup.upper()[-8:])])
            off = npc_from(host, nid, guest)
            print(f'  NPC #{nid} round {rnd + 1}: {tried} melee hits on the airborne puppet in the host\'s game so far; '
                  f'it is {off if off is not None else "?"} m from the guest (the guest put beside it {moves}x)', flush=True)
            if tried or (off is not None and off > WALKED_OFF):
                break
        if tried:
            break
        tries.append(f'#{nid} bit on the ground, walked off in the air')
    ck(ground > 0, f'on the ground an NPC bites the guest (#{nid}: {ground} hits from the host in 8 s; {tries})')
    if not ground:
        return ck.result()
    print(f'  in the air ({airborne} of {samples} samples > 0.5 m): {air} bites from the host in the air, {low} while it touched the ground', flush=True)
    print('  host ' + ' | '.join(host.call('coop.rules')['out'].strip().splitlines()), flush=True)
    ck(airborne >= samples // 2, f'the guest\'s creature was in the air ({airborne} of {samples} samples)')
    ck(tried > 0, f'the NPC kept hitting at the guest\'s creature in the air in the host\'s game ({tried}; {tries})')
    ck(air == 0, f'no NPC bite reaches the guest while it is in the air ({air} hits)')
    for c in (host, guest):
        c.call('coop.team on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
