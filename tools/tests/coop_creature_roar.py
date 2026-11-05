#!/usr/bin/env python3
"""BUG-0501: the guest's roar scares the host's animals (player report: "ROAR ability is broken for guests and works
only for the host").

The roar (ability type 0x31) is the behaviour tree's action node 0x06005DAF (tick SporeCoop::BT_SpecialAbilityTick
0xD8E840): on its first tick the roarer's game gives every creature in the ability's range that is not the avatar,
not a baby, of another species and not epic the roar's goal slot (template 1 of group 1 at creature +0xB48, for the
ability's +0xA0 seconds: the creature runs away) and the stimulus (0, 0x4000000) with the value FLT_MAX
(docs/re/creature.md#creature-roar-relay). In the guest's game those creatures are proxies without AI.

1. Two games on the creature fixture; the guest's avatar has the roar (type 0x31).
2. The guest stands 6 m from a group of the host's wild NPCs (another species than the guest's, mirrored by the
   guest) and roars (`coop.rules roar 31`: the node's first tick, as the ability button starts it).
3. The host's game: every one of its NPCs of another species within 20 m of the guest's puppet (the roar's range is
   30 m) holds the roar's goal slot within 2 s (the game's own state: the slot's template and group), and none did
   before the roar.
4. The host's game: the behaviour tree of each of them is in node 0x02D852E0 (its answer to the roar) within 3 s,
   and they back away: their mean distance from the puppet grows by 1.5 m or more in 10 s.

  coop_creature_roar.py   (SPORE_FIX_OFF=creature.roar_relay: no host NPC gets the slot or runs, FAILS)"""
import math
import re
import struct
import time

from coop_creature_kills import read
from twoinst import Checks, start

NEAR = 20.0     # m from the puppet: inside the roar's 30 m on the host
ANSWER = '02D852E0'  # the node the host's NPCs run after a roar (every one of them, lab 20261105-115422; none without)
FLEE = 1.5      # m the mean distance grows by (none moved without the fix: 0.0 m, lab 20261105-110044; 2.6 m in warm games
                # late in the day, 8 m in cold ones)
FLT_MAX = 3.4028234663852886e38


def roar_slot(con, c):
    """the creature's goal slots (+0xB48): is the roar's (template 1, group 1) running"""
    g = struct.unpack('<I', read(con, c + 0xB48, 4))[0]
    if not g:
        return False
    b = read(con, g, 0x230)
    if len(b) < 0x230:
        return False
    mask = struct.unpack_from('<I', b, 0x22C)[0]
    for i in range(8):
        tid, grp = struct.unpack_from('<iI', b, i * 0x44 + 0x24)
        if mask & (1 << i) and tid == 1 and grp == 1:
            return True
    return False


def roar_stimulus(con, c):
    ai = struct.unpack('<I', read(con, c + 0xB4C, 4))[0]
    b = read(con, ai + 8, 8 * 0x38) if ai else b''
    for i in range(len(b) // 0x38):
        lo, hi, v = struct.unpack_from('<IIf', b, i * 0x38)
        if hi & 0x4000000 and v > 1e37:
            return True
    return False


def node(con, c):
    ai = struct.unpack('<I', read(con, c + 0xB4C, 4))[0]
    return '%08X' % struct.unpack('<I', read(con, ai + 0x1D8, 4))[0] if ai else '-'


def npcs(con):
    out = []
    for line in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) ([0-9A-F]+) species=(\S+) hp=([\d.]+) dist=[\d.]+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and 'DEAD' not in line:
            out.append(dict(id=m.group(1), ptr=int(m.group(2), 16), species=m.group(3), family='family' in line,
                            pos=tuple(map(float, m.groups()[4:]))))
    return out


