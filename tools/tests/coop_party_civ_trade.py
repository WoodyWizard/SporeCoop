#!/usr/bin/env python3
"""Civilization with three players and three cities (BUG-0152): city owners, roles of two guests and
the trade between the players' cities, checked on every machine.

Alice hosts, Bob and Carol are guests (tools/tests/party.py), on `civ_npc`. The host's civ captures
two NPC cities (`coop.civ capture`, the game's cCity::Capture): three cities for three players.

1. owners  the capital is Alice's, the other two go to Bob and Carol (one each), the same on every
           machine (no shared capital: as many cities as players).
2. roles   Bob picks Military, Carol Economic (`coop.role`): every machine lists both roles.
3. trade   every pair of cities of two different players trades every 30 s: three pairs, the
           Economic governor doubles the two pairs with her city (20 + 40 + 40 = 100 per payout).
           One payout raises the shared money on every machine by that (spice income aside).
4. leave   Carol leaves: her city goes to a present player, her role is gone on every machine
           (BUG-0064), the trade pairs drop to the cities of different present owners.

  SPORE_LANE=6 python3 tools/tests/coop_party_civ_trade.py
"""
import re
import time

import uifind
from coop_civ import civ
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


def owners(con):
    """{city index: owner} of the player's cities"""
    return {k: v['owner'] for k, v in civ(con)[0].items() if v['player']}


def roles(p):
    return {int(i): r for i, r in re.findall(r'^role (\d+) \S+ (\w+)', p.call('coop.role'), re.M)}


def header(p):
    return {k: int(v) for k, v in re.findall(r'(trade_pairs|trade_total)=(-?\d+)', p.call('coop.civ'))}


def main():
    ck = Checks()
    p = Party.start('civ_npc', 3)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        ids = p.ids()
        bob, carol = p.guests
        time.sleep(3)
        hc = civ(p.host.con)[0]
        npc = [k for k, v in hc.items() if not v['player']]
        ck(len(npc) >= 2, f'[owners] the planet has NPC cities to capture ({npc})')
        for k in npc:  # (a capture can be refused: until the player civ has three cities)
            if len(owners(p.host.con)) >= 3:
                break
            print(f'  [owners] capture city {k}:', p.host.call(f'coop.civ capture {k}').strip(), flush=True)
            # the captured city asks for its specialty (a modal box): answered like a player, before
            # the next capture (the campaign's way, BUG-0290)
            for _ in range(12):
                b = uifind.shown(p.host.con, 'Military')
                if b and 600 < b[0] < 1100 and 500 < b[1] < 750:
                    uifind.press(p.host.con, 'Military')
                    print('  [owners] the specialty box: Military', flush=True)
                    break
                time.sleep(1)
            time.sleep(3)
        ok = wait(lambda: len(owners(p.host.con)) >= 3 and set(owners(p.host.con).values()) == {0, ids['Bob'], ids['Carol']}, 30)
        h = owners(p.host.con)
        print(f'  [owners] host: {h}', flush=True)
        ck(ok, f'[owners] three cities: Alice, Bob and Carol own one each, no shared capital ({h})')
        for g in p.guests:
            ck(wait(lambda: owners(g.con) == owners(p.host.con), 15), f'[owners] {g.name} shows the same owners ({owners(g.con)})')

        print('  [roles] Bob:', bob.call('coop.role 1').strip().splitlines()[0], '| Carol:',
              carol.call('coop.role 2').strip().splitlines()[0], flush=True)
        want = {ids['Bob']: 'military', ids['Carol']: 'economic'}
        for x in p.all:
            ok = wait(lambda: all(roles(x).get(i, '').lower().startswith(r[:4]) for i, r in want.items()), 10)
            ck(ok, f'[roles] {x.name} lists Bob military and Carol economic ({roles(x)})')

        # one trade payout: 3 pairs, 100
        t0 = header(p.host)
        ok = wait(lambda: header(p.host).get('trade_total', 0) > t0.get('trade_total', 0), 40)
        t1 = header(p.host)
        gain = t1.get('trade_total', 0) - t0.get('trade_total', 0)
        print(f'  [trade] {t0} -> {t1}', flush=True)
        ck(ok and t1.get('trade_pairs') == 3 and gain == 100,
           f'[trade] three trading pairs, the economic governor doubles hers: +100 per payout (+{gain}, pairs {t1.get("trade_pairs")})')
        time.sleep(3)
        m = {x.name: re.search(r'shared money=(-?[\d.]+)', x.call('coop.res')) for x in p.all}
        m = {k: float(v.group(1)) if v else None for k, v in m.items()}
        ck(len(set(round(v or 0) for v in m.values())) == 1, f'[trade] every machine shows the same money ({m})')

        ck(p.leave(carol), '[leave] Carol leaves')
        p.guests = [bob]
        time.sleep(4)
        present = {0, ids['Bob']}
        for x in p.all:
            o = owners(x.con)
            ck(set(o.values()) <= present | {253}, f'[leave] {x.name}: every city has a present owner ({o})')
            ck(ids['Carol'] not in roles(x), f'[leave] {x.name}: Carol\'s role is gone ({roles(x)})')
        p.guests = [bob, carol]
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
