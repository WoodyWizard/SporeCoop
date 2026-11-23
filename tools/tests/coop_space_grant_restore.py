#!/usr/bin/env python3
"""Space: grants that reach a rejoined guest before its own cargo hold is back are not lost (BUG-1415, audit SPC-8).

A guest that rejoins loads the host's world (the host's hold); its own hold comes back a moment later (BUG-0678,
K_INV_KEPT, up to 20 s) and the restore (apply_inv) takes out every item that is not in it. BUG-1034 made the tool
gifts and the sales of space_trade.cpp wait; these grants still went into the replaced hold:
  * a partner's gift spice (K_GIFT, `coop.space give <n>` on the host)
  * a partner's mission reward tool (K_MISSION, `coop.space missiontool <hash>` on the host)
  * a won contract's reward tool (`coop.space contracts result 0 <hash>` on the guest)
  * Steve's gift, the Staff of Life (`coop.space finale reward` on the guest); its flag then blocked a second gift.

1. Both play the `space` fixture; the host has spice. The guest's hold reaches the host (kept for its rejoin).
2. The guest's restore is held (`coop.space trade restorehold 1`), the guest leaves and joins again.
3. In that window: the host gives 5 spice and a mission tool, the guest wins a contract tool and Steve's gift.
4. The hold is released: the restore comes (the host resends every 10 s).
PASS: the guest's game inventory (`trade spices`, `contracts tool <hash>`: the game's HasTool) has its own spice + 5,
both tools and the Staff of Life. SPORE_FIX_OFF=space.grant_wait_restore: all of them went into the replaced hold
(FAIL: spice as before, no tools, no Staff).

  SPORE_LANE=2 python3 tools/tests/coop_space_grant_restore.py
"""
import re
import time

import lane
from coop_space_map import no_movie, sp, wait
from twoinst import Checks, keyframe, start, wait_guest_in

STAFF = '72E076A7'  # genesisdevice: the Staff of Life (space_finale.cpp kToolStaff)
# reward tools of the contracts (space_contracts.cpp kSurveyTools / kArmadaTools)
TOOLS = ['4542380A', '44808DE7', '931426A0', '6DB0189A', '7B76E38C', 'FC22027B', '6DB0189B', '7B76E38D', 'FC22027A']


def has(con, h):
    return ': have' in sp(con, 'contracts tool', h)


def spice_total(con):
    return sum(int(m.group(1)) for m in re.finditer(r"cargo \S+ '[^']*' x(\d+)", sp(con, 'trade spices')))


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
    print('  host spice: ' + sp(host, 'trade spice 0 10'), flush=True)
    free = [h for h in TOOLS if not has(guest, h) and not has(host, h)]
    print(f'  tools neither game has: {free}', flush=True)
    ck(len(free) >= 2, f'two reward tools neither game has ({free})')
    staff = not has(guest, STAFF) and not has(host, STAFF) and 'staff=0' in sp(guest, 'finale')
    print(f'  Staff of Life in neither game, no flag: {staff}', flush=True)
    if len(free) < 2:
        return ck.result()
    t_mission, t_contract = free[0], free[1]
    time.sleep(4)  # the guest's hold reaches the host
    own = spice_total(guest)
    print(f'  guest spice before it leaves: {own}', flush=True)

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
    ck('restore_pending=1' in sp(guest, 'trade'), 'the guest\'s restore is pending')

    # 3. the grants in the window
    print('  host gives spice: ' + sp(host, 'give', 5), flush=True)
    print('  host mission: ' + sp(host, 'missiontool', t_mission), flush=True)
    print('  guest contract: ' + sp(guest, 'contracts result 0', t_contract).strip(), flush=True)
    if staff:
        print('  guest Steve\'s gift: ' + sp(guest, 'finale reward'), flush=True)
    time.sleep(6)  # all of them reach the guest inside the window
    print('  guest in the window: ' + sp(guest, 'gift') + ' | ' + sp(guest, 'contracts owed').strip(), flush=True)

    # 4. the restore
    print('  ' + sp(guest, 'trade restorehold 0'), flush=True)
    ck(wait(lambda: 'restore_pending=0' in sp(guest, 'trade') and field(sp(guest, 'trade kept'), 'restored') >= 1, 30)
       is not None, f'the guest\'s own hold came back ({sp(guest, "trade kept").splitlines()[0]})')
    got_spice = wait(lambda: spice_total(guest) == own + 5, 15)
    got_mission = wait(lambda: has(guest, t_mission), 15)
    got_contract = wait(lambda: has(guest, t_contract), 10)
    got_staff = wait(lambda: has(guest, STAFF), 15) if staff else None
    print(f'  after the restore: spice {spice_total(guest)} (own {own} + 5), mission tool {has(guest, t_mission)}, '
          f'contract tool {has(guest, t_contract)}, Staff {has(guest, STAFF)}', flush=True)
    print('  guest finale: ' + sp(guest, 'finale')[-200:], flush=True)
    keyframe('grant_restore', 'the rejoined guest after its hold came back: the gift spice, the two reward tools and '
             'the Staff of Life of the restore window are in its cargo', players={'host': host, 'guest': guest})
    ck(got_spice is not None, f'the gift spice is in the guest\'s cargo on top of its own: {spice_total(guest)} ({own + 5})')
    ck(got_mission is not None, f'the mission reward tool {t_mission} is in the guest\'s cargo')
    ck(got_contract is not None, f'the contract reward tool {t_contract} is in the guest\'s cargo')
    if staff:
        ck(got_staff is not None, 'Steve\'s gift (the Staff of Life) is in the guest\'s cargo')
    else:
        print('  (the Staff part is not checked: the fixture already has the core gift)')
    print('  guest trade: ' + sp(guest, 'trade').strip()[-160:], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
