#!/usr/bin/env python3
"""Space: guest receives and keeps Flight School starter tools across tutorial sync and rejoin (BUG-1570).

Discord (Doctor): a friend on one Space save ended up without the basic ship tools (scanner, laser,
abduction beam). Spore grants the 6 Flight School starter tools via cPlayerInventory::AddDefaultTools
(0x00FF4590, prop 0xF0609EC3: scan 0x08EF6A62, laserlevel1 0x6DB01899, abduct 0xF58C1654, dropcargo
0xD001F5E2, healthupgrade1 0x88914EF2, energyupgrade1 0x33E11760) directly in cMissionFlightSchool
without a synced mission reward broadcast. Two bugs left guests without them:
  1. BUG-1559's tutorial sync set mbFlightSchoolFinished=1 and granted drives, but never granted the
     6 Flight School starter tools.
  2. space_trade.cpp's apply_inv() stripped starter tools from a freshly loaded host save when restoring
     a guest's older pre-Flight-School kept cargo hold (holds/<crc>.txt or K_INV_KEPT).

1. Start both games on `space` fixture. Simulate pre-Flight-School Space entry on both: reset tutorial
   flags (0 0 0) and remove the 6 starter tools on both host and guest. Give guest 7 units of spice A
   and wait for its pre-Flight-School hold to be recorded on host and disk.
2. Phase 1 (kept-hold rejoin protection): guest leaves; host finishes Flight School (adds the 6 starter
   tools and sets tutorial 1 1 0); guest rejoins and loads the host's world. Verify guest's kept hold
   is restored (spice A x7, restored >= 1) AND all 6 starter tools survive (starters_kept >= 6).
3. Phase 2 (live in-session tutorial sync): reset both to pre-Flight-School (0 0 0, no starter tools).
   On host ONLY, finish Flight School (add 6 starter tools + tutorial set 1 1 0) without reloading.
   Verify guest automatically receives flight_school=1 and all 6 starter tools (tut_starter_granted >= 6).
PASS: 1..3 with zero faults.
SPORE_FIX_OFF=space.starter_tools_sync (`space_starter_tools_off`): Phase 1 strips starter tools on
rejoin and Phase 2 unlocks flight_school=1 without granting scan/laser/abduct/dropcargo: FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_space_starter_tools.py
"""
import re
import time

import lane
from coop_space_items_rejoin import epoch, inv, spice_keys
from coop_space_map import no_movie, sp, wait
from coop_space_tut_drive import tut_flags
from harness import num
from twoinst import Checks, start, wait_guest_in

STARTERS = [
    ('scan', '08EF6A62'),
    ('laserlevel1', '6DB01899'),
    ('abduct', 'F58C1654'),
    ('dropcargo', 'D001F5E2'),
    ('healthupgrade1', '88914EF2'),
    ('energyupgrade1', '33E11760'),
]


def present_starters(con):
    out = sp(con, 'tools')
    return [name for name, h in STARTERS if h in out]


def missing_starters(con):
    out = sp(con, 'tools')
    return [name for name, h in STARTERS if h not in out]


def strip_starters(con):
    for name, _ in STARTERS:
        sp(con, 'tool remove', name)


def add_starters(con):
    for name, _ in STARTERS:
        sp(con, 'tool add', name)


