#!/usr/bin/env python3
"""A transition, then the whole planet compared: the guest's game holds the host's world (W2 class of 0.7.7).

BUG-0564 / BUG-0566 / BUG-0567 / BUG-0569 (classes BUG-0421, BUG-0427): after a transition (the host's game
over and reload of its last save, a guest's leave and rejoin) the guest's world kept villages the host's no
longer had, broken buildings, empty plazas. The detector compared 250 m around the guest only; this test
compares the planet's whole lists, read from each game's own noun list (`coop.realign planet`: every village,
hut, tool, food mat, nest, city with its position, the player flag, a building's health).

1. A fixture (`--fixture tribe|creature`), both games in it; the planets match (baseline, printed).
2. Tribe: what the reports' sessions had before the host's game over: a village the host's game formed after
   its last save (`coop.tribe form` on the host: the controller's own forming; the guest makes it), a hut the
   host's members damaged (an NPC hut at 40 % on the host: within 10 s the guest's hut has the host's health,
   BUG-0566), and huts damaged in the guest's game only (every other NPC hut at 30 %: the old world's damage,
   BUG-0567). Without the fix (realign.buildings) the guest keeps its damage.
3. The transition (`--case`): `gameover` (Tribe: the host's player tribe knocked out, OK on the caption: the
   host's game reloads its last save while the guest's stays in its world) or `rejoin` (the guest leaves,
   joins again and loads the host's world).
4. PASS: within 40 s the guest's planet has every host village / hut / tool / food mat / nest / city (tolerances
   as realign_rule.h), none the host has not, and each hut's health within 5 % of the host's; checked twice 6 s
   apart.

SPORE_FIX_OFF=realign.villages,realign.buildings: step 2's village and hut stay (FAIL) on `gameover`;
SPORE_FIX_OFF=wobj.tools_planet: the far villages' tools differ (FAIL) on the tribe `rejoin` (BUG-0488).

  SPORE_LANE=6 python3 tools/tests/coop_realign_planet.py --fixture tribe --case gameover
"""
import argparse
import math
import re
import time

import lane
from twoinst import Checks, start

TOL = {'village': 40, 'hut': 10, 'tool': 3, 'food_mat': 10, 'nest': 10, 'city': 30}
COUNTED = ('village', 'hut', 'tool', 'food_mat', 'nest', 'city')


def planet(con):
    """[(kind, (x, y, z), state, flags)] of the game's whole planet"""
    out = []
    try:
        text = con.call('coop.realign planet')['out']
    except (OSError, ValueError):
        return None
    for ln in text.splitlines():
        m = re.match(r'pl (\w+) \(([-\d.]+) ([-\d.]+) ([-\d.]+)\) st=(\d+) fl=(\d+)', ln)
        if m:
            out.append((m.group(1), tuple(float(m.group(i)) for i in (2, 3, 4)), int(m.group(5)), int(m.group(6))))
    return out


def compare(h, g):
    """{kind: (lack, extra, health_off)} with the greedy nearest match of realign_rule.h"""
    res = {}
    for k, tol in TOL.items():
        hs = [o for o in h if o[0] == k]
        gs = [o for o in g if o[0] == k]
        pairs = sorted((math.dist(a[1], b[1]), i, j) for i, a in enumerate(hs) for j, b in enumerate(gs)
                       if a[3] == b[3] and math.dist(a[1], b[1]) <= tol)
        hu, gu, off = set(), set(), []
        for d, i, j in pairs:
            if i in hu or j in gu:
                continue
            hu.add(i)
            gu.add(j)
            if k in ('hut', 'tool') and abs(hs[i][2] - gs[j][2]) > 50:
                off.append((tuple(round(v) for v in gs[j][1]), hs[i][2], gs[j][2]))
        lack = [tuple(round(v) for v in hs[i][1]) for i in range(len(hs)) if i not in hu]
        extra = [tuple(round(v) for v in gs[j][1]) for j in range(len(gs)) if j not in gu]
        if hs or gs:
            res[k] = (lack, extra, off)
    return res


def show(res):
    return '; '.join(f'{k}: lack {len(v[0])} extra {len(v[1])} health {len(v[2])}' for k, v in res.items()) or 'empty'


def bad(res):
    return {k: v for k, v in res.items() if k in COUNTED and (v[0] or v[1] or v[2])}


def gameover(host, ck):
    from coop_tribe_gameover import frames_go_on, player_members
    own, enemy = player_members(host)
    if not ck(own and enemy, 'the player tribe and another tribe have members'):
        return False
    t0 = time.time()
    while time.time() - t0 < 60:
        own, enemy = player_members(host)
        alive = [i for i, hp in own.items() if hp > 0]
        if not alive or not enemy:
            break
        for i in alive:
            host.call(f'coop.tribe pincer {i} {enemy[0]} 300')
        time.sleep(1)
    t2, alas = time.time(), False
    while time.time() - t2 < 75 and not alas:  # ~20 s in a quiet lab, more under load
        time.sleep(3)
        alas = 'no window' not in host.call('coop.ui find "Alas"')['out']
    if not ck(alas, 'the game-over caption is up on the host'):
        return False
    bounce_ok(host)  # OK: the game reloads the last save
    return ck(frames_go_on(host, 15), 'the host bounces back and keeps drawing')


