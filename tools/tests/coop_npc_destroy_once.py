#!/usr/bin/env python3
"""BUG-0850 (regression of 0.7.11, BUG-0710): a proxy the guest's own game destroyed is not destroyed again.

destroy_proxy only unregisters a proxy whose creature the game has destroyed already (its tribe killed it, a
cleanup). 97392cd1 put a line into that if/else without braces, so destroy_creature ran in both branches: a
second cGameNounManager::DestroyInstance of an object the game had destroyed (0.7.11, 0.7.11.1).

1. Two games on the creature fixture (--fixture tribe: the tribe fixture); the guest's living proxies.
2. For --n of them the guest runs `coop.npc gamekill <id>`: the game's own DestroyInstance of the proxy's
   creature (no bookkeeping of ours, as the game does it by itself), then the proxy is dropped before the next
   npc tick (as a despawn or the stale drop in the same frame). gamefixes' DestroyInstance hook counts the
   game's calls for that object.
3. PASS when each object was destroyed by the game (its mbIsDestroyed set) and DestroyInstance ran once for it,
   and no game faulted or wrote a crash report (10 s later).

SPORE_FIX_OFF=npc.destroy_once fails: 2 DestroyInstance calls of each object.

  coop_npc_destroy_once.py [--fixture creature|tribe] [--n 3] [--wait 10]"""
import argparse
import re
import time

from twoinst import Checks, start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--n', type=int, default=3)
    ap.add_argument('--wait', type=float, default=10.0)
    ap.add_argument('--fixture', default='creature', choices=('creature', 'tribe'))
    a = ap.parse_args()
    host, guest = start(a.fixture)
    ck = Checks()
    time.sleep(15)
    near = []
    for line in guest.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) species=\S+ spawned hp=([\d.]+) dist=([\d.]+)', line)
        if m and float(m.group(2)) > 0 and ' DEAD' not in line:
            near.append((float(m.group(3)), int(m.group(1))))
    ids = [i for _, i in sorted(near)[:a.n]]
    ck(len(ids) >= min(a.n, 2), f'living proxies of the guest: {ids} (of {len(near)})')
    calls = {}
    for i in ids:
        out = guest.call(f'coop.npc gamekill {i}')['out'].strip()
        print('  guest: ' + out, flush=True)
        m = re.search(r'destroyed_by_game=(\d) destroy_calls=(\d+)', out)
        if m:
            calls[i] = (int(m.group(1)), int(m.group(2)))
    ck(len(calls) == len(ids), f'the guest\'s game destroyed {sorted(calls)} of {ids} by itself')
    ck(all(g == 1 for g, _ in calls.values()), 'each object is destroyed in the game (mbIsDestroyed)')
    twice = [i for i, (_, n) in calls.items() if n != 1]
    ck(calls and not twice,
       f'DestroyInstance ran once for each object: {dict((i, n) for i, (_, n) in calls.items())}, other than 1: {twice}')
    time.sleep(a.wait)
    print('  guest ' + guest.call('coop.npc resync')['out'].strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
