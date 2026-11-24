#!/usr/bin/env python3
"""BUG-1458: Tribe chief staff (and held items) on the same hand side on host and guest.

Citizen_PickHandBone (0x00C24270) picks a citizen's primary hand slot as `mObjectId % num_hands`,
so a guest mirror has an independent object ID and picks the opposite hand ~50% of the time,
making the chief's staff appear raised on its left on one machine and on its right on the other.

With tribe.hand_side ON, the host replicates each member's active hand slot via fields kind
`tribe_member_hand` (F_HAND_SLOT = 8) and the guest syncs cCreatureCitizen+0xFC4/+0xFC8 and
re-attaches any held item cleanly onto that hand.

Two games in the Tribe fixture:
1. Wait until the guest runs and both games show the host's chief with item=17 and model=1.
2. Verify both games report >=2 hands and agree on the chief's hand slot and side.
3. Deterministically flip the guest mirror to its opposite hand (`coop.tribe handflip <cid>`,
   reproducing the 50% mObjectId%num_hands mismatch).
4. Wait up to 5 s:
   - With tribe.hand_side ON, `tribe_member_hand` reconciles the guest mirror back to the host's
     hand slot and side with `model=1`, `item=17`, and `side_sets` incremented.
   - With `SPORE_FIX_OFF=tribe.hand_side`, the mirror stays on the opposite hand: must FAIL."""
import re
import time

from harness import waiter
from twoinst import Checks, game_runs, start

wait = waiter(step=0.25)


def parse_hand(out):
    def iv(key):
        m = re.search(rf'\b{key}=(-?\d+)', out)
        return int(m.group(1)) if m else None
    return {
        'item': iv('item'),
        'model': iv('model'),
        'hand': iv('hand'),
        'slot': iv('slot'),
        'hands': iv('hands'),
        'side': iv('side'),
        'side_sets': iv('side_sets') or 0,
        'raw': out.strip(),
    }


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    lead = re.findall(r'#(\d+) \S+ owner=0 .*leader=1', host.call('coop.tribe')['out'])
    cid = lead[0] if lead else '1'
    print(f"  host chief #{cid}", flush=True)

    ck(wait(lambda: parse_hand(host.call(f'coop.tribe hand {cid}')['out'])['item'] == 17 and
            parse_hand(guest.call(f'coop.tribe hand {cid}')['out'])['model'] == 1, 20),
       'both games show the chief holding its staff (item=17, model=1)')

    hh = parse_hand(host.call(f'coop.tribe hand {cid}')['out'])
    gh = parse_hand(guest.call(f'coop.tribe hand {cid}')['out'])
    print(f'  initial host:  {hh["raw"]}', flush=True)
    print(f'  initial guest: {gh["raw"]}', flush=True)
    ck(hh['hands'] is not None and hh['hands'] >= 2, f'chief has >=2 hands ({hh["hands"]})')
    ck(hh['slot'] is not None and hh['slot'] > 0, f'host chief hand slot valid ({hh["slot"]})')
    ck(wait(lambda: parse_hand(guest.call(f'coop.tribe hand {cid}')['out'])['slot'] == hh['slot'], 5),
       f'guest chief starts on host hand slot {hh["slot"]}')

    sets0 = parse_hand(guest.call(f'coop.tribe hand {cid}')['out'])['side_sets']
    flip = guest.call(f'coop.tribe handflip {cid}')['out'].strip()
    print(f'  guest handflip: {flip}', flush=True)
    gh_bad = parse_hand(guest.call(f'coop.tribe hand {cid}')['out'])
    print(f'  after flip:    {gh_bad["raw"]}', flush=True)
    ck(gh_bad['slot'] != hh['slot'],
       f'handflip moved guest mirror to opposite hand (guest slot={gh_bad["slot"]} vs host slot={hh["slot"]})')

    reconciled = wait(lambda: parse_hand(guest.call(f'coop.tribe hand {cid}')['out'])['slot'] == hh['slot'], 6)
    gh_end = parse_hand(guest.call(f'coop.tribe hand {cid}')['out'])
    print(f'  after sync:    {gh_end["raw"]}', flush=True)

    ck(reconciled, f'guest chief hand slot reconciled to host ({gh_end["slot"]} == {hh["slot"]})')
    ck(gh_end['side'] == hh['side'], f'guest chief hand side matches host ({gh_end["side"]} == {hh["side"]})')
    ck(gh_end['item'] == 17 and gh_end['model'] == 1,
       f'staff remains attached after hand-side sync (item={gh_end["item"]} model={gh_end["model"]})')
    ck(gh_end['side_sets'] > sets0, f'tribe.hand_side applied the hand slot ({sets0} -> {gh_end["side_sets"]})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
