#!/usr/bin/env python3
"""BUG-0231 (GitHub #165), the share retry: a world share that fails after the host's save
("cannot share the world: ...") is sent again, so a guest in another world of the host's stage
gets the host's world.

0.7.3.0 set the guest's `world_sent` before the share and nothing cleared it when world::tick
failed later: follow_phase skipped that guest for the rest of the phase and it stayed in its own
world for good.

  1. the usual session (Creature fixture), then the guest leaves and plays a world of its own:
     a copy of the fixture planet under another name (Otherworld)
  2. the host's next share fails once (coop.worldfix failnext: a failure other than "planet file
     missing"), the guest joins: the share of its join fails
  3. within 120 s the guest plays the host's world (the retry comes 30 s after the failure)

  SPORE_FIX_OFF=core.share_retry coop_share_retry.py   (no retry: the guest stays: must FAIL)
"""
import glob
import os
import re
import shutil
import time

import lane
from twoinst import Checks, start


def world_of(con):
    m = re.search(r"world '([^']*)'", con.call('coop.avatar')['out'])
    return m.group(1) if m else None


def main():
    host, guest = start()
    ck = Checks()
    hw = world_of(host)
    print(f'  host world {hw}')
    print(' ', guest.call('coop.leave')['out'].strip())
    games = os.path.join(lane.GUEST_DIR, 'Games')
    src = next(os.path.dirname(p) for p in glob.glob(os.path.join(games, '*', 'Scalinus.spo')))
    dst = os.path.join(games, 'Game9')
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    if os.path.exists(os.path.join(dst, 'sporecoop.txt')):
        os.remove(os.path.join(dst, 'sporecoop.txt'))
    os.rename(os.path.join(dst, 'Scalinus.spo'), os.path.join(dst, 'Otherworld.spo'))
    print(' ', guest.call('coop.load Game9 Otherworld')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 120:
        if world_of(guest) == 'Otherworld.spo' and guest.call('coop.avatar')['ok']:
            break
        time.sleep(2)
    ck(world_of(guest) == 'Otherworld.spo', f'the guest plays a world of its own ({world_of(guest)})')
    guest.call('wait.frames 300 60000')
    time.sleep(5)

    print(' ', host.call('coop.worldfix failnext')['out'].strip())
    print(' ', guest.call(lane.JOIN)['out'].strip())
    t0 = time.time()
    got = None
    while time.time() - t0 < 120:
        w = world_of(guest)
        if w and w != 'Otherworld.spo' and guest.call('coop.avatar')['ok']:
            got = time.time() - t0
            break
        time.sleep(2)
    m = re.search(r'share retries (\d+)', host.call('coop.status')['out'])
    print(f'  guest world after the join: {world_of(guest)} ({"after %.0f s" % got if got else "never in 120 s"});'
          f' host share retries: {m.group(1) if m else "?"}')
    ck(got is not None, f"the guest gets the host's world after the failed share ({world_of(guest)})")
    guest.call('wait.frames 120 30000')
    ck(world_of(guest) == hw, f"the guest plays the host's world ({world_of(guest)}, host {hw})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
