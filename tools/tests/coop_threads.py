#!/usr/bin/env python3
"""A14 (BUG-1130, docs/01-architecture.md#arch-threads): the off-main game call counter, one game of the lane.

game.cpp installs skg::call_probe: every game call through the skg helpers made off the game's main thread is counted
per call site and the first one of a site writes a log line. The check:
  1. `coop.threads` answers with the main thread's id and the probe on, and no game call was made off the main
     thread from the boot to the galaxy menu (calls=0)
  2. `coop.threads probe` tells the probe of one call from the console's thread (off the main thread) and queues one
     on the main thread: the table counts exactly the off-main one (calls=1, a coop.dll+0x... site, game 0x401000),
     and the log has its line
  3. `coop.threads reset` empties the table
and no fault was logged.

  coop_threads.py [--old]   (--old or SPORE_THREADS_OLD=1: `coop.threads off` first, the probe is not installed, as before A14: FAILS)"""
import os
import re
import sys
import time

import lane
from diag_check import logname
from twoinst import Checks, wait_up
from sporectl import Console  # (after twoinst: it puts tools/ on the path)

LINE = 'threads: game call 0x401000 from coop.dll+0x'


def calls(out):
    m = re.search(r'off_main calls=(\d+) sites=(\d+)', out)
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def main():
    ck = Checks()
    old = '--old' in sys.argv or os.environ.get('SPORE_THREADS_OLD') == '1'
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
            print('  --old: ' + c.call('coop.threads off')['out'].strip().splitlines()[0], flush=True)
        st = c.call('coop.threads')['out']
        print('  ' + st.strip().replace('\n', '\n  '), flush=True)
        m = re.search(r'main_thread=(\d+) probe=(\w+)', st)
        ck(bool(m) and int(m.group(1)) != 0 and m.group(2) == 'on',
           f'the main thread is known and the probe is on ({m.group(0) if m else st.strip()[:80]})')
        n, _ = calls(st)
        ck(n == 0, f'no game call off the main thread from the boot to the galaxy menu (calls={n})')
        before = open(logname(), errors='replace').read().count(LINE)
        c.call('coop.threads probe')
        c.call('wait.frames 5 10000')  # the main thread's half ran
        st = c.call('coop.threads')['out']
        print('  after probe: ' + st.strip().replace('\n', '\n  '), flush=True)
        n2, sites = calls(st)
        ck(n2 == (n or 0) + 1 and sites is not None and sites >= 1 and '-> game 0x401000 calls=1' in st,
           f'the console thread\'s call is counted once, the main thread\'s not (calls={n2}, sites={sites})')
        after = open(logname(), errors='replace').read().count(LINE)
        ck(after == before + 1, f'the first call of the site wrote its log line ({after - before} new)')
        st = c.call('coop.threads reset')['out']
        ck(calls(st)[0] == 0, f'coop.threads reset empties the table ({st.strip().splitlines()[-1][:60]})')
        ck.faults(host=c)
    finally:
        lane.sh('kill', check=False)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
