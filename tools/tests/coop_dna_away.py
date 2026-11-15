#!/usr/bin/env python3
"""DNA a guest earns while the host has no creature reaches the pack (BUG-0151).

A guest sends what it earns to the host (MSG_DNA_ADD); the host applied it only with a creature
of its own, so DNA earned while the host was in an editor, a loading screen or just leaving for
the main menu was dropped. While the host is "away" (main menu) the guest keeps it and sends it
when the host is back.

1. The host leaves for the main menu (`coop.menu`). As soon as the host has no creature (before
   the guest counts it as away), the guest earns 7 DNA through the game (`coop.dna add`:
   AddEvolutionPoints, the hooked game function): it goes to the host.
2. Once the guest knows the host is away it earns 11 more (kept on the guest).
3. The host loads its world again: the host's game DNA total is its saved total + 18.

--nofix: `coop.fix core.away_dna off` on both (the old behaviour): the 7 are lost, must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_dna_away.py [--nofix]
"""
import argparse
import re
import time

from twoinst import Checks, lane_state, start


def dna(con):
    m = re.search(r'dna=(-?[\d.]+)', con.call('coop.dna')['out'])
    return float(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            c.call('coop.fix core.away_dna off')
    time.sleep(5)
    d0, g0 = dna(host), dna(guest)
    print(f'  dna host {d0} guest {g0}')
    print(' ', host.call('coop.menu')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 20 and host.call('coop.avatar')['ok']:
        time.sleep(0.1)
    st = guest.call('coop.status')['out']
    early = 'away=0' in st
    print(' ', guest.call('coop.dna add 7')['out'].strip(), '(guest not away yet)' if early else '(guest away already)')
    ck(early, 'the first gain is sent while the host has no creature and the guest does not count it away yet')
    t0 = time.time()
    while time.time() - t0 < 60 and 'host_away=1' not in guest.call('coop.status')['out']:
        time.sleep(1)
    print(' ', guest.call('coop.dna add 11')['out'].strip())
    st = [ln for ln in guest.call('coop.status')['out'].splitlines() if ln.startswith('dna=')]
    print('  guest:', st[0] if st else '')
    lst = lane_state()
    slot, name = lst.get('planet', 'Game0/Scalinus').split('/')
    if lst.get('slot', 9) != 9:
        slot = f'Game{lst["slot"]}'
    print(' ', host.call(f'coop.load {slot} {name}')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 150:
        if host.call('coop.avatar')['ok'] and 'host_away=0' in guest.call('coop.status')['out']:
            break
        time.sleep(1)
    time.sleep(8)
    d1, g1 = dna(host), dna(guest)
    hs = [ln for ln in host.call('coop.status')['out'].splitlines() if ln.startswith('dna=')]
    print(f'  dna host {d0} -> {d1}, guest {g0} -> {g1}; host: {hs[0] if hs else ""}')
    ck(d1 is not None and d0 is not None and abs((d1 - d0) - 18) < 0.6,
       f'the host\'s game DNA grew by both gains (7 + 11): {d0} -> {d1}')
    ck(g1 is not None and d1 is not None and abs(g1 - d1) < 0.6, 'the guest has the same total')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
