#!/usr/bin/env python3
"""Space: a guest's own cargo hold survives its rejoin, and the host's never shows up on it (BUG-0678).

Player report (Discord 2026-11-07, Space): "When players log back in, their items disappear and the
host's items appear". Every player flies in a copy of the host's save; a guest that leaves and joins
again loads the host's share save, whose player inventory (cSimulatorSpaceGame +0x40) is the host's.
The host keeps each guest's own hold (space_trade.cpp, K_INV) and gives it back when it rejoins.

1. Both play the `space` fixture. The two holds are made different: the guest gets spice A and an Antimatter Bomb
   with 9 charges and the interstellar drive; the host gets spice B and the bomb with 3 charges.
2. The guest leaves, joins again and loads the host's world.
3. In the rejoined guest's game (`coop.space trade inv`: the game's cPlayerInventory items) the hold is the
   one it had before it left: its spice A, its bomb's charges, its drive, no spice B of the host.
4. The host's game still has its own hold (spice B, its charges, no spice A).
PASS: 1 to 4. SPORE_FIX_OFF=space.keep_items: the rejoined guest has the host's hold (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_items_rejoin.py
"""
import re
import time

import lane
from coop_space_map import no_movie, sp, wait
from twoinst import Checks, start, wait_guest_in

BOMB = '144DF50A'  # Antimatter Bomb (a tool with charges)


def inv(con):
    """the game's hold: {(type, key0): (count, ammo)}, slots"""
    items, slots = {}, -1
    for line in sp(con, 'trade inv').splitlines():
        m = re.match(r'slots (-?\d+)', line)
        if m:
            slots = int(m.group(1))
        m = re.match(r'item type=(\d+) key=([0-9A-F]+) \S+ \S+ count=(-?\d+) ammo=(-?\d+)', line)
        if m:
            items[(int(m.group(1)), m.group(2))] = (int(m.group(3)), int(m.group(4)))
    return items, slots


def spice_keys(con):
    return [m.group(1) for m in re.finditer(r'^spice \d+ ([0-9A-F]+)', sp(con, 'trade spices'), re.M)]


def epoch(con):
    m = re.search(r'world=(\d+) open=1', sp(con, 'world'))
    return int(m.group(1)) if m else -1


def brief(items, keys):
    return {k: items.get(k) for k in keys}


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    no_movie(host)
    g0, gslots = inv(guest)
    h0, hslots = inv(host)
    print(f'  start: host {len(h0)} items {hslots} slots, guest {len(g0)} items {gslots} slots', flush=True)
    print('  guest hold: ' + sp(guest, 'trade inv').replace('\n', ' | '), flush=True)
    print('  guest tools: ' + sp(guest, 'trade all').replace('\n', ' | '), flush=True)
    keys = spice_keys(guest)
    ck(len(keys) >= 2, f'the game knows at least two spices ({len(keys)})')
    tool = BOMB  # both get it, with other charges
    print('  guest drive (the host has none): ' + sp(guest, 'tool add interstellardrive'), flush=True)
    print('  bombs: guest ' + sp(guest, 'tool add', '0x' + BOMB) + ', host ' + sp(host, 'tool add', '0x' + BOMB), flush=True)
    if len(keys) < 2:
        return ck.result()
    sa, sb = (4, keys[0]), (4, keys[1])
    # 1. different holds
    print('  guest: ' + sp(guest, 'trade spice 0 7') + ' ' + sp(guest, f'trade ammo {tool} 9'), flush=True)
    print('  host: ' + sp(host, 'trade spice 1 5') + ' ' + sp(host, f'trade ammo {tool} 3'), flush=True)
    g1, _ = inv(guest)
    h1, _ = inv(host)
    tk = (2, tool)
    drive = [k for k in g1 if k[0] == 2 and k not in h1]
    watch = [sa, sb, tk] + drive
    print(f'  guest before leaving: {brief(g1, watch)}', flush=True)
    print(f'  host before: {brief(h1, watch)}', flush=True)
    ck(sa in g1 and sb not in g1 and g1.get(tk, (0, 0))[1] == 9, 'the guest\'s hold: its spice A, no spice B, its bomb at 9 charges')
    ck(len(drive) == 1, f'the guest has a tool the host lacks: {drive}')
    ck(sb in h1 and h1.get(tk, (0, 0))[1] == 3, 'the host\'s hold: its spice B, its bomb at 3 charges')
    time.sleep(4)  # the guest's hold reaches the host
    print('  host: ' + sp(host, 'trade kept').replace('\n', ' | '), flush=True)

    # 2. leave and join again
    guest.call('coop.leave')
    time.sleep(4)
    e0 = epoch(guest)
    guest.call(lane.JOIN)
    loaded = wait(lambda: epoch(guest) > e0 and 'Space' in guest.call('game.mode')['out'], 180, 2)
    ck(loaded is not None, f'the guest loaded the host\'s world again (Space world {e0} -> {epoch(guest)})')
    ck(wait_guest_in(host, guest, timeout=120), 'the guest is back in the host\'s world')
    no_movie(guest)

    # 3. the guest's own hold again
    def own():
        g, _ = inv(guest)
        return g if brief(g, watch) == brief(g1, watch) else None
    r = wait(own, 45, 2)
    g2, gslots2 = inv(guest)
    print(f'  rejoined guest: {brief(g2, watch)} ({len(g2)} items, {gslots2} slots); '
          + sp(guest, 'trade kept').splitlines()[0], flush=True)
    ck(r is not None, f'the rejoined guest\'s game has its own hold again: {brief(g2, watch)} (before leaving {brief(g1, watch)})')
    ck(sb not in g2 or sb in g1, 'the host\'s spice B is not in the rejoined guest\'s hold')
    missing = [k for k in g1 if k not in g2]
    extra = [k for k in g2 if k not in g1]
    ck(not missing and not extra, f'the rejoined guest\'s items are the ones it had (missing {missing}, extra {extra})')

    # 4. the host's own hold is the host's
    h2, _ = inv(host)
    print(f'  host after: {brief(h2, watch)}; ' + sp(host, 'trade kept').replace('\n', ' | '), flush=True)
    ck(brief(h2, watch) == brief(h1, watch), f'the host\'s hold is still its own: {brief(h2, watch)}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
