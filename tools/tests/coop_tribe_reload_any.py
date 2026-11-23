#!/usr/bin/env python3
"""CORE-13 of the external audit (BUG-1505): the host's reload after the live Creature -> Tribe change also runs when
the last pending share ended without going out.

After the live change the host loads its own share save once that save went out (BUG-0332: a live change and a load
of the same save give two grounds; the guests load the save). The reload ran only when a share succeeded with nothing
pending after it: with 3+ players a guest leaving with its share still pending, or a last share that failed, left the
reload due, and the host kept a ground the other guests did not have until a later share.

Three players on the tribe fixture (tools/tests/party.py: Alice hosts, Bob and Carol):
  1. the host's reload is made due as by the live change (`coop.tribe reload arm`, the stage change's own call);
  2. Carol's next share fails (`coop.worldfix failpeer <Carol>`), the host shares with Bob and Carol at once
     (`coop.worldfix queue <Bob> <Carol>`): Bob's share goes out first, Carol's fails after it;
  3. PASS: within 60 s the host's game loads its share save (its mode shows the load; `coop.tribe reload`
     host_reloads one more) and plays its Tribe world again; Bob is in the host's world afterwards; key frame of the
     host's Tribe world after the reload.
SPORE_FIX_OFF=world.reload_after_any: no reload (the last share failed): FAIL.

  SPORE_LANE=5 python3 tools/tests/coop_tribe_reload_any.py [--players 3]   (3 only: the lab counts its games by it)
"""
import argparse
import re
import time

import twoinst
from party import Party
from twoinst import Checks


def reloads(p):
    m = re.search(r'host_reloads=(\d+)', p.call('coop.tribe reload'))
    return int(m.group(1)) if m else -1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--players', type=int, default=3, choices=[3])
    ap.parse_args()
    ck = Checks()
    p = Party.start('tribe', 3)
    try:
        ids = p.ids()
        alice, bob, carol = p.host, p.guests[0], p.guests[1]
        print(f'  ids: {ids}', flush=True)
        if not p.check_together(ck, 'join'):
            return ck.result()
        time.sleep(5)
        r0 = reloads(alice)
        print(' ', alice.call('coop.tribe reload arm').strip(), flush=True)
        print(' ', alice.call(f'coop.worldfix failpeer {carol.pid}').strip(), flush=True)
        print(' ', alice.call(f'coop.worldfix queue {bob.pid} {carol.pid}').strip(), flush=True)
        t0, loaded = time.time(), False
        while time.time() - t0 < 60:
            m = alice.call('game.mode')
            if 'Load' in m or reloads(alice) > r0:
                loaded = True
                break
            time.sleep(0.3)
        print(f'  {time.time() - t0:.0f} s: host mode {alice.call("game.mode").strip()}, host_reloads {r0} -> '
              f'{reloads(alice)}', flush=True)
        ck(loaded and reloads(alice) == r0 + 1,
           "the host's game loads its own share save although Carol's share (the last pending one) failed")
        t1 = time.time()
        while time.time() - t1 < 180 and not ('Tribe' in alice.call('game.mode') and alice.ok('coop.avatar')):
            time.sleep(1)
        ck('Tribe' in alice.call('game.mode'), f"the host plays its Tribe world again ({alice.call('game.mode').strip()})")
        ck(p.wait_in(bob, 200), "Bob is in the host's world after the reload")
        twoinst.keyframe('reloaded', "the host's Tribe world after its reload of the share save (the same village "
                         "ground as Bob's)", players={'host': alice.con, 'guest': bob.con})
        p.faults(ck)
    finally:
        p.stop()
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
