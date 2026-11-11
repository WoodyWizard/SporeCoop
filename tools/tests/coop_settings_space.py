#!/usr/bin/env python3
"""The Space session settings (BUG-0768, F3 step 3 of 0.7.12): the features of the F9 Space page (space.trade,
space.rivals, ...) and space.disable, each on (the default, 0.7.11's behaviour) and off, checked in each game.

Space fixture, two players (cold: the settings are lane knobs), both land on the home planet:
  1. features: on, the guest's feature list has trade and rivalry running and the host's Space page has its
     "Switch to rivals" button; the host switches space.trade and space.rivals off in play: the guest's list says
     "off (the session setting)" for both, the host's page has no rivals switch; on again: back;
  2. space.disable on: a lethal hit on the guest's ship (`coop.space hit`, the game's TakeDamage) next to the host
     disables it (down=1, state 0) and the host is told (partners_down=1); the host repairs it by flying close;
  3. after the rescue's 60 s cooldown the host switches space.disable off: the same lethal hit destroys the guest's
     ship (state 2) as in a solo game, the host is told of no disabled partner.
SPORE_FIX_OFF=settings.all (every step-3 setting reads its default): the off checks must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_settings_space.py
"""
import re
import time

import fixtures
import lane
import settings_lib as sl
import twoinst
from play_space import Player
from twoinst import Checks, start

KEYS = ('space.trade', 'space.rivals', 'space.disable')


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def field(text, key):
    m = re.search(r'\b' + key + (r' ' if key == 'hp' else r'=') + r'([\d.]+)', text)
    return float(m.group(1)) if m else -1


def feature(con, name):
    for ln in sp(con, 'features').splitlines():
        if ln.startswith(name + ':'):
            return ln
    return ''


def lethal(guest):
    hp = field(sp(guest, 'hp'), 'hp')
    r = sp(guest, 'hit', f'{hp + 5:.1f}')
    print('  guest hit:', r, flush=True)
    time.sleep(1.5)
    return sp(guest, 'hp')


def main():
    twoinst.forget_settings()
    host, guest = start('space')
    ck = Checks()
    try:
        # 1. the Space features
        ck('off' not in feature(guest, 'trade') and 'off' not in feature(guest, 'rivalry'),
           f'features on: trade and rivalry run on the guest ({feature(guest, "trade")!r}, {feature(guest, "rivalry")!r})')
        stats = {}
        ph = Player('host', lane.HOST, 0, 1, time.time() + 1200, stats)
        pg = Player('guest', lane.GUEST, 1, 2, time.time() + 1200, stats)
        ph.partner, pg.partner = pg, ph
        home = fixtures.FIXTURES['space']['home']
        for p in (ph, pg):
            t1 = time.time()
            while p.ctx() == 'system' and time.time() - t1 < 150:
                p.land(home)
        ck(ph.place()[0] == pg.place()[0] and ph.ctx() == 'planet', f'both on the home planet ({sp(guest, "dist")})')
        ck('Switch to rivals' in sp(host, 'rows'), 'features on: the host\'s Space page has the rivals switch')
        for k in ('space.trade', 'space.rivals'):
            ck(sl.force(host, [guest], k, 'off'), f'the running guest got {k} off')
        time.sleep(1)
        ft, fr = feature(guest, 'trade'), feature(guest, 'rivalry')
        ck('off (the session setting)' in ft and 'off (the session setting)' in fr,
           f'features off: trade and rivalry are off on the guest ({ft!r}, {fr!r})')
        ck('Switch to rivals' not in sp(host, 'rows'), 'features off: the host\'s Space page has no rivals switch')
        for k in ('space.trade', 'space.rivals'):
            sl.force(host, [guest], k, 'on')
        ck('Switch to rivals' in sp(host, 'rows') and 'off' not in feature(guest, 'trade'), 'features on again')
        # 2. disabled, not destroyed (the default)
        st = lethal(guest)
        ck(field(st, 'down') == 1 and field(st, 'state') == 0, f'space.disable on: the lethal hit disabled the guest ({st})')
        ck(sl.wait(lambda: 'partners_down=1' in host.call('coop.status')['out'], 5), 'space.disable on: the host is told')
        r0 = field(sp(guest), 'rescues')
        ph.rescue()
        sl.wait(lambda: field(sp(guest), 'rescues') > r0, 20, 1)
        ck(field(sp(guest), 'rescues') == r0 + 1, f'the host repaired the guest ({sp(guest, "hp")})')
        time.sleep(62)  # the rescue's cooldown
        # 3. off: destroyed as in a solo game
        ck(sl.force(host, [guest], 'space.disable', 'off'), 'the running guest got space.disable off')
        st = lethal(guest)
        told = 'partners_down=1' in host.call('coop.status')['out']
        ck(field(st, 'down') == 0 and field(st, 'state') == 2,
           f'space.disable off: the lethal hit destroyed the guest\'s ship ({st})')
        ck(not told, 'space.disable off: the host is told of no disabled partner')
        t1 = time.time()
        while time.time() - t1 < 60 and (field(sp(guest, 'hp'), 'state') == 2 or field(sp(guest, 'hp'), 'movie') == 1):
            pg.captions()  # the game's death captions: the ship respawns at home
    finally:
        sl.reset([host], KEYS)
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
