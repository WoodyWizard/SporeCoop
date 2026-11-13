#!/usr/bin/env python3
"""BUG-0521: the game's own start fails (App::cSporeApp::Init returns 0), one game of the lane.

A player's game (GitHub #193) ended right after the start: cSporeApp::Init returned 0 and the game
called ExitProcess(0). No fault, no error code: SporeKit wrote nothing and the player was not told why.

The test makes one start fail the same way (mods\\test-initfail-<instance>, read and removed by
SporeKit's Init hook; "off" in it: the switch kit.fix initfail off from the start):
  1. the game ends by itself
  2. its log says the game's own start failed and what to check
  3. a report archive was written and the problem is marked (last-problem.txt: "could not start")
  4. the next start names it
  5. SK_EV_POST_INIT ("main menu ready") is not fired after the failed Init (BUG-1021)

  kit_initfail.py [--old]   (--old or SPORE_FIX_OFF=initfail: the switch off, as 0.7.3.6: no report, no mark, FAILS)
  SPORE_FIX_OFF=kit.postinit: SK_EV_POST_INIT fired anyway, as before 0.7.14: check 5 FAILS"""
import glob
import os
import sys
import time

import lane
from twoinst import Checks, wait_up
from sporectl import Console  # (after twoinst: it puts tools/ on the path)

GAME = lane.game_dir()
MODS = os.path.join(GAME, 'SporebinEP1', 'mods')
LOGDIR = os.path.join(MODS, 'logs')


def logname():
    return os.path.join(LOGDIR, f'sporekit.{lane.HOST}.log' if lane.HOST > 1 else 'sporekit.log')


def alive():
    try:
        Console(lane.HOST, timeout=3).call('game.mode')
        return True
    except (OSError, ValueError):
        return False


def main():
    ck = Checks()
    fix_off = os.environ.get('SPORE_FIX_OFF', '').split(',')
    old = '--old' in sys.argv or 'initfail' in fix_off
    postinit_old = 'kit.postinit' in fix_off  # BUG-1021 twin
    if postinit_old:
        print('  SPORE_FIX_OFF: kit.postinit off (in the marker file: from the start)', flush=True)
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    mark = os.path.join(LOGDIR, 'last-problem.txt')
    if os.path.exists(mark):
        os.remove(mark)
    zips0 = set(glob.glob(os.path.join(LOGDIR, 'SporeCoop-report-*.zip')))
    with open(os.path.join(MODS, f'test-initfail-{lane.HOST}'), 'w') as f:
        f.write(('off' if old else 'on') + (' postinit' if postinit_old else ''))
    t0 = time.time()
    lane.run_host()
    started = gone = False
    while time.time() - t0 < 180 and not gone:
        time.sleep(1)
        log = open(logname(), errors='replace').read() if os.path.exists(logname()) else ''
        started = started or (os.path.getmtime(logname()) > t0 and 'App::cSporeApp::Init' in log)
        gone = started and not alive()
    ck(started, 'the game starts and its Init returns')
    ck(gone, 'the game ends by itself after its start failed')
    time.sleep(3)
    log = open(logname(), errors='replace').read()
    ck('tests: this start is made to fail' in log, 'the start was made to fail')
    ck("the game's own start failed (App::cSporeApp::Init returned 0)" in log and 'verify the game files' in log,
       'the log says the game\'s own start failed and what to check')
    # BUG-1021: no "main menu ready" for subscribers of a game that did not start
    ck('SK_EV_POST_INIT not fired' in log and 'SK_EV_POST_INIT fired' not in log,
       'SK_EV_POST_INIT is not fired after the failed Init')
    zips = set(glob.glob(os.path.join(LOGDIR, 'SporeCoop-report-*.zip'))) - zips0
    ck(zips, f'a report archive was written ({[os.path.basename(z) for z in zips]})')
    told = open(mark, errors='replace').read() if os.path.exists(mark) else ''
    ck(told.startswith('could not start'), f'the problem is marked for the next start ({told.splitlines()[:1]})')
    lane.sh('kill')
    time.sleep(2)
    lane.run_host()
    ck(wait_up(lane.HOST), 'the game starts again')
    time.sleep(5)
    log = open(logname(), errors='replace').read()
    ck('the previous run had a problem: could not start' in log, 'the next start names the failed start')
    lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
