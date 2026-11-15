#!/usr/bin/env python3
"""Tribe over two evenings: the members a player owns stay its own in the next session, gifts too (E1 of 0.7.15,
BUG-1154).

The owner table of the player tribe's members lived only in the host's memory: every new session (and every join,
leave and game over) dealt all members anew in turns, so members a player was given or bought were gone. The host now
keeps a claim per member (its owner's stable key, by position) in the per-player store next to its save and gives them
back after the load; while the owner is not there yet the host holds them for it.

1. Two games on the tribe fixture. The host gives two of its own members to the guest (`coop.give` with two of them
   selected): the guest owns more than its share of the turn split. The owners are noted.
2. A new evening (twoinst.session_restart): the host saves, both quit, both start again, the host loads its save, the
   guest joins.
3. On the host (its owner table, which decides who may order a member) and in the guest's game (the owners it got with
   the tribe state) the guest owns as many members as before, the host as many as before.
SPORE_FIX_OFF=store.tribe_owners: the members are dealt anew in turns, the gift is gone (FAIL).

  SPORE_LANE=9 python3 tools/tests/coop_tribe_owners_session.py
"""
import time

from coop_tribe import members
from twoinst import Checks, session_restart, start


def owners(con):
    """{owner: count} of the player tribe's members in this game"""
    out = {}
    for m in members(con).values():
        if m[3] == 'player':
            out[m[0]] = out.get(m[0], 0) + 1
    return out


def wait(pred, timeout, step=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    hm = members(host)
    mine = [i for i, v in hm.items() if v[0] == 0 and v[3] == 'player']
    print(f'  host members: {owners(host)} (host-owned ids {mine})', flush=True)
    if not ck(len(mine) >= 3, f'the host owns members to give ({len(mine)})'):
        return ck.result()
    for i in hm:
        host.call(f'coop.tribe select {i} 0')
    for i in mine[:2]:
        host.call(f'coop.tribe select {i} 1')
    print('  host:', host.call('coop.give')['out'].strip()[:160], flush=True)
    before = wait(lambda: (o := owners(host)) and o.get(1, 0) >= 2 + 1 and o, 10)
    before = owners(host)
    print(f'  after the gift: host {before}, guest {owners(guest)}', flush=True)
    ck(wait(lambda: owners(guest) == before, 10), f'the guest sees the same owners ({owners(guest)})')
    print('  host ' + next((ln for ln in host.call('coop.status')['out'].splitlines() if ln.startswith('claims:')), '-'),
          flush=True)

    host, guest, info = session_restart(host, guest)
    ck(info['saved'], 'the host saved its game before quitting')
    time.sleep(6)
    after = owners(host)
    print(f'  next session: host {after}, guest {owners(guest)} (before: {before}) | host '
          + next((ln for ln in host.call('coop.status')['out'].splitlines() if ln.startswith('claims:')), '-'), flush=True)
    ck(after.get(1, 0) == before.get(1, 0) and after.get(0, 0) == before.get(0, 0),
       f'next session: the guest owns {after.get(1, 0)} members as before ({before.get(1, 0)}, the gift included), '
       f'the host {after.get(0, 0)} ({before.get(0, 0)})')
    ck(wait(lambda: owners(guest) == after, 10), f'next session: the guest\'s game shows the same owners ({owners(guest)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
