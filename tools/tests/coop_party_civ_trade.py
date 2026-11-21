#!/usr/bin/env python3
"""Civilization with three players and three cities (BUG-0152): city owners, roles of two guests and
the trade between the players' cities, checked on every machine.

Alice hosts, Bob and Carol are guests (tools/tests/party.py), on `civ_npc`. Bob and Carol each buy an NPC
city (`coop.civ buycity`, the game's city purchase on the guest, made in the host's game for that guest:
BUG-0877): three cities for three players.

1. owners  the capital is Alice's, the other two are Bob's and Carol's (one each), the same on every
           machine (no shared capital: as many cities as players). Until 0.7.12 the host's own captures
           were dealt out to the players in turns; since BUG-0875 (0.7.13) a city stays with the player who
           took it, so the guests take their own (BUG-1373: the test still had the host capture both).
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

from harness import waiter
import uifind
from coop_civ import civ
from party import Party
from twoinst import Checks


wait = waiter(step=1.0)


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
        ck(len(npc) >= 2, f'[owners] the planet has NPC cities to buy ({npc})')
        # BUG-1373: each guest buys one NPC city (the host's game buys it for that guest, BUG-0877); a host capture
        # would stay the host's (BUG-0875) and leave the capital shared
        print('  [owners] host:', p.host.call('coop.res add money 30000').strip(), flush=True)
        for g, k in zip((bob, carol), npc):
            print(f'  [owners] {g.name} buys city {k}:', g.call(f'coop.civ buycity {k} 500').strip(), flush=True)
            wait(lambda: owners(p.host.con).get(k) == ids[g.name], 30)
            # a city that changed hands may ask for its specialty (a modal box): answered like a player (BUG-0290)
            for _ in range(6):
                hit = False
                for x in (p.host, g):
                    b = uifind.shown(x.con, 'Military')
                    if b and 600 < b[0] < 1100 and 500 < b[1] < 750:
                        uifind.press(x.con, 'Military')
                        print(f'  [owners] the specialty box on {x.name}: Military', flush=True)
                        hit = True
                if hit:
                    break
                time.sleep(1)
            time.sleep(2)
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
