#!/usr/bin/env python3
"""Tribe: a guest's member taking up food when the host comes back holds no removed bundle at the next away (BUG-1084).

While the host is away (main menu, an editor, a load) the guest's own game runs its members' behaviour trees
(tribe.away); when the host is back the trees of the guest's mirrors are skipped again, frozen in whatever node they
were, and the world-object sync removes the food bundles of the guest's game that the host's game does not have (an
own ground bundle after kOwnGrace snapshots, an own carried one: wobj.bundle_carried). At the host's next away the
frozen node goes on with a bundle that is gone. The campaign's faults (caught at 0xC23469 on a guest, campaign_tribe_end
with the host in the Tribe end's City Hall editor and then the Civ editors) came from the bundle node 0x04623E0C,
state 3 (0xC25BB0 with its bundle: amount 0 or less -> the pick-up animation -> Citizen_HoldBundle 0xC23460 with the
first bundle of the member's own container, null).

1. Two games on the tribe fixture. The guest counts the host as in an editor (`coop.awaytest 1`, the editor stand-in:
   the host's game stays in its world, so the members keep their ids, as after an editor visit; a `coop.menu` +
   `coop.load` renumbers them and the guest's mirrors are made again): the guest plays alone after 4 s.
2. Food on the ground 12 m from one of the guest's members (`coop.wobj bundle ground`, what a hunt or a dropped load
   leaves; listed by the guest's game) and the member's order to take it up (`coop.tribe order <id> 14 <spot>
   18C431C`, CitizenAction 14 Bundle): the bundle node 0x04623E0C holds it (its memory +0x14). The tree is held in
   that node from its state 3 on (+5 ticks: the pick-up started; `coop.tribe freezeat`, as the host's return holds
   it, but at a known node); then the host is back (`coop.awaytest 0`, 1 s later).
3. The member's node is read until the guest sees the host back: it must be the bundle node (else the round is
   repeated, at most 3 rounds). 10 s later the sync has removed the bundle (no host partner). Checked (the game's
   own state): the member's tree holds no bundle the guest's game no longer lists.
4. The host is away again: the guest's trees run again for 25 s. Checked: no caught fault on the guest (the game's
   own exception log), and none on either game at the end.

SPORE_FIX_OFF=tribe.away_tree_end: the node still holds the removed bundle at step 3: FAIL. The fault itself needs
the freed bundle's memory to read an amount of 0 or less when the node goes on; the lab runs did not force that
(0 faults with the fix off too): the check is the stale bundle the fault came from.

  SPORE_LANE=6 python3 tools/tests/coop_tribe_away_bundle.py
"""
import re
import time

from coop_tribe_gather_stall import ai, pos_of
from coop_tribe_tools import dist, members
from coop_world_objects import objs
from twoinst import Checks, start

BUNDLE_NODE = '04623E0C'
BUNDLE_NOUN = '18C431C'


def away(guest):
    return 'host_away=1' in guest.call('coop.status')['out']


