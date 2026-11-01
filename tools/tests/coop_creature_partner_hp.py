#!/usr/bin/env python3
"""Each player's game shows the partner's real health on the partner's creature (BUG-0373).

Player report (0.7.3.3, host mark): "in creature stage we dont see each others HP correctly (no dmg
being dealt to other player on my screen)". The puppet of a partner was set to its own full health
every frame, so its health bar never moved.

Both directions, read in the observer's game (the puppet's health field and GetMaxHitPoints, what
the health bar draws), compared with the hurt player's own game:
1. The guest takes a real hit in its own game (`coop.team hit`, the game's TakeDamage): within 3 s
   the host's puppet of the guest shows the guest's health and max health.
2. The same for the host and the guest's puppet of the host.

  coop_creature_partner_hp.py   (SPORE_FIX_OFF=creature.partner_hp must FAIL)"""
import re
import time

from twoinst import Checks, start


def own_hp(con):
    m = re.search(r'^own: hp=([\d.]+)/([\d.]+)', con.call('coop.hp')['out'], re.M)
    return (float(m.group(1)), float(m.group(2))) if m else (None, None)


def puppet_hp(con, pid):
    m = re.search(rf'^puppet {pid}: hp=([\d.]+)/([\d.]+)', con.call('coop.hp')['out'], re.M)
    return (float(m.group(1)), float(m.group(2))) if m else (None, None)


def hurt_and_watch(ck, victim, observer, vid, who):
    hp0, mx = own_hp(victim)
    ck(hp0 is not None and mx, f'{who}: own health {hp0}/{mx}')
    if not mx:
        return
    victim.call('coop.team hp %.2f' % mx)  # full first
    time.sleep(0.5)
    victim.call('coop.team hit %.2f' % (0.45 * mx))  # the game's TakeDamage on its own avatar
    time.sleep(0.3)
    hp1, mx1 = own_hp(victim)
    ck(hp1 is not None and hp1 < 0.7 * mx, f'{who}: its own game took the hit ({hp1}/{mx1})')
    best = None
    tol = max(0.6, 0.08 * mx)  # the regen next to the partner moves it between the two reads
    t0 = time.time()
    while time.time() - t0 < 3:
        own, omx = own_hp(victim)
        php, pmx = puppet_hp(observer, vid)
        if php is not None and own is not None:
            best = (php, pmx, own, omx)
            if abs(php - own) <= tol and pmx is not None and abs(pmx - omx) <= 0.05:
                break
        time.sleep(0.2)
    print(f'  {who}: own {best and best[2]}/{best and best[3]}, the other game\'s puppet {best and best[0]}/{best and best[1]}', flush=True)
    ck(best is not None and abs(best[0] - best[2]) <= tol,
       f'{who}: the other game\'s puppet shows its health ({best and best[0]} / own {best and best[2]})')
    ck(best is not None and best[1] is not None and abs(best[1] - best[3]) <= 0.05,
       f'{who}: the other game\'s puppet has its max health ({best and best[1]} / own {best and best[3]})')
    ck(best is not None and best[0] < 0.8 * best[1], f'{who}: the other game shows it hurt, not at full health')
    victim.call('coop.team hp %.2f' % mx)


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    print('  host:', host.call('coop.hp')['out'].strip().replace('\n', ' | '))
    print('  guest:', guest.call('coop.hp')['out'].strip().replace('\n', ' | '))
    hurt_and_watch(ck, guest, host, 1, 'guest')
    hurt_and_watch(ck, host, guest, 0, 'host')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
