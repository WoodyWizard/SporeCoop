#!/usr/bin/env python3
"""Cell stage: the own cell editor entered within the 2-3 s after a real respawn or revive (BUG-0237).

coop_cell_editor_invuln.py sets the flag with a test command (`coop.pool invulnfor 120`). This one
takes the real way and the real timing the issue (#171) describes:

1. The guest calls its mate (the mating-call button, real click) and then is knocked out
   (`coop.pool selfhit 250`): 25 m from the host (respawn, 3 s invulnerable) or, with --revive,
   right next to it (the host revives it, 2 s invulnerable).
2. The moment the knockout ends (polled every 0.1 s), the guest's cell is put onto its mate
   (`coop.pool mate tp`): the game's own collision opens the cell editor. The test prints how many
   seconds after the end of the knockout the Cell stage was left, and the avatar's +0x111 then.
3. Cancel. Back in the pool: the avatar's +0x111 (`coop.cell pool` av_invuln) is 0 within 4 s, and
   a host bite (`coop.pool hurt 1 1`) lowers the guest's hp in the guest's game.

Measured (2026-10-30, lane 3): the Cell stage is left ~10 s after the cell touches its mate (the
mating cinematic), so the 2 s / 3 s window of the issue is over before the editor opens, whatever
the player does; the test prints that time and stays as a guard of the editor return.

--vote (N2 of 0.7.11): the other way into a cell editor, the host's evolution vote (a guest's own editor never
opens with it: the vote moves only the host). The host calls and reaches its mate (the vote starts), the host is
knocked out while the vote is open, and the moment its knockout ends the guest clicks "I'm ready": the host's
editor opens without a mating cinematic in between, so the 2-3 s window is reachable this way. Then cancel, the
host's +0x111 clear, a bite of 1 (`coop.pool selfhit 1`, the bitten player's path) lowers the host's hp.

  SPORE_LANE=3 python3 tools/tests/coop_cell_editor_timed.py [--revive] [--vote]
"""
import re
import sys
import time

from twoinst import Checks, start
from coop_editor_desync import CANCEL, HEART, READY, mate, mode, wait


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def hp(con):
    m = re.search(r'^avatar hp=(-?\d+)', con.call('coop.pool')['out'], re.MULTILINE)
    return int(m.group(1)) if m else None


def host_vote(host, guest, ck):
    """--vote: the host's mating call and its mate (coop_evolve_in_place.host_cell_editor's first part)"""
    from coop_evolve_in_place import hud_ready
    vote = lambda: 'vote active' in host.call('coop.vote')['out']  # noqa: E731
    for _ in range(3):
        wait(lambda: hud_ready(host), 40)
        host.call('coop.pool mate button')
        time.sleep(1)
        host.call('click %d %d' % HEART)
        time.sleep(3)
        if 'alive=1' in host.call('coop.pool mate')['out']:
            break
    for _ in range(25):
        if vote():
            break
        m = re.search(r'alive=1 .* at (-?\d+) (-?\d+)', host.call('coop.pool mate')['out'])
        if m:
            x, y = (min(max(int(v), 20), lim) for v, lim in ((m.group(1), 1420), (m.group(2), 1000)))
            host.call(f'click {x} {y}')
        time.sleep(2)
    return ck(vote(), 'the host reaches its mate: the evolution vote starts')


def main():
    revive = '--revive' in sys.argv
    by_vote = '--vote' in sys.argv
    window = 2.0 if revive else 3.0
    host, guest = start('cell', guest_creations=False)
    ck = Checks()
    wait(lambda: 'cine=0' in guest.call('coop.pool')['out'], 30)
    ck(wait(lambda: (num(guest.call('coop.pool')['out'], 'mirrors') or 0) > 5, 30), 'the guest mirrors the host pool')
    if by_vote:
        return main_vote(host, guest, ck, revive, window)
    # 1. the mate, then the knockout
    for _ in range(3):
        guest.call('coop.pool mate button')
        time.sleep(1)
        guest.call('click %d %d' % HEART)
        time.sleep(3)
        if 'alive=1' in mate(guest):
            break
    print('  mate: ' + mate(guest).strip())
    if not ck('alive=1' in mate(guest), 'the guest\'s mate is in the pool'):
        return ck.result()
    print('  guest: ' + guest.call('coop.pool tp %s' % ('0.5 0' if revive else '25 0'))['out'])
    time.sleep(0.5)
    r = guest.call('coop.pool selfhit 250')['out']
    print('  guest: ' + r)
    if not ck(num(r, 'downed') == 1, 'the guest is knocked out'):
        return ck.result()
    if revive:
        host.call('coop.pool tp 0.5 0')
    # 2. the end of the knockout, then right onto the mate
    t0 = time.time()
    while time.time() - t0 < 20 and num(guest.call('coop.pool')['out'], 'downed') != 0:
        time.sleep(0.1)
    t_up = time.time()
    st = guest.call('coop.pool')['out']
    up_line = guest.call('coop.cell pool')['out'].strip()
    print(f'  knockout over after {t_up - t0:.1f} s ({"revive" if revive else "respawn"}): revives={num(st, "revives")} '
          f'respawns={num(st, "respawns")}; {up_line}')
    print('  mate tp: ' + guest.call('coop.pool mate tp')['out'].strip())
    left = None
    last = ''
    while time.time() - t_up < 15:
        if 'Cell' not in mode(guest):
            left = time.time() - t_up
            break
        last = guest.call('coop.cell pool')['out'].strip()
        time.sleep(0.05)
    print(f'  Cell stage left {left if left is None else round(left, 2)} s after the knockout ended '
          f'(window {window} s); last read in the pool: {last}')
    if left is None:
        ck(False, 'the guest\'s cell editor opens (the mate was touched)')
        return ck.result()
    reached = wait(lambda: 'Editor' in mode(guest), 30, 0.2)
    ck(reached, f'the guest\'s own cell editor opens ({mode(guest).strip()})')
    in_window = left < window
    # 3. back from the editor
    time.sleep(3)
    guest.call('click %d %d' % CANCEL)
    back = wait(lambda: 'Cell' in mode(guest) and 'av_ptr=00000000' not in guest.call('coop.cell pool')['out'], 30, 0.2)
    ck(back, 'the guest leaves its editor (cancel)')
    print('  back: ' + guest.call('coop.cell pool')['out'].strip())
    wait(lambda: 'cine=0' in guest.call('coop.pool')['out'], 30)
    clear = wait(lambda: 'av_invuln=0' in guest.call('coop.cell pool')['out'], 4, 0.2)
    p = guest.call('coop.cell pool')['out'].strip()
    print('  then: ' + p)
    ck(clear, f'the avatar is not invulnerable after the editor ({p})')
    h0 = hp(guest)
    host.call('coop.pool hurt 1 1')
    time.sleep(1.5)
    h1 = hp(guest)
    print(f'  host bite: guest hp {h0} -> {h1}')
    ck(h0 is not None and h1 is not None and h1 < h0, f'a host bite hurts the guest after its editor ({h0} -> {h1})')
    ck.faults(host=host, guest=guest)
    if not in_window:  # lane 3, 2026-10-30: always ~10 s (the mating cinematic): the issue's window cannot be hit
        print(f'  note: the Cell stage was left {left:.2f} s after the knockout, after the {window} s window: the timer '
              f'had cleared +0x111 already; the checks above still guard the editor return')
    return ck.result()


