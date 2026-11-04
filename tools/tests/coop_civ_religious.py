#!/usr/bin/env python3
"""Civilization: a religious attack looks the same on both screens (BUG-0530).

After BUG-0477's shot relay the divergence detector saw religious (cultural) projectiles and AoE areas in the
guest's game that the host's did not have (`noun_only_guest:cultural_projectile`, `default_ao_e_area`): the host
relayed every FireTool call of a shooter (cCombatSimulator::Update tries each update) and the guest's weapon fired
on its own refire time for as long as the host's kept trying.

1. Two games on civ_npc.
2. The host gets three religious land vehicles at its city (`coop.civ spawnveh <city> 1 0`) and orders them and
   its military ones to the nearest NPC city (religious attack, order type 3; military, type 2).
3. For 70 s, twice a second, the religious shots in each game's noun list (cultural projectiles and AoE areas)
   and all projectiles.
PASS: the host's religious vehicles fired (religious shots in at least 4 samples); the guest's game showed them
(religious shots in at least a third of the host's samples with any); the guest's game made no religious shot of
its own: at most 25 % more religious shots over the samples than the host's (+4) and at most 15 % of the samples
with religious shots on the guest only.
and the guest's count series follows the host's by less than 0.25 s (the best shift of the two series, sampled
every ~0.15 s).
SPORE_FIX_OFF=civ.shot_now: the shots wait for the next 0.2 s list, the guest follows by 0.3-0.4 s (FAIL; on: 0.10-0.20 s).
SPORE_FIX_OFF=civ.shot_fired: not seen to fail (the guest's weapon refused the extra calls on its own refire time:
126 / 126 shots, lab 20261104-095320); the check guards the count.

  SPORE_LANE=9 python3 tools/tests/coop_civ_religious.py
"""
import math
import time

from play_civ import civ_state
from twoinst import Checks, start

RELIGIOUS = {'4F76F0D': 'cultural_projectile', '4167186': 'default_ao_e_area'}
OTHER = {'24270C5': 'default_tool_projectile', '5776A2C': 'resource_projectile'}


def count(con, nouns):
    return sum(sum(1 for ln in con.call(f'coop.civ nouns {n}')['out'].splitlines() if ln.startswith('obj '))
               for n in nouns)


def best_lag(hser, gser):
    """the shift (s) of the guest's count series that matches the host's best (mean absolute difference)"""
    if len(hser) < 10:
        return 0.0

    def at(ser, t):
        v = ser[0][1]
        for ts, c in ser:
            if ts > t:
                break
            v = c
        return v
    best, best_err = 0.0, None
    for k in range(0, 21):
        lag = k * 0.05
        err = sum(abs(at(gser, t + lag) - h) for t, h in hser if t + lag <= gser[-1][0]) / len(hser)
        if best_err is None or err < best_err - 1e-9:
            best, best_err = lag, err
    return best


def shots_line(con):
    return next((ln for ln in con.call('coop.civ')['out'].splitlines() if ln.startswith('shots:')), '')


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
    for _ in range(3):
        print('  host spawnveh:', host.call(f'coop.civ spawnveh {home} 1 0')['out'].strip(), flush=True)
    time.sleep(3)
    cities, hv, _ = civ_state(host)
    rel = [i for i, v in hv.items() if v['owner'] >= 0 and v['purpose'] == 1]
    army = [i for i, v in hv.items() if v['owner'] >= 0 and v['purpose'] == 0]
    ck(rel, f'the host has religious vehicles ({rel})')
    if not rel:
        return ck.result()
    tgt = min(npc, key=lambda c: math.dist(cities[c]['pos'], cities[home]['pos']))
    for i in rel:
        print(f'  host: religious #{i} -> city {tgt}:', host.call(f'coop.civ attack {i} {tgt} 3')['out'].strip(), flush=True)
    for i in army:
        host.call(f'coop.civ attack {i} {tgt} 2')
    time.sleep(2)
    _, gv, _ = civ_state(guest)
    ck(all(i in gv for i in rel), f'the guest mirrors the religious vehicles ({sorted(gv)})')
    hs = gs = both = gonly = hn = gn = 0
    seq, hser, gser = [], [], []
    t0 = time.time()
    while time.time() - t0 < 70:
        th = time.time()
        h = count(host, RELIGIOUS)
        tg = time.time()
        g = count(guest, RELIGIOUS)
        hser.append((th, h))
        gser.append((tg, g))
        hs += h > 0
        gs += g > 0
        both += h > 0 and g > 0
        gonly += g > 0 and h == 0
        hn += h
        gn += g
        seq.append(f'{h}/{g}')
        time.sleep(0.1)
    any_ = hs + gonly
    lag = best_lag(hser, gser)
    print(f'  the guest\'s religious shots follow the host\'s by {lag:.2f} s (best match of the two count series)',
          flush=True)
    print(f'  religious samples: host {hs}, guest {gs}, both {both}, guest only {gonly}; shots host {hn} guest {gn}',
          flush=True)
    print('  host/guest per sample:', ' '.join(seq), flush=True)
    print(f'  all projectiles now: host {count(host, OTHER)} guest {count(guest, OTHER)}', flush=True)
    print('  host', shots_line(host), flush=True)
    print('  guest', shots_line(guest), flush=True)
    ck(hs >= 4, f'the host\'s religious vehicles fired ({hs} samples with religious shots)')
    ck(hs and both >= hs / 3, f'the guest\'s game shows the host\'s religious shots ({both} of the host\'s {hs} samples)')
    ck(gn <= 1.25 * hn + 4, f'the guest\'s game makes no religious shots of its own ({gn} / {hn} over the samples)')
    ck(gonly <= 0.15 * max(any_, 1),
       f'religious shots on the guest only in at most 15 % of the samples with any ({gonly} of {any_})')
    ck(lag < 0.249, f'the guest\'s game shows the host\'s religious shots within 0.25 s ({lag:.2f} s; BUG-0530 civ.shot_now)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
