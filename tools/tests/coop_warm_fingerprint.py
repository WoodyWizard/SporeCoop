#!/usr/bin/env python3
"""D2 (docs/plan-0.7.15.md, audit TST-4; docs/06-testing.md#testing-warm-fingerprint): a warm reset is taken only when
the games and the profile files are as a cold start leaves them.

Four starts of the lane's games in one test (the inner ones warm-capable, SPORE_WARM=1):
  1. any start (the lab's warm or cold one);
  2. nothing left over: the next start is warm (the fingerprint after the reset is the cold start's);
  3. a planted leftover (`coop.testreset plant 7` on the guest: state of a module whose reset forgets it): the next
     start refuses the warm reuse, names `guest test.planted_leftover`, starts cold, and the guest's new game holds
     nothing;
  4. BUG-1176: a record of the guest's own creature for the fixture world (`coop.creations worlds set creature <own>`,
     what any test that ends an own editor or chooses its creation leaves in worlds.ini): the next start refuses the
     warm reuse naming `guest SporeCoop/worlds.ini`, and the cold start puts worlds.ini back to the fixture state
     (no record: the guest plays the world's species).

Twins: SPORE_WARM_FINGERPRINT_FIX=0 (no comparison: starts 3 and 4 stay warm on the leftover: FAIL);
SPORE_WORLDS_INI_RESET_FIX=0 (the cold start keeps worlds.ini: the record survives step 4: FAIL).

  coop_warm_fingerprint.py
"""
import os
import re
import time

import creations_state
import twoinst
from twoinst import Checks, start


def planted(con):
    m = re.search(r'test\.planted_leftover ([0-9A-F]{8})', con.call('coop.testreset fp')['out'])
    return int(m.group(1), 16) if m else 0


def record(guest):
    m = re.search(r'creature=([0-9A-F]{8})', guest.call('coop.creations worlds')['out'])
    return m.group(1) if m else None


def again(what):
    t0 = time.time()
    os.environ['SPORE_WARM'] = '1'
    host, guest = start()
    w = dict(twoinst.LAST_WARM)
    print(f'  start {what}: {"warm" if w.get("warm") else "cold"} in {time.time() - t0:.0f} s'
          + (f', fingerprint {w["fp_s"]:.2f} s' if 'fp_s' in w else '')
          + (f' ({w["why"][:300]})' if w.get('why') else ''), flush=True)
    return host, guest, w


def main():
    host, guest = start()
    ck = Checks()

    host, guest, w = again('2 (nothing left over)')
    ck(w.get('warm') and 'fp_s' in w, 'nothing left over: the next start is warm and compared its fingerprint '
       f'({w})')
    ck(w.get('fp_s', 9) < 1.0, f'the fingerprint costs the warm reset less than a second ({w.get("fp_s")} s)')

    print('  guest:', guest.call('coop.testreset plant 7')['out'].strip(), flush=True)
    ck(planted(guest) == 7, 'the guest holds the planted leftover')
    host, guest, w = again('3 (a planted leftover)')
    ck(not w.get('warm') and 'guest test.planted_leftover' in (w.get('why') or ''),
       f'a planted leftover refuses the warm reuse and names the entry ({(w.get("why") or "warm")[:200]})')
    ck(planted(guest) == 0, f'the guest\'s game after it holds no planted leftover ({planted(guest)})')

    own = creations_state.own_keys(guest)
    ck(own, f'the guest has own creatures (fixture creations-guest: {own[:3]})')
    if own:
        guest.call(f'coop.creations worlds set creature {own[0]}')
        print('  guest:', guest.call('coop.creations worlds')['out'].strip(), flush=True)
    rec0 = record(guest)
    ck(own and rec0 == own[0], f'the guest\'s worlds.ini records its own creature for this world ({rec0})')
    host, guest, w = again('4 (a worlds.ini record, BUG-1176)')
    ck(not w.get('warm') and 'guest SporeCoop/worlds.ini' in (w.get('why') or ''),
       f'the record refuses the warm reuse and names worlds.ini ({(w.get("why") or "warm")[:200]})')
    rec1 = record(guest)
    ck(rec1 == '00000000', f'after the cold start the guest has no record for the fixture world (BUG-1176): {rec1}')
    ck(twoinst.file_digest(os.path.join(twoinst.lane.GUEST_DIR, 'SporeCoop', 'worlds.ini')) == 'none',
       'the guest\'s worlds.ini is in the fixture state (no record)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
