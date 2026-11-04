#!/usr/bin/env python3
"""Civilization: the guest sees the host's fights: the same shots fly in its game (BUG-0477).

The divergence detector found the host's projectiles (default_tool_projectile, resource / cultural
projectiles, AoE areas) in no guest's game: a guest's mirrors run no behaviour tree, so they never fired,
and the guest saw a fight only as hit points that fell.

1. Two games on civ_npc.
2. The host orders its land military vehicles to attack the nearest NPC city (`coop.civ attack`: the game's
   own SetOrder, type 2) and, if that does not start a fight, its vehicles to attack an NPC vehicle.
3. For 60 s, once a second, the projectile objects in each game's noun list (`coop.civ nouns <noun>`).
PASS: the host fired (projectiles in at least 4 samples) and the guest's game had projectiles in at least
half of the samples in which the host's had; no building of the target city is ruined on the guest that the
host's game has standing (the guest's copies of the shots hurt nothing); the target city's building hit points
on the guest are the host's (BUG-0529: within 3 % or 60 when the host's fell, at 30 s and at the end) and not
below them (the guest's copies took nothing extra).
SPORE_FIX_OFF=civ.shots: the guest's game has no projectile at all (FAIL); SPORE_FIX_OFF=civ.building_hp: the
guest's city stands at full hit points while the host's fell (FAIL); SPORE_FIX_OFF=civ.shot_damage,civ.building_hp:
the guest's copies of the shots hurt the city there (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_civ_shots.py
"""
import math
import re
import time

from play_civ import civ_state
from twoinst import Checks, start

PROJ = {'24270C5': 'default_tool_projectile', '5776A2C': 'resource_projectile', '4F76F0D': 'cultural_projectile',
        '4167186': 'default_ao_e_area'}


def projectiles(con):
    n = 0
    for noun in PROJ:
        n += sum(1 for ln in con.call(f'coop.civ nouns {noun}')['out'].splitlines() if ln.startswith('obj '))
    return n


def ruins(con, ci):
    out = con.call(f'coop.civ buildings {ci}')['out']
    return [tuple(int(float(x)) for x in m.groups()) for m in
            re.finditer(r'state=2 hp=\S+ pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', out)]


def hp_sum(con, ci):
    return round(sum(float(x) for x in re.findall(r' hp=([-\d.]+)', con.call(f'coop.civ buildings {ci}')['out'])))


def shots_line(con):
    return next((ln for ln in con.call('coop.civ')['out'].splitlines() if ln.startswith('shots:')), '')


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    cities, hv, _ = civ_state(host)
    army = [i for i, v in hv.items() if v['owner'] >= 0 and v['purpose'] == 0]
    npc = [c for c, d in cities.items() if not d['player'] and d['owner'] < 0]
    print(f'  host vehicles {sorted(hv)}, army {army}, NPC cities {npc}', flush=True)
    ck(army and npc, 'the host has a military vehicle and there is an NPC city')
    if not (army and npc):
        return ck.result()
    p0 = hv[army[0]]['pos']
    tgt = min(npc, key=lambda c: math.dist(cities[c]['pos'], p0))
    for i in army:
        print(f'  host: #{i} attacks city {tgt}:', host.call(f'coop.civ attack {i} {tgt} 2')['out'].strip(), flush=True)
    g_hp0 = hp_sum(guest, tgt)
    hs, gs, both, t0 = 0, 0, 0, time.time()
    hn, gn, seq = 0, 0, []
    hp_mid = None
    while time.time() - t0 < 60:
        h, g = projectiles(host), projectiles(guest)
        hs += h > 0
        gs += g > 0
        both += h > 0 and g > 0
        hn += h
        gn += g
        seq.append(f'{h}/{g}')
        if int(time.time() - t0) % 10 == 0:
            print(f'  t={time.time() - t0:.0f}s projectiles host {h} guest {g}', flush=True)
        if hp_mid is None and time.time() - t0 > 30:
            hp_mid = (hp_sum(host, tgt), hp_sum(guest, tgt))
            print(f'  t=30s city hp host {hp_mid[0]} guest {hp_mid[1]}', flush=True)
        if time.time() - t0 > 30 and hs == 0:  # no fight at the city: the NPC vehicles
            _, hv2, _ = civ_state(host)
            foes = [i for i, v in hv2.items() if v['owner'] < 0]
            if foes:
                f = min(foes, key=lambda i: math.dist(hv2[i]['pos'], p0))
                for i in army:
                    host.call(f'coop.civ attack {i} v{f} 1')
                print(f'  host: the army attacks NPC vehicle {f}', flush=True)
        time.sleep(1)
    print(f'  samples with projectiles: host {hs}, guest {gs}, both {both}; projectiles host {hn} guest {gn}', flush=True)
    print('  host/guest per sample:', ' '.join(seq), flush=True)
    time.sleep(2)  # the hit points of the last shots: one city per snapshot cycle
    g_hp1, h_hp1 = hp_sum(guest, tgt), hp_sum(host, tgt)
    print('  city hp: host', h_hp1, 'guest', g_hp0, '->', g_hp1, flush=True)
    print('  host', shots_line(host), flush=True)
    print('  guest', shots_line(guest), flush=True)
    ck(hs >= 4, f'the host\'s vehicles fired ({hs} samples with projectiles)')
    ck(hs and both >= hs / 2, f'the guest\'s game shows the host\'s shots (projectiles in {both} of the host\'s {hs} samples)')
    hr, gr = ruins(host, tgt), ruins(guest, tgt)
    extra = [r for r in gr if not any(math.dist(r, x) < 3 for x in hr)]
    print(f'  ruins of city {tgt}: host {hr} guest {gr}', flush=True)
    ck(gn <= 2 * hn + 4, f'the guest\'s game does not fire much more than the host\'s ({gn} / {hn} projectiles)')
    ck(g_hp1 >= h_hp1 - max(60, 0.03 * h_hp1),
       f'the guest\'s copies of the shots take no hit points of the city there (guest {g_hp0} -> {g_hp1}, host {h_hp1})')
    hit = h_hp1 < g_hp0 - 100
    print(f'  the host\'s city lost hit points: {hit} ({g_hp0} -> {h_hp1})', flush=True)
    for when, (h, g) in (('at 30 s', hp_mid or (h_hp1, g_hp1)), ('at the end', (h_hp1, g_hp1))):
        ck(not hit or abs(g - h) <= max(60, 0.03 * h),
           f'the guest\'s city has the host\'s building hit points {when} (host {h}, guest {g}; BUG-0529)')
    ck(not extra, f'no building is ruined on the guest that stands on the host ({extra})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
