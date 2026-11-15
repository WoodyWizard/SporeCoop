#!/usr/bin/env python3
"""Space co-op mechanics (spacecoop.cpp) on two instances in the same star system: a lethal hit
next to a partner only disables the ship, the partner repairs it by flying close, nobody near ->
the native death (respawn after OK on its caption); terraforming of one player reaches the partner
on the same planet; jump to the partner's star system; spice gift."""
import re
import time

from twoinst import Checks, shot, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def stat(con, key):
    m = re.search(r'space co-op:.*?\b' + key + r'=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    # per-process counters: a warm run (lab, w49-warm) starts from the values the games already have
    rescues0, gifts0, terra0 = (max(0, stat(guest, k)) for k in ('rescues', 'gifts_got', 'terra_got'))
    print('  host', sp(host, 'star'), '| guest', sp(guest, 'star'))
    ck(stat(host, 'partners_here') >= 1 and stat(guest, 'partners_here') >= 1, 'both ships are in the same place')
    print('  dist', sp(guest, 'dist'))

    # rescue: the guest is hit hard next to the host -> disabled; the host comes close -> repaired
    print('  hit:', sp(guest, 'hit', 100000))
    ck('down=1' in sp(guest, 'hp'), 'a lethal hit next to a partner only disables the ship')
    time.sleep(1.5)
    ck('disabled' in host.call('coop.status')['out'], 'the partner is told')
    print('  host goto:', sp(host, 'goto'))
    for _ in range(12):
        time.sleep(1)
        if 'down=0' in sp(guest, 'hp'):
            break
    print('  after repair:', sp(guest, 'hp'))
    ck(stat(guest, 'rescues') - rescues0 == 1, 'the partner flying close repairs the ship')
    shot(guest, 'space_play_rescued.png')

    # spice gift (the fixture has none: the host makes some of its planet's spice)
    print('  make:', sp(host, 'spice make 5'), '|', sp(host, 'spice'))
    if 'count' in sp(host, 'spice') and int(re.search(r'count (\d+)', sp(host, 'spice')).group(1)) > 0:
        g0 = sp(guest, 'spice')
        print('  give:', sp(host, 'give', 2), '| host now', sp(host, 'spice'))
        time.sleep(2)
        print('  guest', g0, '->', sp(guest, 'spice'))
        ck(stat(guest, 'gifts_got') - gifts0 == 1, 'spice reaches the partner')
    else:
        ck(False, 'the host has spice to give')

    # jump: the guest flies to another star, then jumps back to the host
    star = int(re.search(r'star (\d+)', sp(host, 'star')).group(1))
    ck('already' in sp(guest, 'jump'), 'jump refuses when already in the partner system')
    print('  guest away:', sp(guest, 'jumpto', star + 1))
    time.sleep(15)
    print('  guest now', sp(guest, 'star'))
    away = int(re.search(r'star (\d+)', sp(guest, 'star')).group(1))
    ck(away and away != star, f'the guest travelled to another star ({away})')
    print('  jump back:', sp(guest, 'jump'))
    time.sleep(15)
    back = int(re.search(r'star (\d+)', sp(guest, 'star')).group(1))
    ck(back == star, f'jump to partner brings the guest to the host star ({back} == {star})')
    shot(guest, 'space_play_jump.png')

    # terraforming on the same planet: both land on the host's planet
    print('  host land:', sp(host, 'land'))
    time.sleep(20)
    print('  host', sp(host, 'star'))
    print('  guest jump:', sp(guest, 'jump'))
    time.sleep(15)
    print('  guest land:', sp(guest, 'land'))
    time.sleep(20)
    hp_, gp_ = sp(host, 'star'), sp(guest, 'star')
    print('  host', hp_, '| guest', gp_)
    if hp_.split()[1].startswith('4') and hp_.split()[1] == gp_.split()[1]:
        g0 = sp(guest, 'planet')
        print('  host terra:', sp(host, 'terra', 0.05, 0.05, 0.05), '| guest before', g0)
        time.sleep(3)
        print('  guest after', sp(guest, 'planet'))
        ck(stat(guest, 'terra_got') > terra0, 'terraforming reaches the partner on the same planet')
        shot(guest, 'space_play_planet.png')
    else:
        ck(False, 'both players landed on the same planet')
    # nobody near: the native death after 20 s
    print('  guest away again:', sp(guest, 'jumpto', star + 1))
    time.sleep(15)
    print('  hit alone:', sp(guest, 'hit', 100000))
    ck('down=0' in sp(guest, 'hp'), 'alone the ship is not disabled (the native death)')
    # the native death caption waits for the player's OK (Enter / check button), then the respawn at a
    # colony (coop_space_death.py checks this path in detail)
    time.sleep(3)
    for _ in range(15):
        if 'state=0' in sp(guest, 'hp'):
            break
        guest.call('key enter')
        time.sleep(2)
    print('  guest after the death', sp(guest, 'hp'), sp(guest, 'star'))
    ck('state=0' in sp(guest, 'hp'), 'after OK on the death caption the ship is respawned')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
