#!/usr/bin/env python3
"""Tribe stage with four players (BUG-0152): the split of the player tribe, chiefs per owner, a gift
from guest to guest and the new split when the fourth player leaves and joins again, each checked
on every machine.

Alice hosts, Bob, Carol and Dave are guests (tools/tests/party.py). The host adds 6 adults
(`coop.tribe spawn 6`) so every player can have more than one.

1. split    every player owns members of the player tribe; every guest's game shows the same owner
            for every member as the host's.
2. chiefs   each of the four players has a chief, the same on every machine. (Their size is not
            compared: GetDesiredScale of a member differs between machines and from moment to moment,
            lab 20261030-012230.)
3. gift     Bob selects one of his members and gives it to Carol (`coop.give <id>`, the co-op
            window's Give): the host, Carol and Dave show Carol as its owner, Bob no longer has it
            selected, Carol can select it (it stays selected: she owns it).
4. leave    Dave leaves: no member is Dave's any more (host and every guest), three chiefs.
5. rejoin   Dave joins again: he owns members again and has a chief, the same on every machine.

  SPORE_LANE=6 python3 tools/tests/coop_party_tribe.py
"""
import re
import time

from coop_tribe import members
from coop_tribe_chief_removed import chiefs
from party import Party
from twoinst import Checks


def owners(con):
    """{member id: owner} of the player tribe as this game has it"""
    return {i: v[0] for i, v in members(con).items() if v[3] == 'player'}


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def same_split(ck, p, label, want_owners):
    """every machine shows the host's owners; the owners are exactly want_owners"""
    def ok():
        h = owners(p.host.con)
        return h and set(h.values()) == want_owners and all(owners(g.con) == h for g in p.guests)
    wait(ok, 30, 2)
    h = owners(p.host.con)
    print(f'  [{label}] host owners {sorted(h.items())}', flush=True)
    ck(set(h.values()) == want_owners, f'[{label}] the members belong to exactly {sorted(want_owners)} ({sorted(set(h.values()))})')
    for g in p.guests:
        go = owners(g.con)
        diff = {i: (h.get(i), go.get(i)) for i in set(h) | set(go) if h.get(i) != go.get(i)}
        ck(not diff, f'[{label}] {g.name}\'s game shows the host\'s owners ({len(go)} members, differ: {diff})')
    return h


def same_chiefs(ck, p, label, n):
    wait(lambda: len(chiefs(p.host.con)) == n and all(chiefs(g.con) == chiefs(p.host.con) for g in p.guests), 30, 2)
    hc = chiefs(p.host.con)
    ck(len(hc) == n, f'[{label}] {n} chiefs, one per player ({hc})')
    for g in p.guests:
        ck(chiefs(g.con) == hc, f'[{label}] {g.name} shows the same chiefs ({chiefs(g.con)})')
    return hc


def main():
    ck = Checks()
    p = Party.start('tribe', 4)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        ids = p.ids()
        bob, carol, dave = p.guests
        print(' ', p.host.call('coop.tribe spawn 6').strip(), flush=True)
        time.sleep(6)
        h = same_split(ck, p, 'split', {0, ids['Bob'], ids['Carol'], ids['Dave']})
        hc = same_chiefs(ck, p, 'chiefs', 4)

        # a gift from guest to guest
        mine = [i for i, o in owners(bob.con).items() if o == ids['Bob']]
        ck(mine, f'[gift] Bob owns members ({mine})')
        if mine:
            gift = mine[-1]
            for i in members(bob.con):
                bob.call(f'coop.tribe select {i} 0')
            bob.call(f'coop.tribe select {gift} 1')
            print('  [gift] Bob:', bob.call(f'coop.give {ids["Carol"]}'), flush=True)
            ok = wait(lambda: owners(p.host.con).get(gift) == ids['Carol'] and owners(carol.con).get(gift) == ids['Carol']
                      and owners(dave.con).get(gift) == ids['Carol'], 10)
            ck(ok, f'[gift] the host, Carol and Dave show Carol as the owner of #{gift} '
                   f'({owners(p.host.con).get(gift)}, {owners(carol.con).get(gift)}, {owners(dave.con).get(gift)})')
            ck(members(bob.con).get(gift, (0, 1))[1] == 0, '[gift] Bob no longer has it selected')
            sel = []
            for _ in range(3):  # (a select right after the owner changed was lost once: 20261030-014410)
                print('  [gift] Carol:', carol.call(f'coop.tribe select {gift} 1').strip(), flush=True)
                time.sleep(1.5)  # a member of somebody else is dropped from the selection every 0.1 s
                sel.append(members(carol.con).get(gift, (0, 0))[1])
                if sel[-1] == 1:
                    break
            ck(sel[-1] == 1, f'[gift] Carol selects it and it stays selected (hers): {sel}')

        # the fourth player leaves: a new split among three
        ck(p.leave(dave), '[leave] Dave leaves')
        time.sleep(3)
        p.guests = [bob, carol]
        same_split(ck, p, 'leave', {0, ids['Bob'], ids['Carol']})
        same_chiefs(ck, p, 'leave', 3)
        p.guests = [bob, carol, dave]
        ck(p.join(dave), '[rejoin] Dave joins again')
        ids = p.ids()
        time.sleep(4)
        same_split(ck, p, 'rejoin', {0, ids['Bob'], ids['Carol'], ids['Dave']})
        same_chiefs(ck, p, 'rejoin', 4)
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
