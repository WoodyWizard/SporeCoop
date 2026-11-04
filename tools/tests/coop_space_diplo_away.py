#!/usr/bin/env python3
"""BUG-0544: a guest's diplomacy while the host is out of Space is not lost.

The host is out of Space (`coop.space away 1`: what an editor or an adventure entered from Space gives
the mod; the Space world lives on) while the guest gives an NPC empire of the fixture's contacts a gift
(ApplyRelationship with the gift event, as the comm screen does; the money goes through the shared
budget). Back in Space the host's relation table must have the gift, and 25 s later (the host's
periodic table goes out every 20 s) the guest's must still have it. Before the fix the host dropped the
message while it was away: its table never got the gift and its next table took it from the guest too.

  coop_space_diplo_away.py   (SPORE_FIX_OFF=diplomacy.keep_away: FAILS)"""
import re
import time

from coop_space_diplomacy import FIXTURE, GIFT, rec, sp, space_planet, table, wait
from twoinst import Checks, start


def gift(con, pol):
    m = re.search(GIFT + r'=(-?[\d.]+)', rec(con, pol))
    return float(m.group(1)) if m else None


def main():
    host, guest = start(FIXTURE, planet=space_planet())
    ck = Checks()
    time.sleep(4)
    ht, gt = table(host), table(guest)
    print('  host table:', ht, '\n  guest table:', gt)
    known = sorted(p for p in ht if 'flags=' in ht[p] and int(re.match(r'flags=(\d+)', ht[p]).group(1)) & 48)
    ck(known, 'the fixture has a contacted NPC empire')
    if not known:
        return ck.result()
    p = known[0]
    g0 = gift(host, p)
    ck(ht.get(p) == gt.get(p), f'both copies have the same record of {p} ({ht.get(p)})')
    print('  host away:', sp(host, 'away 1'))
    try:
        print('  guest gift:', sp(guest, 'diplomacy apply', p, GIFT, 3))
        ck(wait(lambda: gift(guest, p) not in (None, g0), 5), f'the guest\'s relation has the gift ({rec(guest, p)})')
        g1 = gift(guest, p)
        time.sleep(4)
        print('  host (away):', sp(host, 'diplomacy'))
    finally:
        print('  host back:', sp(host, 'away 0'))
    ok = wait(lambda: gift(host, p) == g1, 10)
    print(f'  host {p}: {rec(host, p)}')
    ck(ok, f'back in Space the host\'s relation has the guest\'s gift ({g0} -> {gift(host, p)}, the guest {g1})')
    time.sleep(25)
    print(f'  guest {p} 25 s later: {rec(guest, p)}')
    print('  guest:', sp(guest, 'diplomacy'))
    ck(gift(guest, p) == g1, f'after the host\'s next table the guest still has its gift ({gift(guest, p)})')
    ck(rec(host, p) == rec(guest, p), 'both copies have the same record')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
