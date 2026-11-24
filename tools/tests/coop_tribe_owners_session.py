#!/usr/bin/env python3
"""Tribe over two evenings: the members a player owns stay its own in the next session, gifts too (E1 of 0.7.15,
BUG-1154).

The owner table of the player tribe's members lived only in the host's memory: every new session (and every join,
leave and game over) dealt all members anew in turns, so members a player was given or bought were gone. The host now
keeps a claim per member (its owner's stable key, by position) in the per-player store next to its save and gives them
back after the load; while the owner is not there yet the host holds them for it.

1. Two games on the tribe fixture. The host gives all but one of its own members to the guest (`coop.give` with them
   selected): the guest owns more than its share of the turn split. The owners are noted.
2. A new evening (twoinst.session_restart): the host saves, both quit, both start again, the host loads its save, the
   guest joins.
3. On the host (its owner table, which decides who may order a member) and in the guest's game (the owners it got with
   the tribe state) the guest owns as many members as before, the host as many as before.
SPORE_FIX_OFF=store.tribe_owners: the members are dealt anew in turns, the gift is gone (FAIL).

--adopt (NET-4 of the audit, BUG-1500): in the next session the guest comes as another game under the same name
(`coop.space keptkey other on` before the join: a new install, another PC), so the host holds its members for the
stored Bob; the host gives them over (`coop.store adopt Bob`). PASS: the adopt finds the stored Bob and the guest owns
its members as before on both games. SPORE_FIX_OFF=store.owner_names: a Tribe world keeps its owners only in the world
record, the store has no player named Bob ("0 stored players"): FAIL.

  SPORE_LANE=9 python3 tools/tests/coop_tribe_owners_session.py [--adopt]
"""
import sys
import time

from harness import waiter
from coop_tribe import members
from twoinst import Checks, session_restart, start


def owners(con):
    """{owner: count} of the player tribe's members in this game"""
    out = {}
    for m in members(con).values():
        if m[3] == 'player':
            out[m[0]] = out.get(m[0], 0) + 1
    return out


wait = waiter(step=1.0, final=True)


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    hm = members(host)
    mine = [i for i, v in hm.items() if v[0] == 0 and v[3] == 'player']
    print(f'  host members: {owners(host)} (host-owned ids {mine})', flush=True)
    if not ck(len(mine) >= 2, f'the host owns members to give ({len(mine)})'):
        return ck.result()
    for i in hm:
        host.call(f'coop.tribe select {i} 0')
    for i in mine[:len(mine) - 1]:  # all but one: the guest owns more than its share of the turns
        host.call(f'coop.tribe select {i} 1')
    print('  host:', host.call('coop.give')['out'].strip()[:160], flush=True)
    wait(lambda: owners(host).get(0, 0) == 1, 10)
    before = owners(host)
    print(f'  after the gift: host {before}, guest {owners(guest)}', flush=True)
    ck(wait(lambda: owners(guest) == before, 10), f'the guest sees the same owners ({owners(guest)})')
    print('  host ' + next((ln for ln in host.call('coop.status')['out'].splitlines() if ln.startswith('claims:')), '-'),
          flush=True)

    adopt = '--adopt' in sys.argv
    if adopt:
        # Isolate store.owner_names: own_history also writes a per-player record for Bob in Tribe, which would put
        # Bob's name in the store even when store.owner_names is off.
        host.call('coop.fix core.own_history off')
    other = (lambda g: print('  guest:', g.call('coop.space keptkey other on')['out'].strip(), flush=True)) if adopt else None
    host, guest, info = session_restart(host, guest, before_join=other)
    ck(info['saved'], 'the host saved its game before quitting')
    time.sleep(6)
    if adopt:  # BUG-1500: the guest is a stranger to the store until the host's word
        print(f'  next session, before the adopt: host {owners(host)}', flush=True)
        r = host.call('coop.store adopt Bob')
        print('  host adopt:', (r.get('out') or '').strip()[:160], flush=True)
        ck(r.get('ok') and "are Bob's now" in (r.get('out') or ''), 'coop.store adopt finds the stored Bob of this Tribe world')
        time.sleep(6)
    after = owners(host)
    print(f'  next session: host {after}, guest {owners(guest)} (before: {before}) | host '
          + next((ln for ln in host.call('coop.status')['out'].splitlines() if ln.startswith('claims:')), '-'), flush=True)
    ck(after.get(1, 0) == before.get(1, 0) and after.get(0, 0) == before.get(0, 0),
       f'next session: the guest owns {after.get(1, 0)} members as before ({before.get(1, 0)}, the gift included), '
       f'the host {after.get(0, 0)} ({before.get(0, 0)})')
    ck(wait(lambda: owners(guest) == after, 10), f'next session: the guest\'s game shows the same owners ({owners(guest)})')
    if adopt:
        guest.call('coop.space keptkey other off')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
