#!/usr/bin/env python3
"""A guest that rejoins and loads the host's world does not fault in Herd_Update (BUG-0256).

The way it was seen (fix/w5-core, coop_vote_drop_editor.py with both cases in one session):
  1. host and guest play; an evolution vote passes (the host goes into its editor), the guest
     leaves while its own editor is due (with core.own_editor_end off its editor opens offline,
     as on 0.7.3.0) and leaves that editor again;
  2. the host leaves its editor, the guest joins again, the host shares its world, the guest loads it;
  3. the guest removed a saved creature of its own species ("without tuning in a herd like ours")
     and faulted every frame at 0x00C0B8D2 (Creature_IsAdult, a null member of a herd) called from
     Herd_Update+0x61.
Checked: the guest is back in the host's world, plays 20 s there, and neither game faulted.

  coop_rejoin_saved_puppet.py   (SPORE_FIX_OFF=npc.herd_nulls must FAIL)"""
import time

import lane
from coop_vote_drop_editor import back_to, mode, stays, vote
from twoinst import Checks, start, wait_guest_in


def main():
    host, guest = start()
    ck = Checks()
    for c in (host, guest):
        print(' ', c.call('coop.fix core.own_editor_end off')['out'].strip())
    time.sleep(2)
    m0 = mode(guest)
    print(' ', guest.call('coop.vote hold 40')['out'].strip().splitlines()[0])
    print(' ', host.call('coop.vote editor')['out'].strip().splitlines()[0])
    t0 = time.time()
    while time.time() - t0 < 15 and 'vote active' not in vote(guest):
        time.sleep(0.5)
    ck('vote active' in vote(guest), 'the guest sees the evolution vote')
    host.call('coop.vote go')
    t0 = time.time()
    while time.time() - t0 < 15 and 'own editor due 1' not in vote(guest):
        time.sleep(0.3)
    print(' ', guest.call('coop.leave')['out'].strip())
    other = stays(guest, m0, 55)
    print(f'  guest mode after the Leave and the hold: {other or m0}')
    if other:
        back_to(guest, m0)
    ck(back_to(host, m0, 90), 'the host is back in its stage after its editor')
    time.sleep(3)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest joined again and plays the host\'s world')
    time.sleep(6)
    wait_guest_in(host, guest, timeout=120)  # a second share after the first load
    time.sleep(20)  # Herd_Update runs over the loaded world every frame
    print('  guest ' + guest.call('coop.npc herdnulls')['out'].strip(), flush=True)
    ck(guest.call('coop.avatar')['ok'], 'the guest plays in the host\'s world')
    for c in (host, guest):
        c.call('coop.fix core.own_editor_end on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
