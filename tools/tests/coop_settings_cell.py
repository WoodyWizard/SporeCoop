#!/usr/bin/env python3
"""The Cell session settings (BUG-0768, F3 step 3 of 0.7.12): cell.knockout and cell.respawn_small, on (the default,
0.7.11's behaviour) and off, checked in the game's state of both games (the switch vote: coop_switch_vote.py
--vote-off).

Cell fixture, two players (cold: the settings are lane knobs); the guest swims 30 m away (no partner can revive it):
  1. on: a lethal bite from the host's side (`coop.pool hurt 1 50`) knocks the guest out (its game holds it at hp 1,
     downed=1) and the host sees it knocked out (ko[1]=1); nobody revives it: after 10 s it respawns small next to the
     partner (its game: not dying, not dead, a respawn counted; the host no longer shows it knocked out);
  2. cell.respawn_small off: the next lethal bite knocks it out again, after 10 s the game's own death runs (its game:
     the avatar dying or dead, ko_deaths counted), the host does not show it knocked out;
  3. cell.knockout off (once the guest's game respawned it): a lethal bite next to the partner kills the guest at once
     (the game's dying / dead flag, never downed) and the host never shows it knocked out.
SPORE_FIX_OFF=settings.all (every step-3 setting reads its default): steps 2 and 3 must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_settings_cell.py
"""
import re
import time

import settings_lib as sl
import twoinst
from twoinst import Checks, start

KEYS = ('cell.knockout', 'cell.respawn_small')


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def pool(con):
    return con.call('coop.pool')['out']


def cell_status(con):
    return con.call('coop.status')['out']


def host_sees_ko(host):
    return bool(re.search(r'^ko\[1\]=1', pool(host), re.MULTILINE))


def dying(out):
    return num(out, 'dying') == 1 or num(out, 'dead') == 1


def alive_again(guest):
    out = pool(guest)
    return num(out, 'downed') == 0 and not dying(out) and (num(out, 'hp') or 0) > 0


def main():
    twoinst.forget_settings()
    host, guest = start('cell')
    ck = Checks()
    try:
        ck(sl.wait(lambda: (num(pool(guest), 'mirrors') or 0) > 5, 30), 'the guest mirrors the host pool')
        # 1. on: knocked out, then respawned small
        guest.call('coop.pool tp 30 0')
        time.sleep(1.5)
        r0 = num(cell_status(guest), 'respawns') or 0
        host.call('coop.pool hurt 1 50')
        ck(sl.wait(lambda: num(pool(guest), 'downed') == 1, 5), 'knock-out on: the guest is knocked out')
        ck(sl.wait(lambda: host_sees_ko(host), 3), 'knock-out on: the host sees the guest knocked out')
        ck(sl.wait(lambda: (num(cell_status(guest), 'respawns') or 0) > r0, 16), 'respawn on: the guest respawned after 10 s')
        out = pool(guest)
        ck(num(out, 'downed') == 0 and not dying(out), f'respawn on: the guest plays on (not dying, not dead): {out.splitlines()[:1]}')
        ck(sl.wait(lambda: not host_sees_ko(host), 3), 'respawn on: the host no longer shows it knocked out')
        time.sleep(5)  # the 3 s of invulnerability after the respawn
        # 2. respawn off: the game's death after the 10 s
        ck(sl.force(host, [guest], 'cell.respawn_small', 'off'), 'the running guest got cell.respawn_small off')
        guest.call('coop.pool tp 30 0')
        time.sleep(1.5)
        d0 = num(cell_status(guest), 'ko_deaths') or 0
        host.call('coop.pool hurt 1 50')
        ck(sl.wait(lambda: num(pool(guest), 'downed') == 1, 5), 'respawn off: the guest is knocked out first')
        ok = sl.wait(lambda: dying(pool(guest)), 16, 0.25)
        print('  guest after 10 s:', [ln for ln in pool(guest).splitlines() if 'downed=' in ln][:1], flush=True)
        ck(ok and (num(cell_status(guest), 'ko_deaths') or 0) == d0 + 1,
           'respawn off: nobody revived it: the game\'s own death runs on the guest')
        ck(not host_sees_ko(host), 'respawn off: the host does not show it knocked out')
        ck(sl.wait(lambda: alive_again(guest), 90, 1), "the guest's game respawned its cell (the game's way)")
        time.sleep(5)
        # 3. knock-out off: the hit kills at once
        ck(sl.force(host, [guest], 'cell.knockout', 'off'), 'the running guest got cell.knockout off')
        guest.call('coop.pool tp 3 0')
        time.sleep(1.5)
        host.call('coop.pool hurt 1 50')
        seen_down = seen_ko = False
        t0 = time.time()
        died = False
        while time.time() - t0 < 6:
            out = pool(guest)
            seen_down |= num(out, 'downed') == 1
            seen_ko |= host_sees_ko(host)
            died |= dying(out)
            if died:
                break
            time.sleep(0.25)
        ck(died and not seen_down, f'knock-out off: the lethal bite kills the guest at once (died {died}, downed {seen_down})')
        ck(not seen_ko, 'knock-out off: the host never shows it knocked out')
        sl.wait(lambda: alive_again(guest), 90, 1)
    finally:
        sl.reset([host], KEYS)
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