def main_vote(host, guest, ck, revive, window):
    """--vote: the host is the player knocked out while its own vote is open; the guest's "I'm ready" the
    moment the host's knockout ends opens the host's editor (a guest's own editor never opens with the vote:
    lab 20261108-034621, the vote moves only the host, docs/re/cell.md 12)"""
    if not host_vote(host, guest, ck):
        return ck.result()
    wait(lambda: "'I'm ready'" in guest.call('coop.uistate')['out'], 10)
    wait(lambda: 'Cell' in mode(host) and 'cine=0' in host.call('coop.pool')['out'], 20, 0.2)
    print('  host: ' + host.call('coop.pool tp %s' % ('0.5 0' if revive else '25 0'))['out'])
    time.sleep(0.5)
    r = host.call('coop.pool selfhit 250')['out']
    print('  host: ' + r)
    if not ck(num(r, 'downed') == 1, 'the host is knocked out (its vote open)'):
        return ck.result()
    if revive:
        guest.call('coop.pool tp 0.5 0')
    t0 = time.time()
    while time.time() - t0 < 20 and num(host.call('coop.pool')['out'], 'downed') != 0:
        time.sleep(0.1)
    t_up = time.time()
    guest.call('click %d %d' % READY)
    st = host.call('coop.pool')['out']
    print(f'  host knockout over after {t_up - t0:.1f} s ({"revive" if revive else "respawn"}): revives={num(st, "revives")} '
          f'respawns={num(st, "respawns")}; the guest clicked "I\'m ready"; {host.call("coop.cell pool")["out"].strip()}')
    left, last, inv_last = None, '', None
    while time.time() - t_up < 20:
        if 'Cell' not in mode(host):
            left = time.time() - t_up
            break
        last = host.call('coop.cell pool')['out'].strip()
        inv_last = num(last, 'av_invuln')
        time.sleep(0.05)
    print(f'  the host left Cell {left if left is None else round(left, 2)} s after its knockout ended (window {window} s); '
          f'last read in the pool: {last}')
    if not ck(left is not None and wait(lambda: 'Editor' in mode(host), 30, 0.2),
              f'the host\'s cell editor opens after the vote ({mode(host).strip()})'):
        return ck.result()
    in_window = left < window
    print(f'  {"inside" if in_window else "outside"} the window; av_invuln on the last read in Cell: {inv_last}')
    ck(in_window, f'the editor opened inside the {window} s window ({left:.2f} s): the issue\'s timing reached')
    time.sleep(3)
    host.call('click %d %d' % CANCEL)
    back = wait(lambda: 'Cell' in mode(host) and 'av_ptr=00000000' not in host.call('coop.cell pool')['out'], 30, 0.2)
    ck(back, 'the host leaves its editor (cancel)')
    print('  back: ' + host.call('coop.cell pool')['out'].strip())
    wait(lambda: 'cine=0' in host.call('coop.pool')['out'], 30)
    time.sleep(window + 1)  # past any timer of before the editor
    p = host.call('coop.cell pool')['out'].strip()
    print('  then: ' + p)
    ck('av_invuln=0' in p, f'the host\'s avatar is not invulnerable after the editor ({p})')
    # a bite of 1 on the host's own cell (`coop.pool selfhit 1`: the path a partner's bite takes on the bitten
    # player's game, hurt_avatar, which the game's +0x111 stops; the host refuses a guest's MSG_CELL_HIT on a
    # player's cell, so `coop.pool hurt 0 1` from the guest is no test: lab 20261108-034839)
    h0 = hp(host)
    r = host.call('coop.pool selfhit 1')['out'].strip()
    time.sleep(0.5)
    h1 = hp(host)
    print(f'  bite of 1 on the host ({r}): host hp {h0} -> {h1}')
    ck(h0 is not None and h1 is not None and h1 < h0, f'a bite hurts the host after its editor ({h0} -> {h1})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
