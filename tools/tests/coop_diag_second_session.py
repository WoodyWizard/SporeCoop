#!/usr/bin/env python3
"""The support summary of a host's second session in the same game run lists its players (UIFX-10 of the external
audit, BUG-1519).

The summary's player list (diag_stats.cpp g_c.peers) is cleared at every session start, but the names behind it (a
name seen again counts as a rejoin) were cleared only by a guest's connect: on a host's second session the returning
guest was taken for a rejoin and never listed ("players none; rejoins 1").

Creature fixture, two players:
  1. the first session runs (twoinst.start): the host's summary names Bob;
  2. the guest leaves, the host ends its session (`coop.leave`) and hosts again in the same game run; Bob joins again;
  3. PASS: the host's `coop.diag summary` of the second session says "players Bob ..." (not "players none") and
     counts no rejoin. SPORE_FIX_OFF=diag.peer_names_reset: "players none; ... rejoins 1": FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_diag_second_session.py
"""
import re
import time

import lane
from twoinst import Checks, start, wait_guest_in


def summary(con):
    return (con.call('coop.diag summary')['out'] or '').strip()


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    s1 = summary(host)
    print('  first session:', s1[:240], flush=True)
    ck('players Bob' in s1, 'the first session\'s summary names Bob')
    guest.call('coop.leave')
    time.sleep(3)
    print('  host leave:', host.call('coop.leave')['out'].strip()[:80], flush=True)
    time.sleep(3)
    print('  host:', host.call(lane.HOST_CMD)['out'].strip()[:80], flush=True)
    time.sleep(2)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest, 200), "the guest is in the host's world in the second session")
    time.sleep(3)
    s2 = summary(host)
    print('  second session:', s2[:240], flush=True)
    m = re.search(r'rejoins (\d+)', s2)
    ck('players Bob' in s2, f'the second session\'s summary names Bob ({s2[:120]!r})')
    ck(m and m.group(1) == '0', f'the second session counts no rejoin of the host\'s guest ({m and m.group(1)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
