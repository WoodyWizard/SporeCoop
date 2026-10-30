#!/usr/bin/env python3
"""BUG-0225 (GitHub #159): a trade refund that comes back after a galaxy reload is paid only into
the game it was taken from, and only when that game's file lacks it.

The host has an Antimatter Bomb with 12 charges and gives 5 to the guest, which does not answer
(`trade hold 1`: the answer is still on its way). The host leaves for the galaxy menu (the game's
quit saves the game: its file now has 7) and loads a game; then the guest leaves, so the 5 charges
come back to the host while another Space world is in memory.

  default round: the host loads ANOTHER game (game B: a copy of the fixture's slot made at the start,
                 without the bomb). The refund must be kept owed, not paid into game B (no bomb
                 there). Back in game A (loaded again: 7 charges) it is paid once: 12.
                 0.7.3.0 paid it into game B (a new bomb with 5 charges there) and game A kept 7:
                 a transfer between games.
  --same-game:   the host loads game A again (its file was saved after the gift: 7). The refund is
                 paid once: 12. Both versions pass (BUG-0058's rule, regression).

The checks read the game's own tool ammo (cSpaceToolData) on the host.

  coop_space_trade_reload.py [--same-game]   (SPORE_FIX_OFF=space.owed_id: the 0.7.3.0 rule, the
                                              default round FAILS)"""
import os
import re
import shutil
import sys
import time

import fixtures
import lane
from twoinst import Checks, start

BOMB = '144DF50A'  # Antimatter Bomb


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def ammo(con, inst=BOMB):
    m = re.search(r'tool %s \S+ \S+ \'[^\']*\' ammo=(-?\d+)' % inst, sp(con, 'trade all'))
    return int(m.group(1)) if m else None


def wait(pred, timeout=20, step=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def load(host, ck, slot, planet, what):
    print('  menu:', host.call('coop.menu')['out'].strip())
    ck(wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 90, 1), f'the host is in the galaxy menu ({what})')
    host.call('wait.frames 240 90000')
    print('  load:', host.call(f'coop.load {slot} {planet}')['out'].strip())
    ck(wait(lambda: 'Space' in host.call('game.mode')['out'], 180, 2), f'the host is in Space: {what}')
    time.sleep(6)
    print('  world:', sp(host, 'world'))


def main():
    same = '--same-game' in sys.argv
    planet = fixtures.planet_of('space')  # Game0/Madas
    slot_a, name = planet.split('/')
    games = os.path.join(lane.HOST_DIR, 'Games')
    made = []
    try:
        return run(same, slot_a, name, games, made)
    finally:
        for d in made:
            shutil.rmtree(d, ignore_errors=True)


def run(same, slot_a, name, games, made):
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    slot_b = None
    if not same:  # game B: a copy of the fixture's slot as loaded (no bomb), never the slot being played
        slot_b = next(f'Game{n}' for n in range(7, 30) if not os.path.exists(os.path.join(games, f'Game{n}')))
        shutil.copytree(os.path.join(games, slot_a), os.path.join(games, slot_b))
        made.append(os.path.join(games, slot_b))
        print(f'  game B: {slot_b}/{name} (a copy of {slot_a})')
    print('  host bomb:', sp(host, 'tool add', '0x' + BOMB), '|', sp(host, 'trade ammo', BOMB, 12))
    ck(ammo(host) == 12, f'the host has 12 Antimatter Bomb charges ({ammo(host)})')
    print(' ', sp(guest, 'trade hold 1'))
    print('  give:', sp(host, 'trade give', BOMB, 5))
    ck(wait(lambda: ammo(host) == 7, 5), f'the 5 charges left the host\'s hold: 7 ({ammo(host)})')
    ck('pending=1' in sp(host, 'trade'), 'the gift is on its way (no answer yet)')

    if same:
        load(host, ck, slot_a, name, 'game A again')
        ck(ammo(host) == 7, f'game A was saved by the quit after the gift: 7 ({ammo(host)})')
    else:
        load(host, ck, slot_b, name, 'game B')
        ck(ammo(host) is None, f'game B has no bomb ({ammo(host)})')
    ck('pending=1' in sp(host, 'trade'), f'the gift is still on its way ({sp(host, "trade")})')

    print('  guest leaves:', guest.call('coop.leave')['out'].strip())
    ck(wait(lambda: 'pending=0' in sp(host, 'trade'), 30), 'the host saw the partner leave with the gift open')
    time.sleep(5)
    print('  host trade:', sp(host, 'trade'))
    if same:
        ck(wait(lambda: ammo(host) == 12, 10), f'the 5 came back once into game A: 12 ({ammo(host)})')
    else:
        ck(ammo(host) is None, f'nothing was paid into game B: no bomb there ({ammo(host)})')
        load(host, ck, slot_a, name, 'game A again')
        ck(wait(lambda: ammo(host) == 12, 15), f'back in game A the 5 came back: 12 ({ammo(host)})')
    time.sleep(3)
    ck(ammo(host) == 12, f'and only once: 12 ({ammo(host)})')
    print('  host trade:', sp(host, 'trade'))

    sp(guest, 'trade hold 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
