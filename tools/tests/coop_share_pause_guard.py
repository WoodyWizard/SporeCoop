#!/usr/bin/env python3
"""BUG-1577: automatic world-share saves wait while the host has the Pause Menu or Save & Exit
dialog open (prevents concurrent save-during-teardown slot corruption and 0x00BA7FA7 LoadGame crash).

Reproduces Vlaid18's exact sequence from sporekit-20261126-190925.log:
  1. Two-game Tribe co-op session.
  2. Guest exits to Galaxy Menu (starts the 5 s `left_to_menu` auto-share window on host).
  3. Host immediately opens the Pause Menu (`key esc` -> `Resume Game` visible, `host busy` `why=0044`).
  4. Hold Pause Menu open for 9 s (well past the 5 s `left_to_menu` trigger):
     - ON (`world.share_not_paused=1`): host holds the automatic world share (`shares that waited for
       pause menu/dialog >= 1`), posts 0 share saves while Pause Menu is open, and `world_offers_sent`
       does NOT increase.
     - Then host closes the Pause Menu (`key esc`): the held share proceeds cleanly and guest reloads
       back into Tribe!
     - Twin OFF (`SPORE_FIX_OFF=world.share_not_paused`): host triggers `back to the world` and posts
       a share save WHILE the Pause Menu is open -> MUST FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_share_pause_guard.py
"""
import re
import sys
import time

import uifind
from harness import mode, waiter
from twoinst import Checks, start

wait = waiter(step=0.4)


def world_counts(con):
    st = con.call('coop.status').get('out') or ''
    m_s = re.search(r'world saves posted (\d+), for shares (\d+)', st)
    m_p = re.search(r'shares that waited for pause menu/dialog (\d+) \(share_not_paused=(\d+)\)', st)
    m_o = re.search(r'world_offers_sent=(\d+)', st)
    return {
        'saves': int(m_s.group(1)) if m_s else -1,
        'share_saves': int(m_s.group(2)) if m_s else -1,
        'pause_waits': int(m_p.group(1)) if m_p else -1,
        'fix': int(m_p.group(2)) if m_p else -1,
        'offers_sent': int(m_o.group(1)) if m_o else -1,
    }


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(2.5)

    c0 = world_counts(host)
    print('  initial host world counts:', c0, flush=True)
    ck(c0['pause_waits'] >= 0, f'pause share guard reported in coop.status ({c0})')

    # 1. Guest exits to Galaxy Menu (same trigger as Vlaid18 19:08:15)
    if 'open=1' in (guest.call('coop.uistate').get('out') or ''):
        guest.call('key f9')
    for _ in range(4):
        guest.call('key esc')
        time.sleep(1.0)
        if uifind.shown(guest, 'Resume Game'):
            break
    ck(uifind.shown(guest, 'Resume Game'), 'guest opened pause menu')
    ok_exit = (uifind.press(guest, 'Exit to Main Menu', 'Do you want to save your game?', timeout=8)
               and uifind.press(guest, "Don't Save", 'Are you sure?', timeout=8)
               and uifind.press(guest, "Don't Save", timeout=8))
    ck(ok_exit, 'guest chose Exit to Main Menu -> Don\'t Save')
    ck(wait(lambda: 'GalaxyGameEntry' in mode(guest), 25) is not None, 'guest arrived in Galaxy menu')

    # 2. Immediately open host Pause Menu (and confirm Resume Game is shown)
    if 'open=1' in (host.call('coop.uistate').get('out') or ''):
        host.call('key f9')
    for _ in range(4):
        host.call('key esc')
        time.sleep(1.0)
        if uifind.shown(host, 'Resume Game'):
            break
    ck(uifind.shown(host, 'Resume Game'), 'host opened pause menu')
    c_open = world_counts(host)

    # 3. Wait 9 s while Pause Menu stays open (past the 5 s left_to_menu window)
    time.sleep(9.0)
    c_held = world_counts(host)
    print('  while host paused:', c_held, flush=True)
    ck(uifind.shown(host, 'Resume Game'), 'host pause menu still open')
    ck(c_held['share_saves'] == c_open['share_saves'],
       f'no share save posted while host pause menu is open ({c_open["share_saves"]} -> {c_held["share_saves"]})')
    ck(c_held['offers_sent'] == c_open['offers_sent'],
       f'no world offer sent while host pause menu is open ({c_open["offers_sent"]} -> {c_held["offers_sent"]})')
    ck(c_held['pause_waits'] > c_open['pause_waits'],
       f'host recorded pause-menu share wait ({c_open["pause_waits"]} -> {c_held["pause_waits"]})')

    # 4. Close host Pause Menu: held share now fires and brings guest back to Tribe
    host.call('key esc')
    ck(wait(lambda: not uifind.shown(host, 'Resume Game'), 8) is not None, 'host closed pause menu')
    sent_after = wait(lambda: world_counts(host)['offers_sent'] > c_held['offers_sent'], 40)
    print('  after host resumed:', world_counts(host), flush=True)
    ck(sent_after is not None, 'held share fired automatically after host closed pause menu')
    ck(wait(lambda: 'Tribe' in mode(guest), 60) is not None, 'guest returned to Tribe after host resumed')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
