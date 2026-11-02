#!/usr/bin/env python3
"""BUG-0449: a physics body without an owner must not fault the game's physics update in every frame.

A guest in tribe_walk (out/lab/20261101-204356) faulted at 0xB4C5B4 in every frame for the rest of the
test (1819 caught faults): the physics update (SporeCoop::PhysicsManager_Update) walks the body pool and
reads each body's owner (+0x40) without a check, and one live body of kind 0 had none. How the game came
to leave it so is not known (seen once in the lab), so this test puts the same state in directly
(`coop.physorphan`: the next physics update gets a live body of kind 0 without an owner that no owner
refers to, in the slot past the pool's high water) on the
guest in Tribe, lets the game run, and asserts the game's own exception counter (`exc`) stays at 0 and
the game keeps drawing frames. With the fix the guard leaves that body out of the update (kind 3).

  coop_physics_orphan.py   (SPORE_FIX_OFF=game.physics_owner: the fault at 0xB4C5B4 in every frame, it FAILS)"""
import re
import time

from twoinst import Checks, start


def left_out(con):
    m = re.search(r'physics bodies without an owner left out (\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)  # the guest's game time runs (after a cold join it is paused for 10-40 s: no physics then)
    print('  guest before:', guest.call('exc')['out'].splitlines()[0])
    n0 = left_out(guest)  # warm runs: the counter keeps counting
    out = guest.call('coop.physorphan')['out'].strip()
    print('  physorphan:', out)
    ck(out.startswith('armed'), f'the guest has the physics update guard ({out})')
    t0 = time.time()
    while time.time() - t0 < 60 and 'made without an owner' not in guest.call('log 200')['out']:
        time.sleep(1)  # the update runs only while the game time runs
    taken = 'made without an owner' in guest.call('log 400')['out']
    ck(taken, 'a live physics body of kind 0 without an owner (as on the guest that faulted)')
    out = guest.call('wait.frames 120 20000')['out'].strip()
    print('  wait.frames:', out)
    n1 = left_out(guest)
    ck(n0 >= 0 and n1 == n0 + 1, f'the guard left the body out of the physics update once ({n0} -> {n1})')
    ck('total faults: 0' in guest.call('exc')['out'], 'no fault in the game\'s physics update')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
