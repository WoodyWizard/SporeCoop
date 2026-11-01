#!/usr/bin/env python3
"""Creature stage, two players: each one dies and respawns at the nest, then must grow up and walk.

A player report (2026-10-25, the guest "Dexie"): "After my death I'm in constant state of growing
up from egg and going back into small version and again grow back. Can't move at all." A respawn
puts the creature back into the nest as a hatchling that waits for a family member of its nest to
greet it and make it grow up (docs/re/creature.md 13). A guest has no family of its own (its local
NPCs are removed, the host's mirrored ones have no AI): before the fix the guest stayed a
hatchling for good (half the health, the egg shell on its head, W did not move it).

Rounds: the host dies (`coop.team die`, the game's damage path), the guest dies the same way, then
each one takes lethal hits through the mod's damage filter next to the partner (`coop.team hit`:
the first one is the pack rescue, the second one kills). After each death the
caption's OK is pressed; the creature must respawn, grow up (age 1, full health) and walk under W.

  SPORE_LANE=3 python3 tools/tests/coop_creature_respawn.py [--no-creations]
"""
import math
import re
import sys
import time

import uifind
from twoinst import Checks, shot, start


def av(con):
    out = con.call('coop.avatar')['out']
    m = re.search(r'hp=([-\d.]+)/([-\d.]+)', out)
    p = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    a = re.search(r'age=(-?\d+)', out)
    return {'hp': float(m.group(1)) if m else -1, 'max': float(m.group(2)) if m else -1,
            'pos': tuple(float(x) for x in p.groups()) if p else None,
            'age': int(a.group(1)) if a else None, 'dead': 'flags=0x11' in out}


def close_pause(con):
    for _ in range(3):
        if not uifind.shown(con, 'Resume Game'):
            return
        con.call('key esc')
        time.sleep(1)


def walk(con, secs=2.0):
    """W held: how far the creature went (m)"""
    a = av(con)['pos']
    con.call('key w down')
    time.sleep(secs)
    con.call('key w up')
    time.sleep(0.3)
    b = av(con)['pos']
    return math.dist(a, b) if a and b else 0.0


def hatchling(a, full):
    return a['hp'] > 0 and (a['age'] == 0 if a['age'] is not None else a['max'] < full * 0.9)


def respawn(ck, con, name, full):
    """the creature `con` just took a lethal hit: OK on the caption, then it must come back grown up.
    (Dead is the death caption: after a kill next to the nest the creature's health may climb again
    while the caption waits, the nest heals it and the flag mbDead stays off.)"""
    caption = None
    for _ in range(30):  # the death plays, the caption comes ~6 s later
        if uifind.shown(con, 'You were killed'):
            caption = True
            break
        time.sleep(0.5)
    ck(caption, f'{name}: killed (the death caption is up)')
    back = None
    for _ in range(30):  # OK until the caption is gone and a hatchling stands in the nest
        if uifind.shown(con, 'You were killed'):
            con.call('key enter')
        time.sleep(1)
        close_pause(con)
        if not uifind.shown(con, 'You were killed') and hatchling(av(con), full):
            back = time.time()
            break
    ck(back is not None, f'{name}: respawned at the nest as a hatchling')
    if back is None:
        return
    small = av(con)
    grown = None
    while time.time() - back < 40:  # the host's own family greets the hatchling in ~13 s
        a = av(con)
        if a['hp'] > 0 and a['max'] >= full * 0.99 and a['age'] in (None, 1):
            grown = time.time() - back
            break
        time.sleep(1)
    a = av(con)
    print(f'  ({name}: hatchling hp {small["hp"]}/{small["max"]} age {small["age"]}, after '
          f'{time.time() - back:.0f} s hp {a["hp"]}/{a["max"]} age {a["age"]})', flush=True)
    ck(grown is not None, f'{name}: the hatchling grew up ({grown and round(grown)} s, health {a["max"]} of {full})')
    time.sleep(1)
    d = walk(con)
    ck(d > 1.0, f'{name}: walks under W after the respawn ({d:.1f} m in 2 s)')


def main():
    creations = '--no-creations' not in sys.argv
    host, guest = start('creature', guest_creations=creations)
    ck = Checks()
    print(f'  (guest {"with" if creations else "without"} own creations)', flush=True)
    full = {}
    for con, name in ((host, 'host'), (guest, 'guest')):
        d = 0.0
        for _ in range(3):  # (right after the join a box or the camera may still hold the first keys)
            d = walk(con)
            if d > 1.0:
                break
            con.call('key enter')
            time.sleep(2)
        ck(d > 1.0, f'{name}: walks under W before any death ({d:.1f} m in 2 s)')
        full[name] = av(con)['max']
    guest.call('coop.team come')
    time.sleep(1)

    for con, name in ((host, 'host'), (guest, 'guest')):
        ck('lethal' in con.call('coop.team die')['out'], f'{name}: a lethal hit (coop.team die)')
        respawn(ck, con, name, full[name])
        guest.call('coop.team come')
        time.sleep(2)
    shot(guest, 'respawn_guest.png')
    shot(host, 'respawn_host.png')

    # a lethal hit through the damage filter next to the partner: the first one is the pack rescue
    # (40 % health, once a minute), the second one kills. (Hits by a host NPC through `coop.npc hurt`
    # did no damage in the game here: TakeDamage with that attacker took off nothing, only the
    # rescue counted; docs/06-testing.md)
    for con, name in ((guest, 'guest'), (host, 'host')):
        guest.call('coop.team come')
        time.sleep(2)
        r0 = re.search(r'rescues (\d+)', con.call('coop.team')['out'])
        con.call('coop.team hit 100000')
        time.sleep(1)
        saved = av(con)['hp']
        r1 = re.search(r'rescues (\d+)', con.call('coop.team')['out'])
        ck(saved > 0 and r0 and r1 and int(r1.group(1)) == int(r0.group(1)) + 1,
           f'{name}: the pack rescued it from the first lethal hit (hp {saved})')
        con.call('coop.team hit 100000')
        respawn(ck, con, f'{name} (after a rescue)', full[name])
    print(' ', guest.call('coop.npc')['out'].splitlines()[0][:200])
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