def main():
    host, guest = start('space')
    ck = Checks()
    for c in (host, guest):
        no_movie(c)

    # 1. Simulate fresh pre-Flight-School Space entry on both players
    sp(host, 'tutorial set 0 0 0')
    strip_starters(host)
    time.sleep(1.5)
    sp(guest, 'tutorial set 0 0 0')
    strip_starters(guest)
    time.sleep(0.5)

    ck(not present_starters(host), f'host starts pre-Flight-School without starter tools ({present_starters(host)})')
    ck(not present_starters(guest), f'guest starts pre-Flight-School without starter tools ({present_starters(guest)})')

    keys = spice_keys(guest)
    ck(len(keys) >= 1, f'game knows spice keys ({len(keys)})')
    if not keys:
        return ck.result()
    sa = (4, keys[0])
    print('  guest pre-FS spice: ' + sp(guest, 'trade spice 0 7'), flush=True)
    time.sleep(4)  # pre-Flight-School hold (spice A x7, 0 starter tools) reaches host & holds/<crc>.txt
    g0, _ = inv(guest)
    ck(g0.get(sa, (0,))[0] == 7, f'guest has spice A x7 before leaving ({g0.get(sa)})')
    print('  host kept before leave: ' + sp(host, 'trade kept').splitlines()[0], flush=True)

    # 2. Phase 1: guest leaves, host finishes Flight School, guest rejoins -> kept hold must not strip starter tools
    guest.call('coop.leave')
    time.sleep(4)
    add_starters(host)
    sp(host, 'tutorial set 1 1 0')
    ck(not missing_starters(host), f'host finished Flight School with all 6 starter tools (missing={missing_starters(host)})')

    e0 = epoch(guest)
    guest.call(lane.JOIN)
    loaded = wait(lambda: epoch(guest) > e0 and 'Space' in guest.call('game.mode')['out'], 180, 2)
    ck(loaded is not None, f'guest loaded host save after Flight School (world {e0} -> {epoch(guest)})')
    ck(wait_guest_in(host, guest, timeout=120), 'guest rejoined host world')
    no_movie(guest)

    restored = wait(lambda: (num(sp(guest, 'trade kept').splitlines()[0], 'restored') or 0) >= 1 and inv(guest)[0].get(sa, (0,))[0] == 7, 45, 1.5)
    g1, _ = inv(guest)
    g_kept = sp(guest, 'trade kept').splitlines()[0]
    miss1 = missing_starters(guest)
    kept_cnt = num(g_kept, 'starters_kept') or 0
    print(f'  Phase 1 after rejoin: spiceA={g1.get(sa)} missing_starters={miss1} | {g_kept}', flush=True)
    ck(restored is not None, f'guest kept hold restored spice A x7 ({g1.get(sa)}, {g_kept})')
    ck(not miss1, f'rejoined guest kept all 6 Flight School starter tools despite pre-FS kept hold (missing={miss1})')
    ck(kept_cnt >= 6,  # counter: confirms apply_inv preserved starter tools instead of stripping them; live inventory checked above
       f'apply_inv preserved starter tools from host save (starters_kept={kept_cnt})')

    # 3. Phase 2: live in-session Flight School sync without reload
    sp(host, 'tutorial set 0 0 0')
    strip_starters(host)
    time.sleep(1.5)
    sp(guest, 'tutorial set 0 0 0')
    strip_starters(guest)
    time.sleep(0.5)
    ck(not present_starters(guest) and tut_flags(guest)[:3] == (0, 0, 0),
       f'guest reset to pre-Flight-School before live sync (starters={present_starters(guest)} {tut_flags(guest)[3]})')

    # Host unlocks Flight School live (no reload, no touch on guest)
    add_starters(host)
    sp(host, 'tutorial set 1 1 0')
    synced = wait(lambda: tut_flags(guest)[1] == 1 and not missing_starters(guest), 15, 0.5)
    gf2 = tut_flags(guest)
    miss2 = missing_starters(guest)
    g_st = sp(guest, '').splitlines()[0]
    sg = num(g_st, 'tut_starter_granted') or 0
    print(f'  Phase 2 after live sync: {gf2[3]} missing_starters={miss2} | {g_st}', flush=True)
    ck(synced is not None and not miss2,
       f'live Flight School sync granted all 6 starter tools to guest ({gf2[3]} missing={miss2})')
    ck(sg >= 6,  # counter: confirms tut_sync granted missing starter tools; live inventory checked above
       f'guest tutorial sync counter recorded starter grants (tut_starter_granted={sg})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
