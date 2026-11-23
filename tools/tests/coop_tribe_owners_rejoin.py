#!/usr/bin/env python3
"""Tribe: the guest leaves and joins again: its members are its own again and the host's chief (the game's leader with
the staff) stays the host's (BUG-1450; the join case of TR-1, BUG-1429; E1, BUG-1154).

Lab 20261122-194210 tribe_held_item: after the guest's rejoin the game's leader #1 was "Bob's chief" on both screens.
The host keeps a claim per member of the player tribe; every 2 s the claims follow the owners. In 0.7.16 that follow
ran before the resplit of the tick: when it was due in the tick of the join (about 1 tick in 17) it saw the guest here
while its members were still held by the host and rewrote their claims to the host; the resplit gave the guest nothing
and the balance (a player with no member gets an even share) gave it the host's first plain members in member order:
the leader first.

1. Tribe fixture; the owners of the player tribe's members and the leader (`coop.tribe` leader=1) are noted.
   Default: the host gives the guest all its members but the leader (the guest owns more than the share a balance
   would give it back), and `coop.tribe claims_at_join 1` on the host makes the claims follow due in the tick of the
   next join (the window, hit every time). --back: the guest first gives all of its members to the host (it has no claim, so the
   balance gives it its share at the join).
2. The guest leaves (`coop.leave`), 4 s later joins again.
3. PASS (default): the hook fired; every member the guest owned is its own again and every other member keeps its
   owner (on the host and in the guest's game); the leader is the host's on both games.
   SPORE_FIX_OFF=tribe.claims_order: the guest's claims are lost at the join, the balance gives it only its share:
   FAIL. (With tribe.balance_leader off too, as 0.7.16, the leader is the first member given.)
   PASS (--back): the guest owns its share (the members / 2) again and the leader is still the host's on both games.
   SPORE_FIX_OFF=tribe.balance_leader: the leader is the first member the host gives: FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_tribe_owners_rejoin.py [--back]
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coop_tribe_looks import members, rejoin  # noqa: E402
from coop_tribe_owners_reload import claims, give  # noqa: E402
from harness import waiter  # noqa: E402
from twoinst import Checks, keyframe, start  # noqa: E402

wait = waiter(step=1.0, final=True)


def owners(con):
    """{member id: owner} of the player tribe in this game"""
    return {i: m['owner'] for i, m in members(con).items() if m['player']}


def leader(con):
    return next((i for i, m in sorted(members(con).items()) if m['player'] and m['leader']), None)


def fired(con):
    m = re.search(r'at_join=\d/(\d+)', claims(con))
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('tribe')
    ck = Checks()
    back = '--back' in sys.argv
    time.sleep(5)
    before = owners(host)
    lead = leader(host)
    print(f'  at the start: host {before} (leader #{lead}), guest {owners(guest)} | host {claims(host)}', flush=True)
    if not ck(lead is not None and before.get(lead) == 0, f'the host\'s chief is the game\'s leader #{lead} '
                                                          f'(owner {before.get(lead)})'):
        return ck.result()
    if back:
        theirs = [i for i, o in before.items() if o == 1]
        if not ck(theirs, f'the guest owns members to give ({len(theirs)})'):
            return ck.result()
        give(guest, theirs, 0)
        ck(wait(lambda: 1 not in owners(host).values(), 15), f'the guest owns no member now ({owners(host)})')
        before = owners(host)
    else:  # the host gives the guest all its members but the leader: the guest owns more than a balance would give it
        mine = [i for i, o in before.items() if o == 0 and i != lead]
        if mine:
            give(host, mine, 1)
            ck(wait(lambda: list(owners(host).values()).count(0) == 1, 15), f'the host owns only its chief now ({owners(host)})')
        before = owners(host)
        if not ck(list(before.values()).count(1) > len(before) // 2,
                  f'the guest owns more than half of the members before it leaves ({before})'):
            return ck.result()
    time.sleep(4)  # the claims follow every 2 s
    if not back:
        print('  host:', host.call('coop.tribe claims_at_join 1').get('out', '').strip(), flush=True)
    print(f'  before the leave: host {before} | host {claims(host)}', flush=True)

    if not rejoin(ck, host, guest):
        return ck.result()
    time.sleep(8)
    after = owners(host)
    print(f'  after the rejoin: host {after}, guest {owners(guest)} (before: {before}) | host {claims(host)}', flush=True)
    common = sorted(set(before) & set(after))
    if back:
        share = max(1, len(after) // 2)
        mine = sum(1 for o in after.values() if o == 1)
        ck(mine >= share, f'after the rejoin the guest owns its share again: {mine} of {len(after)} (at least {share})')
    else:
        ck(fired(host) >= 1, f'the claims follow ran in the tick of the join (the hook fired {fired(host)} times)')
        theirs = [i for i in common if before[i] == 1]
        changed = [f'#{i} {before[i]}->{after[i]}' for i in common if before[i] != after[i]]
        ck(theirs and not changed, f'every member keeps its owner across the rejoin: the guest\'s {theirs} are its own '
                                   f'again ({len(common)} compared; changed: {changed})')
    ck(after.get(lead) == 0, f'the host\'s chief #{lead} is still the host\'s member in the host\'s game '
                             f'(owner {after.get(lead)})')
    ck(wait(lambda: owners(guest).get(lead) == 0, 15),
       f'the guest\'s game shows #{lead} as the host\'s member (owner {owners(guest).get(lead)})')
    ck(wait(lambda: owners(guest) == owners(host), 15), f'the guest\'s game shows the host\'s owners ({owners(guest)})')
    keyframe('rejoined', "Tribe after the guest's rejoin: the host's chief (the leader with the staff) is labelled as "
             'the host\'s chief, the guest\'s members carry its colour' + (' (its share)' if back else ' as before'))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
