#!/usr/bin/env python3
"""SporeKit's own tools never freeze the game (wave 1, one game of the lane):

  stack      BUG-0050: `stack` while the main thread holds the heap locks (as when it is suspended
             inside malloc): the answer comes when the main thread lets go, frames go on
  hangdump   BUG-0050: the same for `hangdump` (the watchdog's dump)
  cheat      BUG-0065: `sk shot`, `sk wait.frames 2`, `sk stack` typed into the cheat console run on
             the game's own thread: refused at once, frames go on
  logfault   BUG-0115: a fault while a thread holds the log's lock: the thread goes on
  excfault   BUG-0115: a fault while a thread holds the exception logger's lock: the same
  hookchurn  BUG-0132: a hook is made and removed 200 times while 3 threads run through it: no crash
             (the removal path of MinHook: `kit.fix hookpark 0` first)
  hookrace   BUG-0202: the same, and the hook is also made while the threads run through the function
  hooknew    BUG-0398: 200 functions never hooked before are hooked while 3 threads call them: no
             thread reaches the detour before the hook's original pointer is set (the guest's main
             thread called address 0 when the first injected input hooked GetAsyncKeyState)

  --old      switch each case's fix off first (`kit.fix <name> 0`): every case must FAIL then
  SPORE_FIX_OFF=<kit fix>[,...]  the same for the cases of those fixes only (the lab's twin runs,
             e.g. SPORE_FIX_OFF=hookorig with `--case hooknew`)
  --case a,b only these cases

  SPORE_LANE=3 python3 tools/tests/kit_freeze.py [--old] [--case stack,cheat]
"""
import glob
import os
import re
import socket
import sys
import time

import lane
from twoinst import Checks, wait_up
from sporectl import Console  # noqa: E402 (twoinst puts tools/ on the path)

GAME = lane.game_dir()
LOGDIR = os.path.join(GAME, 'SporebinEP1', 'mods', 'logs')
FIX = {'stack': 'stackdump', 'hangdump': 'stackdump', 'cheat': 'cheatwait', 'logfault': 'reentry',
       'excfault': 'reentry', 'hookchurn': 'hookkeep', 'hookrace': 'hookpark',
       'hooknew': 'hookorig'}
CASES = list(FIX)


def start_host(ck):
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.run_host()
    ck(wait_up(lane.HOST, 180), 'the game starts')
    c = Console(lane.HOST)
    for _ in range(120):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 60 60000')
    return c


def timed(line, timeout):
    """(answer or None, seconds) on a connection of its own (a call that never answers is dropped)"""
    c = Console(lane.HOST, timeout=timeout)
    t0 = time.time()
    try:
        r = c.call(line)
    except (OSError, ValueError, socket.timeout):
        r = None
    return r, time.time() - t0


def frames_go_on():
    a, _ = timed('frame', 10)
    time.sleep(1.5)
    b, _ = timed('frame', 10)
    if not a or not b:
        return False, 'no answer'
    fa = int(re.search(r'frame=(\d+)', a['out']).group(1))
    fb = int(re.search(r'frame=(\d+)', b['out']).group(1))
    return fb > fa, f'{fa} -> {fb}'


def crash_files():
    return set(glob.glob(os.path.join(LOGDIR, 'crash-*.txt')))


def run_case(ck, case):
    crashes = crash_files()
    if case in ('stack', 'hangdump'):
        r, dt = timed(f'kit.test.stackheap {case} 3000', 25)
        print(f'  {case}: {r["out"].strip() if r else "no answer"} ({dt:.1f} s)')
        ck(r is not None and dt < 15, f'{case} answers while the main thread holds the heap ({dt:.1f} s)')
    elif case == 'cheat':
        for line in ('sk shot', 'sk wait.frames 2', 'sk stack', 'sk hangdump'):
            r, dt = timed(f'cheat {line}', 25)
            print(f'  cheat {line}: {(r["out"].strip()[:100] if r else "no answer")} ({dt:.1f} s)')
            ck(r is not None and dt < 3, f'the cheat console "{line}" comes back at once ({dt:.1f} s)')
            if r is None:
                break
    elif case in ('logfault', 'excfault'):
        r, dt = timed(f'kit.test.{case}', 15)
        print(f'  {case}: {r["out"].strip() if r else "no answer"} ({dt:.1f} s)')
        ck(r is not None and 'goes on' in r['out'], f'a fault inside the lock of {case[:3]}: the thread goes on')
    elif case == 'hookchurn':
        timed('kit.fix hookpark 0', 10)  # MinHook's own removal is what BUG-0132 is about
        r, dt = timed('kit.test.hookchurn 200 3', 60)
        timed('kit.fix hookpark 1', 10)
        print(f'  hookchurn: {r["out"].strip() if r else "no answer"} ({dt:.1f} s)')
        ck(r is not None and 'rounds' in r['out'], 'hook churn finishes')
    elif case == 'hookrace':
        r, dt = timed('kit.test.hookchurn 200 3 nogate', 60)
        print(f'  hookchurn: {r["out"].strip() if r else "no answer"} ({dt:.1f} s)')
        ck(r is not None and 'rounds' in r['out'], 'hook churn finishes')
    elif case == 'hooknew':
        r, dt = timed('kit.test.hooknew 200 3', 60)
        print(f'  hooknew: {r["out"].strip() if r else "no answer"} ({dt:.1f} s)')
        m = re.search(r'null original (\d+) times', r['out']) if r else None
        ck(m is not None and re.search(r'\((\d+) hooked\)', r['out']).group(1) == '200',
           'all 200 new hooks made')
        ck(m is not None and m.group(1) == '0',
           f'no detour ran before its original was set ({m.group(1) if m else "?"} times; the game calls address 0 then)')
    ok, how = frames_go_on()
    ck(ok, f'after {case}: frames go on ({how})')
    time.sleep(1)
    new = crash_files() - crashes
    ck(not new, f'after {case}: no crash report ({[os.path.basename(p) for p in new]})')
    return ok and not new


def main():
    ck = Checks()
    old = '--old' in sys.argv
    off = {n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',') if n.strip()}
    cases = CASES
    if '--case' in sys.argv:
        cases = sys.argv[sys.argv.index('--case') + 1].split(',')
    c = start_host(ck)
    res = {}
    for case in cases:
        case_old = old or FIX[case] in off
        print(f'== {case}' + (' (fix off)' if case_old else ''))
        fx = timed(f'kit.fix {FIX[case]} {0 if case_old else 1}', 10)[0]
        ck(fx is not None and fx['out'].strip().endswith(str(0 if case_old else 1)), f'kit.fix {FIX[case]}')
        if FIX[case] in off:
            print(f'  SPORE_FIX_OFF: {FIX[case]} off', flush=True)
        before = ck.ok
        ck.ok = True
        alive = run_case(ck, case)
        res[case] = ck.ok
        ck.ok &= before
        if not alive:  # frozen or dead: a fresh game for the next case
            c = start_host(ck)
    print('cases:', ', '.join(f'{k}={"PASS" if v else "FAIL"}' for k, v in res.items()))
    lane.sh('kill')
    return ck.result()  # with --old: expected FAIL in every case


if __name__ == '__main__':
    sys.exit(main())