def puppet_pos(host):
    m = re.search(r'puppet pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.status')['out'])
    return tuple(map(float, m.groups())) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team off')
    gab = guest.call('coop.social abil -1')['out']
    ck('type=0x31 ' in gab, 'the guest\'s avatar has the roar (ability type 0x31)')
    gsp = re.search(r'key=0x([0-9A-F]+)', guest.call('coop.avatar')['out'])
    gsp = gsp.group(1).zfill(8) if gsp else ''
    mirrored = set()
    for _ in range(10):
        mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
        if mirrored:
            break
        time.sleep(1)
    wild = [n for n in npcs(host) if n['species'] != gsp and not n['family'] and n['id'] in mirrored]
    ck(bool(wild), f'wild NPCs of another species the guest mirrors ({len(wild)})')
    if not wild or 'type=0x31 ' not in gab:
        return ck.result()
    # the NPC with the most others of the wild ones around it
    best = max(wild, key=lambda n: sum(math.dist(n['pos'], o['pos']) < 12 for o in wild))
    x, y, z = best['pos']
    guest.call(f'coop.npc gopos {x + 6:.2f} {y:.2f} {z:.2f}')
    time.sleep(3)
    pp = puppet_pos(host)
    near = [n for n in npcs(host) if n['species'] != gsp and not n['family'] and pp and math.dist(n['pos'], pp) < NEAR]
    ck(bool(near), f'host NPCs of another species within {NEAR:.0f} m of the guest\'s puppet ({[n["id"] for n in near]})')
    if not near:
        return ck.result()
    before = {n['id']: roar_slot(host, n['ptr']) for n in near}
    n0 = {n['id']: node(host, n['ptr']) for n in near}
    print('  host NPC nodes before: ' + ', '.join(f"#{i} {v}" for i, v in n0.items()), flush=True)
    d0 = {n['id']: math.dist(n['pos'], pp) for n in near}
    print('  ' + guest.call('coop.rules roar 31')['out'].strip(), flush=True)
    got = {}
    t0 = time.time()
    while time.time() - t0 < 2.5 and len(got) < len(near):
        for n in near:
            if n['id'] not in got and roar_slot(host, n['ptr']):
                got[n['id']] = roar_stimulus(host, n['ptr'])
        time.sleep(0.2)
    time.sleep(2)
    n1 = {n['id']: node(host, n['ptr']) for n in near}
    print('  host NPC nodes 3 s after: ' + ', '.join(f"#{i} {v}" for i, v in n1.items()), flush=True)
    time.sleep(8)
    pp2 = puppet_pos(host) or pp
    now = {n['id']: n for n in npcs(host)}
    d1 = {i: math.dist(now[i]['pos'], pp2) for i in d0 if i in now}
    grow = [d1[i] - d0[i] for i in d1]
    mean = sum(grow) / len(grow) if grow else 0.0
    answered = sum(n1[i] == ANSWER for i in n1)
    for c in (host, guest):
        print('  ' + c.call('coop.rules')['out'].splitlines()[-1], flush=True)
    print(f'  near {sorted(d0)}: slot before {before}, roar slot (stimulus) after {got}', flush=True)
    print('  distance from the puppet: ' + ', '.join(f'#{i} {d0[i]:.1f} -> {d1[i]:.1f}' for i in d1), flush=True)
    ck(not any(before.values()), 'no host NPC held the roar\'s goal slot before the roar')
    ck(len(got) == len(near), f'every host NPC within {NEAR:.0f} m holds the roar\'s goal slot ({len(got)} of {len(near)})')
    ck(not any(v == ANSWER for v in n0.values()), f'none of them was in node {ANSWER} before the roar')
    ck(answered == len(near), f'the behaviour tree of every one of them answers within 3 s (node {ANSWER}: {answered} of {len(near)})')
    ck(mean >= FLEE, f'the host\'s NPCs back away from the guest\'s puppet (mean distance +{mean:.1f} m in 10 s, >= {FLEE:.1f})')
    for c in (host, guest):
        c.call('coop.team on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
