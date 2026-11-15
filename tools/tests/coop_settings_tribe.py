#!/usr/bin/env python3
"""The Tribe session settings (BUG-0768, F3 step 3 of 0.7.12): tribe.pincer, tribe.chief_aura and tribe.epic, each on
(the default, 0.7.11's behaviour) and off, checked in the game's state (the duet: coop_tribe_duet.py --duet-off).

Tribe fixture, two players (cold: the settings are lane knobs):
  1. on: a member of the host's and one of the guest's hit one enemy tribe (`coop.tribe pincer`, the game's citizen
     damage): the second player's blow takes x1.5 of the victim's health in the host's game; the epic creature comes
     (`coop.tribe epic`) and the guest is told;
  2. the host switches tribe.pincer, tribe.chief_aura and tribe.epic off in play (`coop.settings force`): the guest gets
     the values; the same two blows on another enemy member take the base damage each (no pincer, no aura); the epic
     no longer comes: the host's `coop.tribe epic` is refused and the guest is told nothing.
SPORE_FIX_OFF=settings.all (every step-3 setting reads its default): step 2 must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_settings_tribe.py
"""
import re
import time

import settings_lib as sl
import twoinst
from coop_tribe import members
from twoinst import Checks, start

KEYS = ('tribe.pincer', 'tribe.chief_aura', 'tribe.epic')


def blows(host, victim, a, b, dmg=2):
    """the victim's health taken by a blow of a then one of b (the host's game)"""
    r1 = host.call(f'coop.tribe pincer {victim} {a} {dmg}')['out']
    r2 = host.call(f'coop.tribe pincer {victim} {b} {dmg}')['out']
    print(f'  hits on #{victim}: {r1.strip()} | {r2.strip()}', flush=True)
    return [float(x) - float(y) for x, y in re.findall(r'hp ([\d.]+) -> ([\d.]+)', r1 + ' ' + r2)]


def main():
    twoinst.forget_settings()
    host, guest = start('tribe')
    ck = Checks()
    try:
        t0 = time.time()
        while True:  # the split of the player tribe arrives with the first states
            hm = members(host)
            if any(v[0] == 1 for v in hm.values()) or time.time() - t0 > 90:
                break
            time.sleep(3)
        time.sleep(3)
        host_m = [i for i, v in hm.items() if v[0] == 0 and v[3] == 'player']
        guest_m = [i for i, v in hm.items() if v[0] == 1 and v[3] == 'player']
        enemy = [i for i, v in hm.items() if v[3] != 'player']
        print('  host members', host_m, 'guest members', guest_m, 'others', enemy[:6], flush=True)
        if not ck(len(enemy) >= 2 and host_m and guest_m, 'members of both players and two enemy members'):
            return ck.result()
        # 1. on (the defaults)
        ck(all(sl.get(guest, k) == 1 for k in KEYS), 'the guest plays with the defaults (all on)')
        d = blows(host, enemy[0], host_m[-1], guest_m[-1])
        ck(len(d) == 2 and d[1] >= 2 * 1.5 - 0.01, f'pincer on: the second player\'s blow takes x1.5 ({d})')
        print(' ', host.call('coop.tribe epic')['out'].strip()[:160], flush=True)
        ck(sl.wait(lambda: 'epic creature comes' in guest.call('coop.status')['out'], 10),
           'epic on: the epic creature comes, the guest is told')
        # 2. off
        for k in KEYS:
            ck(sl.force(host, [guest], k, 'off'), f'the running guest got {k} off')
        time.sleep(11)  # the pincer window (10 s) of the first tribe is over
        d = blows(host, enemy[1], host_m[-1], guest_m[-1])
        ck(len(d) == 2 and abs(d[0] - 2) < 0.01 and abs(d[1] - 2) < 0.01,
           f'pincer and aura off: each blow takes the base damage ({d})')
        for _ in range(40):  # the epic of step 1 goes (killed by the host's member) before the next is asked for
            r = host.call(f'coop.tribe epic hit {host_m[-1]} 60')['out']
            if 'no epic' in r or 'hp 0.00' in r:
                break
            time.sleep(0.2)
        time.sleep(3)
        g0 = guest.call('coop.status')['out'].count('epic creature comes')
        r = host.call('coop.tribe epic')['out'].strip()
        print('  host:', r[:160], flush=True)
        time.sleep(4)
        g1 = guest.call('coop.status')['out'].count('epic creature comes')
        ck('tribe.epic is off' in r and g1 == g0, f'epic off: no epic comes, the guest is told nothing ({r[:60]}; {g0} -> {g1})')
    finally:
        sl.reset([host], KEYS)
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
