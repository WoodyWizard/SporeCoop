#!/usr/bin/env python3
"""Civilization: a guest copies a host city turret's shots without a crash (BUG-0688, GitHub #198).

A guest crashed about 10 s into a Civ fight with tanks: guest_fire took a turret's combatant at a building's offset
(+0x120), where a turret holds no object, and weapon_of called vt 0x14 of that word (execute of 0xCCCCCCCC).

1. Two games on civ_npc. The host's game adds two turrets to the NPC city nearest to its army
   (`coop.civ addturret`: the game's City_AddTurret); civ.turrets (BUG-0484) puts them in the guest's game too.
2. The turret objects of both games: where each holds its combatant (a dword of the turret combatant vtable,
   probed with `mem.read`).
3. The host's military vehicles attack that city (`coop.civ attack`), so its turrets fire at them; for 60 s the
   shot counters of both games.
PASS: the host relayed turret shots (`turret_shots` seen > 0), the guest fired copies of them from its own turret
(fired > 0, no bad combatant), and neither game faulted or died.
SPORE_FIX_OFF=civ.turret_weapon: the guest calls a virtual of a non-object at the first turret shot (crash / fault: FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_civ_turret_shot.py
"""
import math
import re
import struct
import time

from play_civ import civ_state
from twoinst import Checks, start

TURRET_COMBATANT_VT = 0x01467758  # the turret TakeDamage's vtable (docs/re/civ.md, `this` = turret + 0x588)


def turrets(con, ci):
    return [(int(m.group(1), 16), tuple(float(x) for x in m.groups()[1:])) for m in
            re.finditer(r'turret (?:0x)?([0-9A-Fa-f]+) pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)',
                        con.call(f'coop.civ turrets {ci}')['out'])]


def dwords(con, addr, n):
    out = con.call(f'mem.read 0x{addr:08X} {4 * n}')['out']
    b = bytes(int(x, 16) for ln in out.splitlines() if ln[:8].strip() for x in ln.split()[1:])
    return struct.unpack(f'<{len(b) // 4}I', b[:len(b) // 4 * 4])


def probe(con, who, t):
    d = dwords(con, t, 0x800 // 4)
    at = [4 * i for i, v in enumerate(d) if v == TURRET_COMBATANT_VT]
    w120 = d[0x120 // 4] if len(d) > 0x120 // 4 else None
    w588 = d[0x588 // 4] if len(d) > 0x588 // 4 else None
    print(f'  {who} turret {t:08X}: vtable {d[0]:08X}, +0x120 {w120:08X}, +0x588 {w588:08X}, '
          f'turret combatant vtable at {[hex(x) for x in at]}', flush=True)
    return at


def line(con, key):
    return next((ln for ln in con.call('coop.civ')['out'].splitlines() if ln.startswith(key + ':')), '')


def stat(ln, key):
    m = re.search(rf'\b{key}=(\d+)', ln)
    return int(m.group(1)) if m else 0


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    cities, hv, _ = civ_state(host)
    army = [i for i, v in hv.items() if v['owner'] >= 0 and v['purpose'] == 0]
    npc = [c for c, d in cities.items() if not d['player'] and d['owner'] < 0]
    ck(army and npc, f'the host has a military vehicle ({army}) and there is an NPC city ({npc})')
    if not (army and npc):
        return ck.result()
    p0 = hv[army[0]]['pos']
    tgt = min(npc, key=lambda c: math.dist(cities[c]['pos'], p0))
    for _ in range(2):
        print('  host addturret:', host.call(f'coop.civ addturret {tgt}')['out'].strip(), flush=True)
    ht = []
    for _ in range(10):  # civ.turrets sends a city's turrets when they changed and every 3 s
        time.sleep(2)
        ht, gt = turrets(host, tgt), turrets(guest, tgt)
        if ht and len(gt) >= len(ht):
            break
    print(f'  city {tgt}: turrets host {len(ht)} guest {len(gt)}', flush=True)
    ck(ht and gt, f'city {tgt} has turrets in both games')
    offs = set()
    for who, con, ts in (('host', host, ht), ('guest', guest, gt)):
        for t, _ in ts[:2]:
            offs.update(probe(con, who, t))
    ck(offs == {0x588}, f'a turret holds its combatant at +0x588 ({sorted(hex(x) for x in offs)})')
    for i in army:
        print(f'  host: #{i} attacks city {tgt}:', host.call(f'coop.civ attack {i} {tgt} 2')['out'].strip(), flush=True)
    t0, dead = time.time(), None
    while time.time() - t0 < 60:
        try:
            hl, gl = line(host, 'turret_shots'), line(guest, 'turret_shots')
        except Exception as e:  # a crashed game does not answer
            dead = repr(e)
            break
        if int(time.time() - t0) % 10 == 0:
            print(f'  t={time.time() - t0:.0f}s host {hl}', flush=True)
            print(f'  t={time.time() - t0:.0f}s guest {gl}', flush=True)
        time.sleep(1)
    ck(dead is None, f'both games answer through the fight ({dead})')
    if dead:
        return ck.result()
    hl, gl = line(host, 'turret_shots'), line(guest, 'turret_shots')
    print('  host', hl, flush=True)
    print('  guest', gl, flush=True)
    print('  guest', line(guest, 'shots'), flush=True)
    ck(stat(hl, 'seen') > 0, f'the host\'s turrets fired at the vehicles ({stat(hl, "seen")} shots)')
    ck(stat(gl, 'fired') > 0, f'the guest\'s game fired copies of the turret shots ({stat(gl, "fired")})')
    ck(stat(gl, 'bad_combatant') == 0, f'no shooter with a wrong combatant on the guest ({stat(gl, "bad_combatant")})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
