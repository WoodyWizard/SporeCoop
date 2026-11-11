#!/usr/bin/env python3
"""BUG-0933: a guest that joined before the host changed its save still shares the Space money.

BUG-0709 (0.7.11) holds the shared Space money on a guest that plays another Space save than the host's, and
the host's save was the one named in WELCOME for the whole session. A host that loads another save after the
guest joined (the campaign: the save got another name between Tribe and Civ) made that guest "in a save of its
own" for good: the host's money never reached it, its own earnings never reached the host (final check 2 of
0.7.12, campaign 20261111-090547: 1 100 000 on the host, 100 000 on two guests for 15 minutes).

  1. the usual session in Creature (fixture creature): the guest's WELCOME names the Creature save
  2. the host loads its Space save (the space fixture in another slot) while connected; the guest follows the host
     into Space by itself (the host's world is sent to it)
  3. the host earns 5000 (coop.res add money 5000): the guest's game has the host's money within 15 s
  4. the guest earns 700: the host's game money rises by 700 within 15 s
  5. the same again after the guest left and joined (its WELCOME names the Space save: passes either way)

  SPORE_FIX_OFF=res.space_save_follow coop_space_save_change.py   (steps 3 and 4 FAIL)
"""
import re
import time

import fixtures
import lane
from twoinst import Checks, start, wait_guest_in


def money(con):
    m = re.search(r'shared money=(-?[\d.]+)', con.call('coop.res').get('out', ''))
    return float(m.group(1)) if m else None


def save_line(con):
    m = re.search(r'^host save: .*$', con.call('coop.status').get('out', ''), re.M)
    return m.group(0) if m else '(no host save line)'


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        if cond():
            return True
        time.sleep(step)
    return cond()


def shared_both_ways(ck, host, guest, what):
    h0, g0 = money(host), money(guest)
    print(f'  {what}: host money {h0}, guest money {g0}')
    print(' ', host.call('coop.res add money 5000')['out'].strip().replace('\n', ' | '))
    ok = wait(lambda: money(guest) is not None and h0 is not None and abs(money(guest) - (h0 + 5000)) < 1, 15)
    ck(ok, f"{what}: the guest's game has the host's money after the host earned 5000 "
           f'(host {money(host)}, guest {money(guest)})')
    h1 = money(host)
    print(' ', guest.call('coop.res add money 700')['out'].strip().replace('\n', ' | '))
    ok = wait(lambda: money(host) is not None and h1 is not None and abs(money(host) - (h1 + 700)) < 1, 15)
    ck(ok, f"{what}: the host's game money rose by the guest's 700 ({h1} -> {money(host)}, guest {money(guest)})")
    print('  guest:', guest.call('coop.res')['out'].strip().replace('\n', ' | '))
    print('  guest:', save_line(guest))


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(5)
    print('  guest before the load:', save_line(guest))
    lane.sh('fixture', 'slot', 'space', lane.HOST_PROFILE, '9')
    slot, name = 'Game9', fixtures.planet_of('space').split('/')[1]
    print(' ', host.call(f'coop.load {slot} {name}')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 150:
        time.sleep(3)
        if 'Space' in host.call('game.mode')['out'] and money(host) is not None:
            break
    ck('Space' in host.call('game.mode')['out'] and money(host) is not None, 'the host plays its Space save')
    wait(lambda: 'Space' in guest.call('game.mode').get('out', '') and money(guest) is not None, 240, 2)
    ck('Space' in guest.call('game.mode').get('out', ''), 'the guest plays Space too')
    ck(wait_guest_in(host, guest, 240), 'the guest followed the host into its Space save')
    guest.call('wait.frames 300 60000')
    time.sleep(8)
    print('  host: ', save_line(host))
    print('  guest:', save_line(guest))
    shared_both_ways(ck, host, guest, 'after the host loaded its Space save')
    # a rejoin names the new save in WELCOME: shared with or without the fix
    print(' ', guest.call('coop.leave')['out'].strip())
    time.sleep(3)
    print(' ', guest.call(lane.JOIN)['out'].strip())
    wait(lambda: re.search(r'client id=\d+ connected', guest.call('coop.status').get('out', '')) is not None, 60, 2)
    ck(wait_guest_in(host, guest, 240), 'the guest is back in the session')
    time.sleep(8)
    shared_both_ways(ck, host, guest, 'after a rejoin')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
