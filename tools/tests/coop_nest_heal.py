#!/usr/bin/env python3
"""The home nest heals the guest as it heals the host (BUG-0496, a player: "I don't get any heals from it").

Each player with the co-op pack regeneration off (`coop.team off`) stands at the home nest with its health set
low, then 100 m away from it: its own game's health must climb at the nest (the game's nest heal) and the guest's
the same as the host's. Not reproduced in the fixture (lab 20261103-235806: both heal): a regression guard. Read from the game's own health of the avatar (coop.avatar)."""
import time

from coop_team import hp, pos
from twoinst import Checks, start


def heal(con, secs=12):
    con.call('coop.team hp 3')
    time.sleep(0.5)
    a = hp(con)[0]
    time.sleep(secs)
    b = hp(con)[0]
    return a, b


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team off')
    time.sleep(2)
    res = {}
    for con, who in ((host, 'host'), (guest, 'guest')):
        a, b = heal(con)
        print(f'  {who} at its nest: hp {a} -> {b}', flush=True)
        p = pos(con)
        con.call('coop.npc gopos %.2f %.2f %.2f' % (p[0] + 100, p[1], p[2]))
        time.sleep(3)
        fa, fb = heal(con)
        print(f'  {who} 100 m away: hp {fa} -> {fb}', flush=True)
        res[who] = (a, b, fa, fb)
        con.call('coop.npc gopos %.2f %.2f %.2f' % p)
        time.sleep(2)
    for c in (host, guest):  # warm runs reuse these games: the team mechanics on again for the next test
        c.call('coop.team on')
    for who, (a, b, fa, fb) in res.items():
        ck(a is not None and b is not None and b > a + 1, f'{who}: the home nest heals it ({a} -> {b})')
        ck(fa is not None and fb is not None and fb <= fa + 1, f'{who}: no heal 100 m from the nest ({fa} -> {fb}): the heal is the nest\'s')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
