#!/usr/bin/env python3
"""The session settings of the Creature stage and of every stage (BUG-0768, F3 step 3 of 0.7.12): all.name_tags,
all.friendly_fire and all.late_join, on and off, checked in each game (the editor at once: coop_editor_now.py --off;
the player limit: tests/net_tests, it needs a third game).

Creature fixture, two players (cold: the settings are lane knobs):
  1. name tags (default on): the guest's creature next to the host's (`coop.team come`), the host on the guest's
     screen: the guest's overlay draws the host's name (coop.status name_tags=1); the host switches all.name_tags off
     in play (a live setting): the guest draws no name tag;
  2. friendly fire (default off): the guest's creature hits the host's (`coop.team pvp 0 <dmg>`: the game's TakeDamage
     on the host's puppet in the guest's game) and the host hits the guest's: neither creature loses health in its
     own game; the host switches all.friendly_fire on: the same hits take the health of the other player's creature in
     that player's game (the game's health, coop.hp);
  3. joining after the start (default on): the host switches all.late_join off; the guest leaves and joins as a new
     player (another name): refused ("does not take new players"), the host still alone; the guest joins with its
     old name (a player who was in the session): admitted; all.late_join on: a new name joins.
SPORE_FIX_OFF=settings.all (every step-3 setting reads its default): the off / on checks must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_settings_creature.py
"""
import re
import time

import lane
import settings_lib as sl
import twoinst
from twoinst import Checks, start, wait_guest_in

KEYS = ('all.name_tags', 'all.friendly_fire', 'all.late_join')
HIT = 0.3  # of the victim's max health


def tags(con):
    m = re.search(r'name_tags=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else None


def own_hp(con):
    m = re.search(r'^own: hp=([\d.]+)/([\d.]+)', con.call('coop.hp')['out'], re.M)
    return (float(m.group(1)), float(m.group(2))) if m else (None, None)


def pvp(attacker, victim, vid):
    """attacker's creature hits victim's: the health victim's own game lost"""
    hp, mx = own_hp(victim)
    if not mx:
        return None
    victim.call('coop.team hp %.2f' % mx)
    time.sleep(0.5)
    print('  ' + attacker.call('coop.team pvp %d %.2f' % (vid, HIT * mx))['out'].strip(), flush=True)
    low = mx
    t0 = time.time()
    while time.time() - t0 < 3:  # (the regeneration next to the partner gives some back)
        h, _ = own_hp(victim)
        low = min(low, h if h is not None else mx)
        time.sleep(0.2)
    victim.call('coop.team hp %.2f' % mx)
    return mx - low


def connected(guest):
    return 'connected' in (guest.call('coop.status')['out'].splitlines() or [''])[0]


def join_as(guest, name):
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN.rsplit(' ', 1)[0] + ' ' + name)


def main():
    twoinst.forget_settings()
    host, guest = start('creature')
    ck = Checks()
    try:
        time.sleep(3)
        # 1. name tags
        print('  guest:', guest.call('coop.team come')['out'].strip(), flush=True)
        time.sleep(2)
        n_on = sl.wait(lambda: tags(guest), 8)
        ck(n_on, f'name tags on: the guest draws the host\'s name ({tags(guest)})')
        ck(sl.force(host, [guest], 'all.name_tags', 'off'), 'the running guest got all.name_tags off')
        time.sleep(1)
        n_off = []
        for _ in range(6):
            n_off.append(tags(guest))
            time.sleep(0.3)
        ck(n_off and all(n == 0 for n in n_off), f'name tags off: the guest draws no name tag ({n_off})')
        sl.force(host, [guest], 'all.name_tags', 'on')
        # 2. friendly fire
        d1, d2 = pvp(guest, host, 0), pvp(host, guest, 1)
        print(f'  friendly fire off: the host lost {d1}, the guest {d2}', flush=True)
        ck(d1 is not None and d1 < 0.05 and d2 is not None and d2 < 0.05,
           f'friendly fire off: no player hurts the other ({d1}, {d2})')
        ck(sl.force(host, [guest], 'all.friendly_fire', 'on'), 'the running guest got all.friendly_fire on')
        _, mh = own_hp(host)
        _, mg = own_hp(guest)
        d1, d2 = pvp(guest, host, 0), pvp(host, guest, 1)
        print(f'  friendly fire on: the host lost {d1} of {mh}, the guest {d2} of {mg}', flush=True)
        ck(d1 and mh and d1 > 0.15 * mh, f"friendly fire on: the guest's hit hurts the host's creature in its game ({d1})")
        ck(d2 and mg and d2 > 0.15 * mg, f"friendly fire on: the host's hit hurts the guest's creature in its game ({d2})")
        print(' ', [ln for ln in host.call('coop.status')['out'].splitlines() if 'friendly fire' in ln][:1], flush=True)
        sl.force(host, [guest], 'all.friendly_fire', 'off')
        # 3. joining after the start
        ck(sl.force(host, [guest], 'all.late_join', 'off'), 'the running guest got all.late_join off')
        join_as(guest, 'Carol')
        told = sl.wait(lambda: 'does not take new players' in guest.call('coop.status')['out'], 10)  # (its toast)
        time.sleep(4)
        hs = host.call('coop.status')['out']
        print('  guest:', (guest.call('coop.status')['out'].splitlines() or [''])[0], flush=True)
        ck(not connected(guest) and 'players=1/' in hs, 'late join off: a new player is refused after the start')
        ck(told, 'late join off: the refused player is told why')
        join_as(guest, 'Bob')
        ck(wait_guest_in(host, guest, 200), 'late join off: a player who was in the session comes back')
        ck(sl.force(host, [guest], 'all.late_join', 'on'), 'all.late_join on again')
        join_as(guest, 'Carol')
        ck(wait_guest_in(host, guest, 200), 'late join on: a new player joins while the host plays')
        join_as(guest, 'Bob')  # (the lane's guest name for the next tests)
        wait_guest_in(host, guest, 200)
    finally:
        sl.reset([host], KEYS)
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
