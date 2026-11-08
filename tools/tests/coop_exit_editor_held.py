#!/usr/bin/env python3
"""Tribe -> Civ: the player quits the game (its own exit) while the City Hall editor is requested (BUG-0578).

A player's 0.7.3.6 crash (2026-11-03, Steam): the host's vote for the City Hall editor ended, "the editor waits: the
game is busy (paused)" (BUG-0576, fixed in 0.7.7 by core.editor_own_pause), the guest left, and 81 s later the
player quit; 57 ms after the mod unloaded the game faulted at cModelWorld::FinishBackgroundLoad+0x2735 (GOG 2024
0x753D05, unlinking a list node with null links) under cGameModeManager::Dispose <- cEditor::Dispose <- Model::Release.
The lab never ran the game's own exit (spore.sh kill, `quit`: TerminateProcess).

tribe fixture, the way of coop_tribe_civ.py up to the end of the vote, then:
  --held (default): core.editor_own_pause off on the host, so the editor waits as in 0.7.3.6; the guest leaves;
                    40 s later (before the old gate's ~60 s) the host's window gets WM_CLOSE (coop.close: the close button / Alt+F4)
  --editor:         the fix on: the City Hall editor opens; the guest leaves; WM_CLOSE in the editor
Checked: the host's game ends by itself within 90 s and writes no crash report (crash-*.txt) on the way out.

  SPORE_LANE=9 python3 tools/tests/coop_exit_editor_held.py [--editor]
"""
import argparse
import glob
import os
import sys
import time

import lane
import twoinst
from coop_tribe_civ import ADVANCE, CAPTION_OK, CARD_OK, busy, mode, vote_active, wait, where
from coop_ui import Ui
from twoinst import Checks, shot, start


def crashes(inst):
    d = os.path.dirname(lane.game_log(inst))
    return set(glob.glob(os.path.join(d, 'crash-*.txt')) + glob.glob(os.path.join(d, 'report-*.txt')))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--editor', action='store_true')
    a = ap.parse_args()
    ck = Checks()
    host, guest = start(fixture='tribe')
    if not a.editor:
        print(' ', host.call('coop.fix core.editor_own_pause off')['out'].strip(), flush=True)
    print(' ', host.call('coop.tribe advance')['out'].strip(), flush=True)
    r = wait(lambda: where(host, ADVANCE), 15)
    if not ck(r, 'the HUD shows the advance button (the goal bar is full)'):
        return ck.result()
    host.call('click %d %d' % ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2))
    time.sleep(5)
    t0 = time.time()
    while time.time() - t0 < 120 and not vote_active(host) and 'Editor' not in mode(host):
        b = busy(host)
        if 'cinematic' not in b:
            host.call('click %d %d' % CARD_OK)
        elif 'paused' in b:
            time.sleep(2)
            host.call('click %d %d' % CAPTION_OK)
        time.sleep(3)
    if not ck(vote_active(host), "the game's City Hall editor request starts the vote"):
        return ck.result()
    gu = Ui(guest, 'guest')
    gu.wait(lambda s: s['ready']['vis'], 15, 'ready button')
    gu.click('ready')
    ck(wait(lambda: not vote_active(host), 20, 0.5), 'the vote ends when the guest is ready')
    if a.editor:
        ck(wait(lambda: 'Editor' in mode(host), 30, 0.5), 'the host is in the City Hall editor')
        time.sleep(8)
    else:
        time.sleep(10)
        st = host.call('coop.status')['out']
        held = 'Editor' not in mode(host)
        print(f'  host after the vote: mode {mode(host).strip()[:40]}, busy {busy(host)}', flush=True)
        ck(held, 'the editor waits (the 0.7.3.6 state: the game paused for its own request)')
        print('  ' + ' | '.join(ln.strip() for ln in st.splitlines() if 'editor' in ln.lower())[:300], flush=True)
    shot(host, 'exit_editor_before.png')
    print(' ', guest.call('coop.leave')['out'].strip()[:120], flush=True)
    # the report: 81 s between the held editor and the player's quit; the old gate of today's code (core.editor_own_pause
    # off) opens the editor ~60 s after the vote (BUG-0576), so 40 s to close in the held state
    time.sleep(20 if a.editor else 40)
    if not a.editor:
        ck('Editor' not in mode(host), f'the editor still waits at the exit ({mode(host).strip()[:40]})')
    ck('total faults: 0' in host.call('exc')['out'], 'host: no fault before the exit')
    ck('total faults: 0' in guest.call('exc')['out'], 'guest: no fault (its game ends with the host\'s: one bottle)')
    for m in twoinst._monitors:  # the host is about to end on purpose: no "froze / crashed" from the watchers
        m.stop = True
    before = crashes(lane.HOST)
    print(' ', host.call('coop.close')['out'].strip(), flush=True)
    gone = wait(lambda: not twoinst.alive(lane.HOST), 90, 1.0)
    ck(gone, 'the host\'s game ends by itself after WM_CLOSE (its own exit)')
    time.sleep(3)
    new = sorted(crashes(lane.HOST) - before)
    for p in new:
        print(f'  new report {os.path.basename(p)}:', flush=True)
        try:
            print('    ' + '\n    '.join(open(p, errors='replace').read().splitlines()[:12]), flush=True)
        except OSError:
            pass
    ck(not new, 'no crash report on the way out')
    try:
        tail = open(lane.game_log(lane.HOST), errors='replace').read().splitlines()[-6:]
        print('  host log end:\n    ' + '\n    '.join(t[:160] for t in tail), flush=True)
    except OSError:
        pass
    return ck.result()  # (no ck.faults: both games of the lane's bottle end with the host's exit)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (TimeoutError, OSError) as e:
        print(f'  FAIL a game stopped answering its console ({e!r})', flush=True)
        print('RESULT: FAIL', flush=True)
        sys.exit(1)
