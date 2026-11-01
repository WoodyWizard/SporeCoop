#!/usr/bin/env python3
"""Creature co-op mechanics on two instances (docs/09-coop-design.md):
joint hunt bonus, the pack saves a player from a lethal hit, pack regeneration.

The joint hunt bonus is measured on the NPC's health in the host's game (BUG-0135): a lone hit by
the guest takes d, the host's hit of the same size right after takes about 1.25 d; the guest's
mirror shows the same health. The mod's "joint hits" counter is only printed.

  coop_team.py [--seed-nobonus]   --seed-nobonus: `coop.team off` on the host, the bonus check must FAIL
"""
import math
import re
import sys
import time

from coop_sync import sample
from twoinst import Checks, shot, start


def team(con):
    out = con.call('coop.team')['out']
    m = re.search(r'joint hits (\d+), rescues (\d+)', out)
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def hp(con):
    m = re.search(r'hp=([\d.]+)/([\d.]+)', con.call('coop.avatar')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else (None, None)


def pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(map(float, m.groups())) if m else None


def players_apart(host, guest):
    a, b = pos(host), pos(guest)
    return round(sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5, 1) if a and b else None


def npc_hp(con, nid):
    """host: the game's health of NPC nid"""
    m = re.search(r'#%s \S+ species=\S+ hp=([\d.]+)' % nid, con.call('coop.npc list')['out'])
    return float(m.group(1)) if m else None


def mirror_hp(con, nid):
    """guest: the game's health of the mirror of NPC nid"""
    m = re.search(r'#%s species=\S+ spawned hp=([\d.]+)' % nid, con.call('coop.npc list')['out'])
    return float(m.group(1)) if m else None


def main():
    host, guest = start()
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    # an NPC the host simulates and the guest mirrors
    nid = None
    for _ in range(20):
        mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
        for line in host.call('coop.npc list')['out'].splitlines():
            m = re.match(r'\s*#(\d+) .*hp=([\d.]+) dist=([\d.]+)', line)
            if m and m.group(1) in mirrored and float(m.group(2)) > 5 and 'DEAD' not in line:
                nid = m.group(1)
                break
        if nid:
            break
        time.sleep(1)
    ck(nid is not None, f'guest mirrors host NPCs (target #{nid})')
    # the guest's puppet has a herd of its own species: the game used to fill it with 4 clones of
    # the guest's creature (real NPCs on the host, mirrored to the guest)
    gsp = re.search(r'puppet pos=.* species=([0-9A-F]+)', host.call('coop.status')['out'])
    hsp = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    if gsp and hsp and gsp.group(1) != hsp.group(1).zfill(8):
        clones = host.call('coop.npc list')['out'].count('species=' + gsp.group(1))
        ck(clones == 0, f'no NPC clones of the guest\'s creature ({clones})')
    print(' ', host.call('coop.team')['out'].splitlines()[-1])

    # joint hunt: the guest hits alone (d), then the host hits the same NPC within 6 s (1.25 d), read
    # on the NPC's health in the host's game, not on our counter (BUG-0135)
    if '--seed-nobonus' in sys.argv:
        print('  seed:', host.call('coop.team off')['out'].strip())
    j0, _ = team(host)
    print(f'  players {players_apart(host, guest)} m apart before the hunt (guest at {pos(guest)})', flush=True)
    h0 = npc_hp(host, nid)
    d = round(max(1.0, min(h0 * 0.3, 40.0)), 2) if h0 else 1.0  # two hits leave it alive
    guest.call(f'coop.npc hit {nid} {d}')
    time.sleep(1.0)
    h1 = npc_hp(host, nid)
    print(f'  after the guest\'s hit: guest at {pos(guest)}, host at {pos(host)}', flush=True)
    host.call(f'coop.npc hosthit {nid} {d}')
    time.sleep(1.0)
    h2 = npc_hp(host, nid)
    j1, _ = team(host)
    print(f'  joint hunt: hp {h0} -> {h1} (guest alone) -> {h2} (host, joint); hit {d}; joint hits {j0} -> {j1}')
    lone = h0 - h1 if h0 is not None and h1 is not None else None
    joint = h1 - h2 if h1 is not None and h2 is not None else None
    ck(lone is not None and abs(lone - d) <= 0.1 * d + 0.02,
       f'a lone hit takes its damage from the NPC in the host\'s game ({lone} of {d})')
    ratio = joint / lone if lone and joint is not None else None  # (hp is printed with 0.01)
    ck(ratio is not None and 1.15 <= ratio <= 1.35,
       f'joint hunt bonus in the host\'s game: the second player\'s hit takes {ratio and round(ratio, 2)}x a lone hit (1.25)')
    gh = None
    for _ in range(10):  # the guest's mirror shows the same health
        gh = mirror_hp(guest, nid)
        if gh is not None and h2 is not None and abs(gh - h2) <= 0.1:
            break
        time.sleep(0.5)
    ck(gh is not None and h2 is not None and abs(gh - h2) <= 0.1,
       f'the guest\'s mirror shows the health after the joint hit ({gh} / host {h2})')
    if '--seed-nobonus' in sys.argv:
        host.call('coop.team on')

    # pack regeneration while the partner is close (an NPC hit would throw the guest away: set hp)
    print(f'  players {players_apart(host, guest)} m apart before the regen check (guest at {pos(guest)}, '
          f'host at {pos(host)}, npc {nid}: {[l.strip()[:120] for l in host.call("coop.npc list")["out"].splitlines() if l.strip().startswith("#" + str(nid) + " ")]})', flush=True)
    # BUG-0330: the guest's avatar was found ~14 m from the host here in 2 of ~40 runs (the regen then
    # rightly did nothing: "Alice near"); the check is about the regen, so the guest stands next to
    # the host first and the guest's own status must say "close (regen)"
    hpos = pos(host)
    if hpos:
        guest.call('coop.npc gopos %.2f %.2f %.2f' % (hpos[0] + 1.5, hpos[1], hpos[2]))
    close = False
    for _ in range(10):
        time.sleep(0.5)
        close = 'close (regen)' in guest.call('coop.team')['out']
        if close:
            break
    ck(close, f'the guest stands next to the host for the regen check ({players_apart(host, guest)} m)')
    # BUG-0400: the move to host x + 1.5 m is a chord of the planet (0.5 m nearer its centre here): the
    # guest's creature stood 0.94 m under the terrain for good (the game does not lift a grounded
    # creature placed under it); gopos now puts it onto the ground
    m = re.search(r'above=([-\d.]+)', guest.call('coop.avatar')['out'])
    above = float(m.group(1)) if m else None
    ck(above is not None and above > -0.3, f"the guest's creature stands on the ground after the move ({above} m)")
    hr0, mx0 = hp(guest)
    guest.call(f'coop.team hp {(mx0 or 10) * 0.6:.1f}')
    time.sleep(0.5)
    hr1, _ = hp(guest)
    print('  pack:', guest.call('coop.team')['out'].splitlines()[-1])
    time.sleep(4)
    hr2, _ = hp(guest)
    ck(hr2 is not None and hr1 is not None and hr2 > hr1, f'pack regen next to the partner: hp {hr1} -> {hr2}')

    # the pack protects: the host forwards a lethal NPC hit to the guest standing next to it
    h0, mx = hp(guest)
    _, r0 = team(guest)
    def damage_in():
        m = re.search(r'damage_in=(\d+)', guest.call('coop.npc')['out'])
        return int(m.group(1)) if m else 0
    d0 = damage_in()
    ids = re.findall(r'#(\d+) .*hp=', host.call('coop.npc list')['out'])
    for cand in [nid] + ids:
        out = host.call(f'coop.npc hurt 1 {cand} 100000')['out']
        time.sleep(1.5)
        if 'puppet hit' in out and damage_in() > d0:
            break
    h1, _ = hp(guest)
    _, r1 = team(guest)
    ck(h1 is not None and h1 > 0, f'guest survived a lethal hit: hp {h0} -> {h1} of {mx}')
    ck(r1 == (r0 or 0) + 1, f'rescue counted ({r0} -> {r1})')
    chat = host.call('coop.status')['out']
    ck('saved Bob' in chat, 'everybody sees "Alice saved Bob from death"')

    # a guest's kill pays the pack's DNA (the game pays only kills by the host's own avatar:
    # until 2026-10-23 a guest could hunt all day for nothing; docs/re/creature.md 2)
    fam = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    fam = fam.group(1).zfill(8) if fam else ''
    mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
    prey = [m.group(1) for m in re.finditer(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=([0-9A-F]+) hp=([\d.]+)',
                                             host.call('coop.npc list')['out'])
            if m.group(1) in mirrored and m.group(2) != fam and float(m.group(3)) > 0]
    dna = lambda c: float(re.search(r'dna=([\d.]+)', c.call('coop.dna')['out']).group(1))  # noqa: E731
    d0 = dna(host)
    killed = before = None
    for cand in prey[:3]:
        before = sample(host).get('n' + cand)
        guest.call(f'coop.npc hit {cand} 100000')
        time.sleep(1.5)
        if dna(host) > d0:
            killed = 'n' + cand
            break
    d1 = dna(host)
    time.sleep(2)
    ck(d1 > d0, f'a kill by the guest pays DNA: host {d0} -> {d1} (prey {prey[:3]})')
    ck(abs(dna(guest) - d1) < 0.5, f'the guest has the same DNA ({dna(guest)})')
    # the corpse stays where the animal fell, the same on both (a forwarded hit used to push with
    # the victim's position as the impulse: the corpse flew ~80 m up, on the guest it stayed there)
    hs, gs = sample(host).get(killed), sample(guest).get(killed)
    if killed and before and hs and gs:
        moved, apart = math.dist(before[0], hs[0]), math.dist(hs[0], gs[0])
        ck(moved < 5 and apart < 3, f'the corpse lies where it fell ({moved:.1f} m) and in the same place '
                                    f'on both ({apart:.1f} m apart)')
    print('  host :', host.call('coop.team')['out'].splitlines()[-1])
    print('  guest:', guest.call('coop.team')['out'].splitlines()[-1])
    shot(host, 'team_host.png')
    shot(guest, 'team_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
