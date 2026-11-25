#!/usr/bin/env python3
"""Space: skipping the Space tutorial on host grants interstellar drive and tutorial unlocks to guest (BUG-1559).

GitHub #221 (Dedlection): when the host skips the Space tutorial (or completes Flight School / Story201
or scans the wreck in-session), Spore sets cMissionManager +0x16/+0x17/+0x18 and adds interstellardrive
(0x2569FAE8) directly without a synced mission reward broadcast, leaving guests without the drive and
locked inside the home star system (galaxy travel check 0xFFC780 refuses other stars while +0x18 == 0).

1. Start two games on the `space` fixture (starts with drive=0, m201=0). Reset tutorial flags to 0 on
   host then guest so both start locked (first_comm=0, flight_school=0, m201=0, drive=0).
2. On the host ONLY, simulate skipping the Space tutorial: unlock tutorial flags (1, 1, 1) and add
   `interstellardrive` (0x2569FAE8). Never touch the guest or reload the save.
3. Verify the guest automatically receives `first_comm=1`, `flight_school=1`, `m201=1` (`coop.space tutorial`)
   and `drive=1` (`coop.space empire`, tool 2569FAE8 in `coop.space tools`), plus counters.
4. Real gameplay verification on the guest: zoom out to the galaxy view by mouse wheel (only allowed with
   interstellardrive) and click a nearby unowned star (only allowed with m201=1): the guest's ship arrives.
PASS: 1..4 with zero faults.
SPORE_FIX_OFF=space.tutorial_drive_sync (`space_tut_drive_off`): guest stays locked at m201=0 drive=0: FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_space_tut_drive.py
"""
import re
import time

from coop_space_free import travel
from coop_space_jump import settle
from coop_space_map import no_movie, sp, to_galaxy, wait
from harness import num
from twoinst import Checks, start


def tut_flags(con):
    out = sp(con, 'tutorial')
    m = re.search(r'first_comm=(\d+) flight_school=(\d+) m201=(\d+)', out)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), out) if m else (-1, -1, -1, out)


def has_drive(con):
    emp = sp(con, 'empire')
    m = re.search(r'drive=(\d)', emp)
    return int(m.group(1)) == 1 if m else False


def has_drive_tool(con):
    return '2569FAE8' in sp(con, 'tools')


def main():
    host, guest = start('space')
    ck = Checks()
    for c in (host, guest):
        no_movie(c)

    # 1. Start both players locked before tutorial skip (host first so any initial sync clears before guest reset)
    sp(host, 'tutorial set 0 0 0')
    time.sleep(1.5)
    sp(guest, 'tutorial set 0 0 0')
    time.sleep(0.5)

    hf0 = tut_flags(host)
    gf0 = tut_flags(guest)
    hd0, gd0 = has_drive(host), has_drive(guest)
    print(f'  initial host: {hf0[3]} drive={int(hd0)}', flush=True)
    print(f'  initial guest: {gf0[3]} drive={int(gd0)}', flush=True)
    ck(hf0[:3] == (0, 0, 0) and not hd0, f'host starts locked without drive ({hf0[3]} drive={int(hd0)})')
    ck(gf0[:3] == (0, 0, 0) and not gd0, f'guest starts locked without drive ({gf0[3]} drive={int(gd0)})')

    # 2. Host skips Space tutorial (unlock flags + interstellar drive) without reloading or touching guest
    sp(host, 'tutorial set 1 1 1')
    add_out = sp(host, 'tool add interstellardrive')
    print(f'  host skip: {sp(host, "tutorial")} | tool add: {add_out}', flush=True)
    ck(tut_flags(host)[:3] == (1, 1, 1) and has_drive(host),
       f'host has unlocked tutorial and interstellar drive ({tut_flags(host)[3]} drive={int(has_drive(host))})')
    settle(host, 'host')

    # 3. Guest automatically receives tutorial unlocks and interstellar drive
    synced = wait(lambda: tut_flags(guest)[:3] == (1, 1, 1) and has_drive(guest) and has_drive_tool(guest), 15)
    gf1 = tut_flags(guest)
    gd1 = has_drive(guest)
    gt1 = has_drive_tool(guest)
    h_st = sp(host, '').splitlines()[0]
    g_st = sp(guest, '').splitlines()[0]
    print(f'  after sync guest: {gf1[3]} drive={int(gd1)} tool2569FAE8={int(gt1)}', flush=True)
    print(f'  host status:  {h_st}', flush=True)
    print(f'  guest status: {g_st}', flush=True)
    ck(synced is not None,
       f'guest automatically unlocked tutorial flags and received interstellar drive ({gf1[3]} drive={int(gd1)} tool={int(gt1)})')
    tx = num(h_st, 'tut_sync_tx') or 0
    rx = num(g_st, 'tut_sync_rx') or 0
    fa = num(g_st, 'tut_flags_applied') or 0
    dg = num(g_st, 'tut_drive_granted') or 0
    ck(tx >= 1,  # counter: confirms host broadcast tutorial sync; guest game state checked above
       f'host broadcast tutorial sync (tut_sync_tx={tx})')
    ck(rx >= 1 and fa >= 1 and dg >= 1,  # counter: confirms guest received and applied tutorial flags and drive; guest game state checked above
       f'guest applied tutorial sync (rx={rx} flags={fa} drives={dg})')

    # 4. Real gameplay verification on guest: zoom out to galaxy view and click a nearby unowned star
    settle(guest, 'guest')
    in_gal = to_galaxy(guest)
    ck(in_gal, 'guest zoomed out to the galaxy view using the synced interstellar drive')
    if in_gal:
        settle(guest, 'guest')
        time.sleep(2)
        star, arrived = travel(guest, 'guest', 1, set())
        ck(star is not None, 'an unowned nearby star is visible on the guest galaxy screen')
        ck(arrived, f'guest ship traveled to unowned star {star["name"] if star else "?"} after tutorial unlock')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
