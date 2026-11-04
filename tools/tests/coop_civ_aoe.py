#!/usr/bin/env python3
"""Civilization: a religious AoE weapon's area ends on the guest when it ends on the host (BUG-0530).

An AoE weapon (cDefaultAoETool) makes its area (noun 0x4167186, default_ao_e_area) when it starts firing and keeps
it until the tool is stopped (cToolManager::OnMouseUp: cCombatSimulator::Update stops a combatant's tool when its
target is gone); the area sends cultural projectiles at what is inside. A guest fires copies of the host's shots
but never stopped its copy of the weapon, so the area and its cultural projectiles stayed in the guest's game
(the detector's `noun_only_guest:default_ao_e_area` / `cultural_projectile`).

1. Two games on civ_npc. The host frees vehicle slots (`coop.civ remove`) and gets religious vehicles of every
   locomotion at its city (`coop.civ spawnveh <city> 1 <loco>`), and orders them to a religious attack on the
   nearest NPC city.
2. 50 s of attack, then the host orders them home (`coop.civ move`): the host's game stops their weapons.
3. Every ~0.15 s, the AoE areas and cultural projectiles of both games (by object), the whole time and 20 s after.
PASS: the host's game made an AoE area; after the host's last area ended, the guest's ended within 2.5 s; at most
15 % of the samples have an area in the guest's game only.
SPORE_FIX_OFF=civ.shot_release: the guest's areas stay after the host's ended (FAIL).

  SPORE_LANE=6 python3 tools/tests/coop_civ_aoe.py
"""
import math
import re
import time

from play_civ import civ_state
from twoinst import Checks, start

AOE, CULT = '4167186', '4F76F0D'


def objs(con, noun):
    return set(re.findall(r'^obj (\S+)', con.call(f'coop.civ nouns {noun}')['out'], re.M))


def shots_line(con):
    return next((ln for ln in con.call('coop.civ')['out'].splitlines() if ln.startswith('shots:')), '')


class Life:
    """first / last time each object was seen"""

    def __init__(self):
        self.first, self.last = {}, {}

    def see(self, t, ids):
        for o in ids:
            self.first.setdefault(o, t)
            self.last[o] = t

    def lives(self):
        return [self.last[o] - self.first[o] for o in self.first]

    def end(self):
        return max(self.last.values()) if self.last else None


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    cities, hv, _ = civ_state(host)
    mine = [c for c, d in cities.items() if d['player']]
    npc = [c for c, d in cities.items() if not d['player'] and d['owner'] < 0]
    ck(mine and npc, f'the host has a city ({mine}) and there is an NPC city ({npc})')
    if not (mine and npc):
        return ck.result()
    home = mine[0]
    for i in [i for i, v in hv.items() if v['owner'] >= 0 and v['purpose'] != 1][:4]:
        print(f'  host remove #{i}:', host.call(f'coop.civ remove {i}')['out'].strip(), flush=True)
    time.sleep(1)
    for loco in (0, 1, 2, 0):
        print(f'  host spawnveh loco {loco}:', host.call(f'coop.civ spawnveh {home} 1 {loco}')['out'].strip(), flush=True)
    time.sleep(3)
    cities, hv, _ = civ_state(host)
    rel = [i for i, v in hv.items() if v['owner'] >= 0 and v['purpose'] == 1]
    print('  religious:', {i: hv[i]['loco'] for i in rel}, flush=True)
    ck(rel, f'the host has religious vehicles ({rel})')
    if not rel:
        return ck.result()
    tgt = min(npc, key=lambda c: math.dist(cities[c]['pos'], cities[home]['pos']))
    for i in rel:
        host.call(f'coop.civ attack {i} {tgt} 3')
    ha, ga, hc, gc = Life(), Life(), Life(), Life()
    samples = gonly = hany = 0
    t0 = time.time()
    sent_home = None
    while time.time() - t0 < 50 + 25:
        now = time.time() - t0
        if sent_home is None and now > 50:
            hp = cities[home]['pos']
            for i in rel:
                host.call(f'coop.civ move {i} {hp[0]:.1f} {hp[1]:.1f} {hp[2]:.1f}')
            sent_home = now
            print(f'  t={now:.0f}s: the religious vehicles go home', flush=True)
        h, g = objs(host, AOE), objs(guest, AOE)
        t = time.time() - t0
        ha.see(t, h)
        ga.see(t, g)
        hc.see(t, objs(host, CULT))
        gc.see(t, objs(guest, CULT))
        samples += 1
        hany += bool(h)
        gonly += bool(g) and not h
        time.sleep(0.15)
    hl, gl = ha.lives(), ga.lives()
    print(f'  AoE areas: host {len(hl)} (lives {sorted(round(x, 1) for x in hl)}), guest {len(gl)} '
          f'(lives {sorted(round(x, 1) for x in gl)})', flush=True)
    print(f'  cultural projectiles: host {len(hc.first)} (mean life {sum(hc.lives()) / max(len(hc.first), 1):.2f} s), '
          f'guest {len(gc.first)} (mean life {sum(gc.lives()) / max(len(gc.first), 1):.2f} s)', flush=True)
    he, ge = ha.end(), ga.end()
    print(f'  last area seen: host {he}, guest {ge}; samples {samples}, host with an area {hany}, guest only {gonly}',
          flush=True)
    print('  host', shots_line(host), flush=True)
    print('  guest', shots_line(guest), flush=True)
    ck(hl, 'the host\'s religious attack made an AoE area')
    ck(he is None or ge is None or ge <= he + 2.5,
       f'the guest\'s last area ended within 2.5 s of the host\'s (host {he}, guest {ge})')
    ck(gonly <= 0.15 * max(samples, 1), f'an area in the guest\'s game only in at most 15 % of the samples ({gonly} of {samples})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
