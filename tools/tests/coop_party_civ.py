#!/usr/bin/env python3
"""Civilization with four players (BUG-0152, #96): ownership, purchases, a gift and superweapon
requests between guests, and a leaver's vehicle, checked on every machine.

Alice hosts, Bob, Carol and Dave are guests (tools/tests/party.py), on the `civ` fixture (one city).

1. capital  fewer cities than players: the capital is shared (owner 253) on every machine.
2. buy      every guest buys a vehicle in it (`coop.civ buy 0 0`, the city menu's button): each owns
            one, and every machine shows the same owner for every vehicle.
3. gift     Bob gives his vehicle to Carol (`coop.give`): the host, Carol and Dave show Carol as the
            owner, and Carol's move order drives it in the host's game (its position).
4. weapons  two guests ask for superweapons during one vote (#96): Bob type 3 (2000), then Carol
            type 1 (16000); the vote passes: the game's money (+0x98, host and guests) went down by
            2000, not 16000, and Carol was told her request was not taken.
            SPORE_FIX_OFF=civ.sw_vote must FAIL here.
5. leave    Dave leaves: his vehicle goes to a present player on every machine (BUG-0031).
            SPORE_FIX_OFF=civ.orphans must FAIL here.

  SPORE_LANE=7 python3 tools/tests/coop_party_civ.py
"""
import time

from coop_civ import civ, dist
from coop_civ_sw_vote import COST, TOL, money, unlock
from party import Party
from twoinst import Checks


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def vowners(con):
    return {i: v['owner'] for i, v in civ(con)[1].items()}


def main():
    ck = Checks()
    p = Party.start('civ', 4)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        ids = p.ids()
        bob, carol, dave = p.guests
        time.sleep(3)

        # 1. the shared capital
        hc, _ = civ(p.host.con)
        pcs = [k for k, v in hc.items() if v['player']]
        ck(pcs and hc[pcs[0]]['owner'] == 253, f'[capital] one city, four players: the capital is shared ({hc})')
        for g in p.guests:
            gc, _ = civ(g.con)
            ck({k: v['owner'] for k, v in gc.items()} == {k: v['owner'] for k, v in hc.items()},
               f'[capital] {g.name} shows the same city owners')
        if not pcs:
            return ck.result()
        cap = pcs[0]

        # 2. every guest buys a vehicle
        before = set(vowners(p.host.con))
        for g in p.guests:
            print(f'  [buy] {g.name}:', g.call(f'coop.civ buy {cap} 0'), flush=True)
            time.sleep(2)
        wait(lambda: len(set(vowners(p.host.con)) - before) >= 3, 15)
        h = vowners(p.host.con)
        new = {i: o for i, o in h.items() if i not in before}
        mine = {g.name: [i for i, o in new.items() if o == ids[g.name]] for g in p.guests}
        print(f'  [buy] new vehicles {new}', flush=True)
        ck(all(mine.values()), f'[buy] every guest owns a vehicle it bought ({mine})')
        wait(lambda: all(vowners(g.con) == vowners(p.host.con) for g in p.guests), 10)
        h = vowners(p.host.con)
        for g in p.guests:
            go = vowners(g.con)
            diff = {i: (h.get(i), go.get(i)) for i in set(h) | set(go) if h.get(i) != go.get(i)}
            ck(not diff, f'[buy] {g.name} shows the host\'s vehicle owners (differ: {diff})')

        # 3. Bob's vehicle to Carol, Carol drives it
        if mine.get('Bob'):
            vid = mine['Bob'][0]
            for i in civ(bob.con)[1]:
                bob.call(f'coop.civ select {i} 0')
            bob.call(f'coop.civ select {vid} 1')
            print('  [gift] Bob:', bob.call(f'coop.give {ids["Carol"]}'), flush=True)
            ok = wait(lambda: all(vowners(x.con).get(vid) == ids['Carol'] for x in (p.host, carol, dave)), 10)
            ck(ok, f'[gift] the host, Carol and Dave show Carol as the owner of #{vid}')
            hv = civ(p.host.con)[1]
            p0 = hv[vid]['pos']
            cpos = hc[cap]['pos']
            d = dist(p0, cpos) or 1
            tgt = tuple(c + (q - c) / d * (d + 40) for q, c in zip(p0, cpos))
            carol.call(f'coop.civ select {vid} 1')
            print('  [gift] Carol:', carol.call('coop.civ move %d %.1f %.1f %.1f' % ((vid,) + tgt)), flush=True)
            time.sleep(8)
            hv = civ(p.host.con)[1]
            moved = dist(hv[vid]['pos'], p0) if vid in hv else 0
            ck(moved > 5, f'[gift] Carol\'s order drives it in the host\'s game ({moved:.1f} m)')

        # 4. superweapon requests of two guests during one vote
        print('  [weapons] unlocked on the host:', unlock(p.host.con, (3, 1)), flush=True)
        h0 = money(p.host.con)
        p.host.call(f'coop.res add money {20000 - h0:.1f}')
        wait(lambda: all(abs((money(x.con) or 0) - 20000) < 50 for x in p.all), 10, 0.5)
        m0 = {x.name: money(x.con) for x in p.all}
        print(f'  [weapons] money {m0}', flush=True)
        print('  [weapons] Bob:', bob.call('coop.civ super 3'), flush=True)
        time.sleep(2)
        ck(p.host.call('coop.vote').startswith('vote active'), '[weapons] Bob\'s request opened a vote')
        print('  [weapons] Carol:', carol.call('coop.civ super 1'), flush=True)
        time.sleep(2)
        p.host.call('coop.vote go')
        wait(lambda: m0['Alice'] - (money(p.host.con) or m0['Alice']) > 1000, 8, 0.5)
        time.sleep(2)
        m1 = {x.name: money(x.con) for x in p.all}
        spent = m0['Alice'] - m1['Alice']
        print(f'  [weapons] money after the vote {m1} (spent {spent:.0f})', flush=True)
        ck(COST[3] - TOL <= spent < COST[1] - TOL,
           f'[weapons] the game fired the vote\'s weapon (Bob\'s type 3, 2000), not Carol\'s type 1 (16000): spent {spent:.0f}')
        ck(all(abs(v - m1['Alice']) < 50 for v in m1.values()), f'[weapons] every game shows the same money ({m1})')
        ck('another vote is running' in carol.call('coop.status'), '[weapons] Carol was told her request was not taken')

        # 5. Dave leaves: his vehicle goes to somebody present
        dv = mine.get('Dave', [])
        ck(p.leave(dave), '[leave] Dave leaves')
        p.guests = [bob, carol]
        time.sleep(4)
        present = {0, ids['Bob'], ids['Carol']}
        for x in p.all:
            o = vowners(x.con)
            ck(all(o.get(v) in present for v in dv), f'[leave] {x.name}: Dave\'s vehicle {dv} belongs to a present player '
                                                     f'({[o.get(v) for v in dv]})')
        p.guests = [bob, carol, dave]
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
