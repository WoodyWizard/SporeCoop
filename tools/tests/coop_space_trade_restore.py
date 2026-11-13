#!/usr/bin/env python3
"""Space: a gift and a sale that reach a rejoined guest before its own cargo hold is back are not lost (BUG-1034).

A guest that rejoins loads the host's world (the host's hold); its own hold comes back a moment later (BUG-0678,
K_INV_KEPT, up to 20 s). A tool gift (K_TOOL) or a spice sale to its colony (K_SELL) in that window went into the
host's copy, which the restore then replaced: the item was in neither game (the sender was told it was taken).

1. Both play the `space` fixture. The guest has an Antimatter Bomb with 9 charges, the host one with 12 and two
   kinds of spice. The guest's hold reaches the host (kept for its rejoin).
2. The guest's restore is held (`coop.space trade restorehold 1`: as a slow host, the kept hold is not taken and the
   guest's own record is not used), the guest leaves and joins again.
3. In that window the host gives 5 bomb charges to the guest and sells spice to the guest's colony.
4. The hold is released: the restore comes (the host resends every 10 s).
PASS: the guest's game (`coop.space trade all` / `trade spices`: the game's inventory) has 9 + 5 charges and the
sold spice on top of its own hold; the host's game has 12 - 5 charges. SPORE_FIX_OFF=space.trade_wait_restore: the
gift and the sale went into the replaced hold (FAIL: 9 charges, no spice).

  SPORE_LANE=2 python3 tools/tests/coop_space_trade_restore.py
"""
import re
import time

import lane
from coop_space_map import no_movie, sp, wait
from twoinst import Checks, keyframe, start, wait_guest_in

BOMB = '144DF50A'  # Antimatter Bomb (a tool with charges)


def ammo(con):
    m = re.search(r'tool %s \S+ \S+ \'[^\']*\' ammo=(-?\d+)' % BOMB, sp(con, 'trade all'))
    return int(m.group(1)) if m else None


def cargo(con, inst):
    m = re.search(r'cargo %s \'[^\']*\' x(\d+)' % inst, sp(con, 'trade spices'))
    return int(m.group(1)) if m else 0


def field(text, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', text)
    return int(m.group(1)) if m else -1


def epoch(con):
    m = re.search(r'world=(\d+) open=1', sp(con, 'world'))
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    no_movie(host)
    print('  bombs: guest ' + sp(guest, 'tool add', '0x' + BOMB) + ', host ' + sp(host, 'tool add', '0x' + BOMB), flush=True)
    print('  ammo: ' + sp(guest, f'trade ammo {BOMB} 9') + ' ' + sp(host, f'trade ammo {BOMB} 12'), flush=True)
    print('  host spice: ' + sp(host, 'trade spice 0 10') + ' ' + sp(host, 'trade spice 1 10'), flush=True)
    ck(ammo(guest) == 9 and ammo(host) == 12, f'bombs: guest {ammo(guest)} (9), host {ammo(host)} (12)')
    time.sleep(4)  # the guest's hold reaches the host
    print('  host kept: ' + sp(host, 'trade kept').replace('\n', ' | '), flush=True)

    # 2. leave, hold the restore, join again
    print('  ' + sp(guest, 'trade restorehold 1'), flush=True)
    guest.call('coop.leave')
    time.sleep(4)
    e0 = epoch(guest)
    sent0 = field(sp(host, 'trade kept'), 'sent_back')
    guest.call(lane.JOIN)
    loaded = wait(lambda: epoch(guest) > e0 and 'Space' in guest.call('game.mode')['out'], 180, 2)
    ck(loaded is not None, f'the guest loaded the host\'s world again (Space world {e0} -> {epoch(guest)})')
    ck(wait_guest_in(host, guest, timeout=120), 'the guest is back in the host\'s world')
    no_movie(guest)
    ck(wait(lambda: field(sp(host, 'trade kept'), 'sent_back') > sent0, 30) is not None,
       'the host sent the guest\'s kept hold back (the restore window is open)')
    g_before = ammo(guest)
    print(f'  guest in the window: bomb {g_before}; ' + sp(guest, 'trade').strip()[-120:], flush=True)
    ck('restore_pending=1' in sp(guest, 'trade'), 'the guest\'s restore is pending')

    # 3. the gift and the sale in the window
    m = re.search(r'id=(\d+)', guest.call('coop.status')['out'])
    gid = int(m.group(1)) if m else None
    print(f'  give: ' + sp(host, 'trade give', BOMB, 5, *([gid] if gid is not None else [])), flush=True)
    deal = wait(lambda: (lambda d: d if d.startswith('deal ') else None)(sp(host, 'trade deal')), 30)
    print(f'  host deal: {deal}', flush=True)
    dm = re.search(r'spice (\S+) .* x(\d+) price (\d+)', deal or '')
    ck(dm is not None, 'the host can sell spice to the guest\'s colony')
    inst, n = (dm.group(1), int(dm.group(2))) if dm else (None, 0)
    if inst:
        n = min(n, 10)
        gc0 = cargo(guest, inst)
        print(f'  sell: ' + sp(host, 'trade sell', n) + f' (guest cargo of {inst} now {gc0})', flush=True)
    ck(wait(lambda: ammo(host) == 7, 5) is not None, f'the host\'s 5 charges left its game ({ammo(host)})')
    time.sleep(4)  # both reach the guest inside the window
    print('  guest trade: ' + sp(guest, 'trade').strip(), flush=True)

    # 4. the restore
    print('  ' + sp(guest, 'trade restorehold 0'), flush=True)
    ck(wait(lambda: 'restore_pending=0' in sp(guest, 'trade') and field(sp(guest, 'trade kept'), 'restored') >= 1, 30)
       is not None, f'the guest\'s own hold came back ({sp(guest, "trade kept").splitlines()[0]})')
    got = wait(lambda: ammo(guest) == 14, 15)
    g_after, h_after = ammo(guest), ammo(host)
    print(f'  after the restore: guest bomb {g_after}, host bomb {h_after}', flush=True)
    keyframe('trade_restore', 'the rejoined guest after its hold came back: the gift and the sale of the restore '
             'window are in its cargo', players={'host': host, 'guest': guest})
    ck(got is not None, f'the guest\'s game has its own 9 charges plus the 5 given in the window ({g_after})')
    ck(h_after == 7, f'the host\'s game gave 5 and got none back ({h_after})')
    if inst:
        gc1 = wait(lambda: (lambda c: c if c >= n else None)(cargo(guest, inst)), 15) or cargo(guest, inst)
        ck(gc1 >= n, f'the sold spice ({n} x {inst}) is in the guest\'s cargo ({gc1})')
    print('  guest trade: ' + sp(guest, 'trade').strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
