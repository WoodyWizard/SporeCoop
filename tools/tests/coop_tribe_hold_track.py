#!/usr/bin/env python3
"""BUG-1347: Tribe chief staff hold modifier bound to fading/pending animation track.

Citizen_SetHandheldItem (0x00C240E0) looks up the current animation via 0x009FF700, which scans
pending (+0xF4C) and playing (+0xF0C) tracks in slot order without checking fade state (+0xC0).
When a restarted/crossfaded animation leaves a pending (state 0) or fading-out (state 3) track of
the same id ahead of the live main track (state 1 or 2), 0x009FF700 binds the kind-5 hold modifier
to that dying track, and CreatureModifiers::Update (0x00C11240) deletes the hold on the very next
frame.

With tribe.hold_track ON, rebind_hold_to_main() patches the kind-5 hold modifier's track handle
(+0x04) onto the live main track handle (`((slot + 1) & 0xFF) | (gen << 8)`), so the hold starts
and persists instead of vanishing on frame 1.

Two games in the Tribe fixture:
1. Wait until the guest runs and both games show the host's chief holding its staff (item=17).
2. On the guest mirror, trigger `coop.tribe hand <cid> 17 crossfade`, which restarts the mirror's
   current animation (creating a real earlier fading-out or pending track of the same id), calls
   Citizen_SetHandheldItem, and runs rebind_hold_to_main when tribe.hold_track is ON.
3. Verify:
   - 0x009FF700 bound the modifier to a non-main track (`before != main`),
   - with tribe.hold_track ON: `after == main`, `rebound > 0`, and 0.15 s later (after several
     CreatureModifiers::Update ticks) `hold=1` is active;
   - with `SPORE_FIX_OFF=tribe.hold_track`: `after == before != main`, `rebound == 0`, and
     0.15 s later `hold=0` because 0x00C11240 deleted the hold on the first tick: must FAIL."""
import re
import time

from harness import waiter
from twoinst import Checks, game_runs, start

wait = waiter(step=0.25)


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    lead = re.findall(r'#(\d+) \S+ owner=0 .*leader=1', host.call('coop.tribe')['out'])
    cid = lead[0] if lead else '1'
    print(f"  host chief #{cid}", flush=True)

    ck(wait(lambda: 'item=17' in guest.call(f'coop.tribe hand {cid}')['out'] and
            'model=1' in guest.call(f'coop.tribe hand {cid}')['out'], 20),
       'guest chief mirror has its staff (item=17, model=1)')

    # Trigger crossfade/pending track collision on the guest mirror and attach staff
    out = guest.call(f'coop.tribe hand {cid} 17 crossfade')['out'].strip()
    print(f'  guest crossfade: {out}', flush=True)
    m = re.search(r'main=([0-9A-Fa-f]+) before=([0-9A-Fa-f]+) after=([0-9A-Fa-f]+) rebound=(\d+)', out)
    ck(m is not None, f'crossfade returned handles ({out})')
    if m:
        main_h, before_h, after_h, rebound = int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4))
        ck(main_h != 0, f'live main track handle is non-zero ({main_h:#x})')
        ck(before_h != main_h,
           f'0x009FF700 bound to non-main fading/pending track ({before_h:#x} != {main_h:#x})')
        ck(after_h == main_h,
           f'hold modifier rebound onto live main track ({after_h:#x} == {main_h:#x})')
        ck(rebound > 0, f'rebound counter incremented ({rebound})')

    # Wait 0.15 s (several CreatureModifiers::Update frames, well before kHoldUnsent = 0.35 s)
    time.sleep(0.15)
    after_hand = guest.call(f'coop.tribe hand {cid}')['out'].strip()
    print(f'  guest +0.15s:    {after_hand}', flush=True)
    ck('hold=1' in after_hand, f'hold survived CreatureModifiers::Update ticks ({after_hand})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