def wait_away(guest, want, limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        if away(guest) == want:
            return time.time() - t0
        time.sleep(0.5)
    return None


def guest_faults(guest):
    m = re.search(r'total faults: (\d+)', guest.call('exc')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    own = sorted(i for i, o in members(guest).items() if o != 0)
    if not ck(bool(own), f'the guest has members ({own})'):
        return ck.result()
    held = None
    for rnd in range(3):
        print(' ', guest.call('coop.awaytest 1')['out'].strip(), flush=True)
        if not ck(wait_away(guest, True, 90) is not None, f'round {rnd + 1}: the guest knows the host is away'):
            return ck.result()
        mid = own[rnd % len(own)]
        at = pos_of(guest, mid)
        chief = pos_of(guest, 1) or at
        # food on the ground 8-15 m off (what a hunt or a dropped load leaves): the member's order to take it up
        # (CitizenAction 14 Bundle, InputAction 0x1B) runs the bundle node 0x04623E0C, which holds the bundle
        k = 12.0 / max(1.0, dist(at, chief)) if at else 0
        spot = tuple(a + (c - a) * min(1.0, k) for a, c in zip(at, chief)) if at else None
        print(f'  round {rnd + 1}: member #{mid} at {at}, food at {spot}', flush=True)
        made = guest.call('coop.wobj bundle ground %.2f %.2f %.2f 5' % spot)['out'].strip()
        bm = re.match(r'bundle (\S+)', made)
        listed0 = {b['p'].upper().replace('0X', '').zfill(8) for b in objs(guest, 'game_bundle', 400)}
        print(f'  guest: ground {made}; listed by the guest\'s game: '
              f'{bool(bm) and bm.group(1).upper().replace("0X", "").zfill(8) in listed0} ({len(listed0)} bundles)', flush=True)
        print('  guest: freezeat ' + guest.call(f'coop.tribe freezeat {mid} {BUNDLE_NODE} 3 5')['out'].strip(), flush=True)
        print('  guest: ' + guest.call('coop.tribe order %d 14 %.2f %.2f %.2f %s' % ((mid,) + spot + (BUNDLE_NOUN,)))['out'].strip(),
              flush=True)
        t0, out, node, st, said = time.time(), '', None, None, -10
        while time.time() - t0 < 30:
            out, node, st = ai(guest, mid)
            if node == BUNDLE_NODE and st is not None and st >= 3:
                break
            if time.time() - said >= 10:
                said = time.time()
                print(f'  {time.time() - t0:.0f} s: {out[:200]}', flush=True)
            time.sleep(1)
        print(f'  {time.time() - t0:.0f} s: {out}', flush=True)
        print(' ', guest.call('coop.awaytest 0')['out'].strip(), flush=True)
        t0, at_back = time.time(), (out, node, st)
        while time.time() - t0 < 60 and away(guest):
            r = ai(guest, mid)
            if r[1]:
                at_back = r
            time.sleep(0.3)
        if not ck(not away(guest), f'round {rnd + 1}: the host is back'):
            return ck.result()
        print(f'  host back after {time.time() - t0:.0f} s; guest member #{mid} at the last poll before: {at_back[0]}', flush=True)
        if at_back[1] == BUNDLE_NODE:
            held = mid
            break
        print(f'  round {rnd + 1}: the member was not in the bundle node when the host came back: again', flush=True)
        time.sleep(5)
    if not ck(held is not None, 'a guest\'s member was in the bundle node 0x04623E0C when the host came back'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    for i in range(5):
        time.sleep(2)
        print(f'  {2 * i + 2} s after the host is back: guest #{held}: {ai(guest, held)[0]}', flush=True)
    m = re.search(r'bundles made=\d+ carried=\d+ removed=\d+[^;]*', guest.call('coop.wobj')['out'])
    print('  guest wobj:', m.group(0) if m else '?', flush=True)
    # the game's own state: the node the member's tree stands in and the bundle it holds (its memory +0x14), against the
    # bundles the guest's game still lists
    out, node, st = ai(guest, held)
    mm = re.search(r'mem=-?\d+ \S+ \S+ \S+ \S+ ([0-9A-F]{8})', out)
    target = mm.group(1) if mm and node == BUNDLE_NODE else None
    listed = {b['p'].upper().replace('0X', '').zfill(8) for b in objs(guest, 'game_bundle', 400)}
    print(f'  the member\'s node {node}, its bundle {target}; the guest lists {len(listed)} bundles', flush=True)
    ck(not target or target in listed, f'the guest\'s member holds no bundle its game removed in a node that goes on at '
       f'the next away (node {node}, bundle {target}, listed {target in listed if target else "-"})')
    f0 = guest_faults(guest)
    print(' ', guest.call('coop.awaytest 1')['out'].strip(), flush=True)
    ck(wait_away(guest, True, 90) is not None, 'the host is away again')
    for i in range(5):
        time.sleep(5)
        print(f'  away {5 * i + 5} s: guest #{held}: {ai(guest, held)[0]}; guest faults {guest_faults(guest)}', flush=True)
    f1 = guest_faults(guest)
    ck(f1 == f0 == 0, f'no caught fault on the guest while its trees run again ({f0} -> {f1})')
    print(' ', guest.call('coop.awaytest 0')['out'].strip(), flush=True)
    ck(wait_away(guest, False, 60) is not None, 'the host is back again')
    time.sleep(4)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
