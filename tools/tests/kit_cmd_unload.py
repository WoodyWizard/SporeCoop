#!/usr/bin/env python3
"""BUG-1018: mod.unload / mod.reload of coop.dll while one of its any-thread console commands runs, one game of the
lane.

`coop.sample` runs on the console's thread (SK_CMD_ANY_THREAD) and samples the main thread for seconds. A second
connection's `mod.unload coop` (or `mod.reload coop`, what tools/lab.py reload sends) ran on the main thread and freed
the DLL under it: the console thread came back into unmapped code (or, after a reload under Wine, into the new image
at the same base). With kit.fix cmdkeep the unload waits until the command has returned.

The order is made sure: `kit.test.hang 1500` holds the main thread first, the unload is queued behind it, then
`coop.sample 4000` starts on the console thread; the main thread runs the unload ~1.2 s into the sample.
Each case (unload, then reload):
  1. the game lives on (the console answers, frames advance) and the sample is answered
  2. the unload waited for the command (the SporeKit log names the wait)
  3. coop works again (mod.load after the unload; coop.status answers)
and no fault was logged.

  kit_cmd_unload.py [--old]   (--old or SPORE_FIX_OFF=kit.cmdkeep: the switch off, the old unload: FAILS)"""
import os
import sys
import threading
import time

import lane
from diag_check import logname
from twoinst import Checks, alive, wait_up
from sporectl import Console  # (after twoinst: it puts tools/ on the path)

HANG_MS = 1500
SAMPLE_MS = 4000


def case(ck, op):
    a = Console(lane.HOST, timeout=60)
    b = Console(lane.HOST, timeout=60)
    print(f'  [{op}] ' + a.call(f'kit.test.hang {HANG_MS}')['out'].strip(), flush=True)
    res = {}

    def unload():
        try:
            res['b'] = b.call(f'mod.{op} coop')
        except Exception as e:  # noqa: BLE001 (a dead game: the check below says so)
            res['b'] = {'ok': False, 'out': f'{type(e).__name__}: {e}'}

    t = threading.Thread(target=unload, daemon=True)
    t.start()
    time.sleep(0.3)  # the unload is queued for the main thread before the sample starts
    t0 = time.time()
    try:
        s = a.call(f'coop.sample {SAMPLE_MS} 3')
    except Exception as e:  # noqa: BLE001
        s = {'ok': False, 'out': f'{type(e).__name__}: {e}'}
    took = time.time() - t0
    t.join(60)
    print(f'  [{op}] coop.sample ({took:.1f} s): {s.get("out", "").strip().splitlines()[:1]}', flush=True)
    print(f'  [{op}] mod.{op} coop: {res.get("b", {}).get("out", "").strip()[:160]}', flush=True)
    time.sleep(1)
    ok = alive(lane.HOST)
    frames = False
    if ok:
        try:
            frames = Console(lane.HOST, timeout=30).call('wait.frames 30 20000')['ok']
        except OSError:
            frames = False
    ck(ok and frames, f'[{op}] the game lives on after the {op} during the command (console, frames)')
    if not ok:
        return False
    ck(s.get('ok') is True, f'[{op}] the running coop.sample was answered')
    ck(res.get('b', {}).get('ok') is True, f'[{op}] mod.{op} coop answered ok')
    log = open(logname(), errors='replace').read()
    waits = log.count('running command(s) (BUG-1018)')
    ck(waits >= case.waits + 1, f'[{op}] the {op} waited for the running command (log lines: {waits})')
    case.waits = waits
    c = Console(lane.HOST, timeout=60)
    if op == 'unload':
        print(f'  [{op}] mod.load coop: ' + c.call('mod.load coop')['out'].strip()[:120], flush=True)
    c.call('sym.reload')
    st = c.call('coop.status')
    ck(st.get('ok') and 'offline' in st.get('out', ''), f'[{op}] coop answers again ({st.get("out", "").strip()[:60]})')
    return True


case.waits = 0


def main():
    ck = Checks()
    old = '--old' in sys.argv or 'kit.cmdkeep' in os.environ.get('SPORE_FIX_OFF', '').split(',')
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    try:
        lane.run_host()
        ck(wait_up(lane.HOST), 'the game starts')
        c = Console(lane.HOST)
        for _ in range(120):
            if 'GalaxyGameEntry' in c.call('game.mode')['out']:
                break
            time.sleep(1)
        c.call('wait.frames 60 60000')
        if old:
            print('  SPORE_FIX_OFF: kit.cmdkeep off: ' + c.call('kit.fix cmdkeep 0')['out'].strip(), flush=True)
        log = open(logname(), errors='replace').read()
        case.waits = log.count('running command(s) (BUG-1018)')
        for op in ('unload', 'reload'):
            if not case(ck, op):
                break
        if alive(lane.HOST):
            ck.faults(host=Console(lane.HOST))
    finally:
        lane.sh('kill', check=False)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
