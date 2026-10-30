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
"""
import sys

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
    press_through([host], lambda: fin(host).get('busy') == '0' and fin(host).get('ctx') == '2', 200)
    settle(guest, 'guest')
    ck.faults(host=host, guest=guest)
    if without:
        host.call('coop.fix space.finale_waited on')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