def rejoin(guest, stage, ck):
    guest.call('coop.leave')
    time.sleep(4)
    print('  rejoin:', guest.call(lane.JOIN)['out'].strip()[:60], flush=True)
    t0, left = time.time(), False
    while time.time() - t0 < 60 and not left:
        m = guest.call('game.mode')['out']
        left = 'Load' in m or 'GalaxyGameEntry' in m or 'receiving world' in guest.call('coop.status')['out']
        time.sleep(0.3)
    t1, back = time.time(), False
    while time.time() - t1 < 150 and not back:
        try:
            m = guest.call('game.mode')['out']
            back = stage in m and 'Load' not in m
        except OSError:
            pass
        time.sleep(0.5)
    return ck(left and back, f'the guest left its old copy ({left}) and plays the host\'s {stage} again ({back})')


def bounce_ok(host):
    """the caption's OK (Enter), again when the game has not left Tribe for its reload 10 s later (a key the
    caption took before its button was live)"""
    for _ in range(3):
        host.call('key enter')
        t0 = time.time()
        while time.time() - t0 < 10:
            if 'no window' in host.call('coop.ui find "Alas"')['out']:
                return True
            time.sleep(1)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='tribe', choices=('tribe', 'creature'))
    ap.add_argument('--case', default='gameover', choices=('gameover', 'rejoin'))
    a = ap.parse_args()
    stage = a.fixture.capitalize()
    host, guest = start(a.fixture)
    ck = Checks()
    time.sleep(10)
    h0, g0 = planet(host), planet(guest)
    print(f'  baseline: host {len(h0)} objects, guest {len(g0)}: {show(compare(h0, g0))}', flush=True)
    if a.fixture == 'tribe':
        print('  host: ' + host.call('coop.tribe form')['out'].strip(), flush=True)
        huts = [o for o in planet(host) if o[0] == 'hut' and not o[3]]
        if ck(len(huts) >= 2, f'the host has two NPC huts ({len(huts)})'):
            x, y, z = huts[0][1]
            print('  host (a damaged hut): ' + host.call(f'coop.realign hp hut {x} {y} {z} 400')['out'].strip(), flush=True)
            t0, seen = time.time(), None
            while time.time() - t0 < 10 and seen is None:
                time.sleep(2)
                gh = [o for o in planet(guest) if o[0] == 'hut' and math.dist(o[1], huts[0][1]) < 10]
                seen = gh[0][2] if gh and abs(gh[0][2] - 400) <= 50 else None
            ck(seen is not None, f'the guest\'s game shows the host\'s damaged hut at the host\'s health (400/1000: '
               f'{[o[2] for o in planet(guest) if o[0] == "hut" and math.dist(o[1], huts[0][1]) < 10]})')
            for o in huts[1:]:  # the old world's damage: every other NPC hut at 30 % in the guest's game only
                x2, y2, z2 = o[1]
                print('  guest (its own damaged hut): ' + guest.call(f'coop.realign hp hut {x2} {y2} {z2} 300')['out'].strip(),
                      flush=True)
        time.sleep(6)
    if a.case == 'gameover':
        if a.fixture != 'tribe':
            ck(False, 'the game-over case needs the tribe fixture')
            return ck.result()
        if not gameover(host, ck):
            return ck.result()
    elif not rejoin(guest, stage, ck):
        return ck.result()
    t1, ok_in_row, res = time.time(), 0, {}
    while time.time() - t1 < 40 + 6:
        h, g = planet(host), planet(guest)
        if h is not None and g is not None:
            res = compare(h, g)
            ok_in_row = ok_in_row + 1 if not bad(res) else 0
            if ok_in_row >= 2:
                break
        time.sleep(6)
    print(f'  after the {a.case}: host {len(h or [])} objects, guest {len(g or [])}: {show(res)}', flush=True)
    for k, (lack, extra, off) in res.items():
        if lack or extra or off:
            print(f'    {k}: lack {lack[:6]} extra {extra[:6]} health (pos, host, guest) {off[:6]}', flush=True)
    for k in COUNTED:
        if k in res:
            lack, extra, off = res[k]
            ck(not extra, f'the guest\'s game has no {k} the host\'s world has not ({extra[:6]})')
            ck(not lack, f'the guest\'s game has every {k} of the host\'s ({lack[:6]} missing)')
            if k == 'hut':
                ck(not off, f'every hut has the host\'s health in the guest\'s game ({off[:6]})')
    print('  guest: ' + ' | '.join(ln for ln in guest.call('coop.realign')['out'].splitlines()
                                   if ln.startswith('realign') or 'removed=0 state_fixed=0' not in ln), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
