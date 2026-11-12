#!/usr/bin/env python3
"""BUG-0820: the galactic core does not wait for a partner who already entered it and flew away.

Both players get the interstellar drive. The host flies to the core, does not wait (`finale alone`, the Space page's
"Enter now") and enters it; after the core's cinematic it flies away from the core (`finale goto near`: a star in the
Grox zone, not the gate). Then the guest flies to the core and zooms in (`finale enter`, what the zoom-in at the
core's centre does): the core must open for it, the host already was there.

Before the fix the core waited for every partner who was not at the gate: "The galactic core waits for your
partners: Alice" for ever (only "Enter now" got the guest in).

  coop_space_finale_gone.py   (SPORE_FIX_OFF=space.finale_entered: FAILS, the guest waits, entered=0)
"""
import time

from coop_space_finale import fin, fixture_planet, get_drive, press_through, settle, sp, to_galaxy, wait
from twoinst import Checks, start


def main():
    host, guest = start('space', planet=fixture_planet())
    ck = Checks()
    time.sleep(3)
    for who, c in (('host', host), ('guest', guest)):
        ck(fin(c).get('core', '0') != '0', f'{who}: the finale feature found the core star')
    for who, c in (('host', host), ('guest', guest)):
        get_drive(c, who, ck)

    # the host enters the core alone
    to_galaxy(host, 'host', ck)
    print('  host:', sp(host, 'finale goto core enter'))
    ck(wait(lambda: fin(host).get('ctx') == '1' and fin(host).get('zone') == '3', 60, 2), 'host: in the core\'s system')
    settle(host, 'host')
    print('  host:', sp(host, 'finale alone'))
    for _ in range(5):
        print('  host:', sp(host, 'finale enter'))
        if wait(lambda: fin(host).get('entered') == '1', 10, 1):
            break
    ck(fin(host).get('entered') == '1', f'host: entered the core alone ({fin(host)["line"]})')
    ck(wait(lambda: 'Alice' in fin(guest).get('reached_by', ''), 20), 'guest: heard that Alice entered the core')
    press_through([host], lambda: fin(host).get('busy') == '0' and fin(host).get('ctx') == '2', 240)
    settle(host, 'host')

    # and flies away from the core
    print('  host:', sp(host, 'finale goto near'))
    ck(wait(lambda: fin(host).get('zone') == '2', 60, 2), f'host: flew away from the core ({fin(host)["line"]})')
    ck(wait(lambda: ' 0:z2/' in fin(guest).get('line', ''), 20, 1),
       f'guest: sees the host away from the gate ({fin(guest)["line"]})')

    # the guest at the core: the core must open
    to_galaxy(guest, 'guest', ck)
    print('  guest:', sp(guest, 'finale goto core enter'))
    ck(wait(lambda: fin(guest).get('ctx') == '1' and fin(guest).get('zone') == '3', 60, 2), 'guest: in the core\'s system')
    settle(guest, 'guest')
    for _ in range(5):
        print('  guest:', sp(guest, 'finale enter'))
        if wait(lambda: fin(guest).get('entered') == '1' or fin(guest).get('waiting') == '1', 10, 1):
            break
    ok = wait(lambda: fin(guest).get('entered') == '1', 30, 1)
    g = fin(guest)
    print('  guest:', g['line'])
    ck(ok, f'guest: the core opens (the host entered before): entered={g.get("entered")} waiting={g.get("waiting")}')
    press_through([guest], lambda: fin(guest).get('busy') == '0', 240)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
