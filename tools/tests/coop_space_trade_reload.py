#!/usr/bin/env python3
"""BUG-0225 (GitHub #159): a trade refund that comes back after a galaxy reload is paid only into
the game it was taken from, and only when that game's file lacks it.

The host has an Antimatter Bomb with 12 charges and gives 5 to the guest, which does not answer
(`trade hold 1`: the answer is still on its way). The host then goes to the galaxy menu and loads
the same game again; the guest leaves, so the 5 charges come back to the host in the reloaded game.

  default round: the host saved BEFORE the gift (the file has 12). The reloaded game has 12 again:
                 the refund must be dropped, the bomb keeps 12. 0.7.3.0 paid it at once into the
                 reloaded game (17 charges: doubled).
  --saved:       the host saved AFTER the gift (the file has 7). The reloaded game has 7: the refund
                 must be paid once (12). Both versions pass (regression of BUG-0058's rule).

The checks read the game's own tool ammo (cSpaceToolData) on the host.

  coop_space_trade_reload.py [--saved]   (SPORE_FIX_OFF=space.owed_id: the 0.7.3.0 rule, the
                                          default round FAILS)"""
import re
import sys
import time

import fixtures
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


def noted(host):
    m = re.search(r'saves_noted=(\d+)', sp(host, 'world'))
    return int(m.group(1)) if m else -1


def save(host, ck, what):
    n0 = noted(host)
    print(f'  save ({what}):', host.call('coop.save')['out'].strip())
    time.sleep(4)
    for _ in range(3):  # the "game saved" box stops the host's frames
        host.call('key enter')
        time.sleep(1)
    ck(wait(lambda: noted(host) > n0, 20), f'the host saved this Space world ({what}): {sp(host, "world")}')


def main():
    saved_after = '--saved' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    print('  host bomb:', sp(host, 'tool add', '0x' + BOMB), '|', sp(host, 'trade ammo', BOMB, 12))
    ck(ammo(host) == 12, f'the host has 12 Antimatter Bomb charges ({ammo(host)})')
    if not saved_after:
        save(host, ck, 'before the gift: the file has 12')
    print(' ', sp(guest, 'trade hold 1'))
    print('  give:', sp(host, 'trade give', BOMB, 5))
    ck(wait(lambda: ammo(host) == 7, 5), f'the 5 charges left the host\'s hold: 7 ({ammo(host)})')
    ck('pending=1' in sp(host, 'trade'), 'the gift is on its way (no answer yet)')
    if saved_after:
        save(host, ck, 'after the gift: the file has 7')

    planet = fixtures.planet_of('space')
    print('  menu:', host.call('coop.menu')['out'].strip())
    ck(wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 90, 1), 'the host is in the galaxy menu')
    host.call('wait.frames 240 90000')
    print('  load:', host.call(f'coop.load {" ".join(planet.split("/"))}')['out'].strip())
    ck(wait(lambda: 'Space' in host.call('game.mode')['out'], 180, 2), 'the host is in Space again')
    time.sleep(5)
    want_loaded = 7 if saved_after else 12
    a = wait(lambda: ammo(host) is not None and ammo(host), 20)
    ck(ammo(host) == want_loaded, f'the reloaded game has the file\'s {want_loaded} charges ({ammo(host)})')
    ck('pending=1' in sp(host, 'trade'), f'the gift is still on its way ({sp(host, "trade")})')

    print('  guest leaves:', guest.call('coop.leave')['out'].strip())
    ck(wait(lambda: 'pending=0' in sp(host, 'trade'), 30), 'the host saw the partner leave with the gift open')
    time.sleep(4)
    st = sp(host, 'trade')
    print('  host trade:', st)
    if saved_after:
        ck(wait(lambda: ammo(host) == 12, 10), f'saved after the gift: the 5 came back once: 12 ({ammo(host)})')
    else:
        ck(ammo(host) == 12, f'saved before the gift: the reloaded game still has them, nothing paid: 12 ({ammo(host)})')
    time.sleep(3)
    ck(ammo(host) == 12, f'and it stays 12 ({ammo(host)})')

    sp(guest, 'trade hold 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
