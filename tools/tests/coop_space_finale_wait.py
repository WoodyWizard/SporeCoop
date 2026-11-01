#!/usr/bin/env python3
"""Space finale: the host that waited at the core counts its entry as together (BUG-0320).

The host flies to the galactic core and enters: the core waits for the guest. The guest flies to the
gate (galaxy view, `finale goto core` without entering), so the host's wait ends and the host enters
first, before any "reached" message of the guest. The host's `coop.space finale` must count that
entry as together (entries_with); the old code counted it as alone because the wait flag was cleared
the moment everybody was at the gate. Before the fix `coop_space_finale.py` failed on this when the
host happened to enter first (lab 20261030-142729).

  SPORE_LANE=9 python3 tools/tests/coop_space_finale_wait.py [--without-fix]

--without-fix (or SPORE_FIX_OFF=space.finale_waited): `coop.fix space.finale_waited off` on the
host: entries_with stays 0 (FAIL).

Then, both in the core's system (the host in the core's cinematic, the guest at the gate), each game's
drawn partner ship against the partner's own ship (coop.fleet track, BUG-0378): SPORE_FIX_OFF=
space.puppet_pin draws the host's ship 20.7 m above where it is (FAIL).
"""
import sys
import time

from coop_space_drawn import compare, dump, report
from coop_space_finale import fin, fixture_planet, get_drive, press_through, settle, sp, to_galaxy, wait
from twoinst import Checks, start


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space', planet=fixture_planet())
    ck = Checks()
    if without:
        print(' ', host.call('coop.fix space.finale_waited off')['out'].strip())
    for who, c in (('host', host), ('guest', guest)):
        get_drive(c, who, ck)
    to_galaxy(host, 'host', ck)
    print('  host:', sp(host, 'finale goto core enter'))
    wait(lambda: fin(host).get('ctx') == '1' and fin(host).get('zone') == '3', 60, 2)
    settle(host, 'host')
    for _ in range(5):  # what the zoom-in at the core's centre does (through the wait)
        print('  host:', sp(host, 'finale enter'))
        if wait(lambda: fin(host).get('waiting') == '1', 10, 1):
            break
    ok = fin(host).get('waiting') == '1'
    print('  host:', fin(host)['line'])
    ck(ok and fin(host).get('entered') == '0', 'host: at the core, waiting for the guest')
    if not ok:
        return ck.result()
    to_galaxy(guest, 'guest', ck)
    print('  guest:', sp(guest, 'finale goto core'))  # to the gate only: the guest does not enter
    ok = wait(lambda: fin(host).get('entered') == '1', 60, 1)
    h, g = fin(host), fin(guest)
    print('  host:', h['line'])
    print('  guest:', g['line'])
    ck(ok, 'host: entered the core once the guest was at the gate')
    ck(g.get('entered') == '0', 'guest: did not enter (the host entered first)')
    ck(int(h.get('entries_with', '0')) >= 1, 'host: the entry after waiting counts as together (entries_with)')
    # BUG-0378: both in the core's system (10 s): where the guest draws the host's ship (the game's own update
    # in the core keeps every ship 20-30 m above its orbit point: the puppet was drawn 20.7 m too high)
    for c in (host, guest):
        c.call('coop.fleet track on')
    time.sleep(10)  # the core's system draws at 5-15 fps in the lab: 40+ rows
    hv, gv = dump(host), dump(guest)
    for c in (host, guest):
        c.call('coop.fleet track off')
    report(ck, 'core: the host ship on the guest', *compare(gv, hv, ('p', 0), ('m', 0)), need=20)
    # printed only: the guest's ship orbits at the gate (up to ~210 m/s in the core) and a short
    # interpolation overshoot when it turns or stops is not this test's subject (BUG-0388)
    report(ck, 'core: the guest ship on the host', *compare(hv, gv, ('p', 1), ('m', 1)), check=False)
    press_through([host], lambda: fin(host).get('busy') == '0' and fin(host).get('ctx') == '2', 200)
    settle(guest, 'guest')
    ck.faults(host=host, guest=guest)
    if without:
        host.call('coop.fix space.finale_waited on')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
