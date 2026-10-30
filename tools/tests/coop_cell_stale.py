#!/usr/bin/env python3
"""Cell: a group member does not chase a dead cell copied from its leader (BUG-0306).

Seen in the 4-player campaign (lab 20261030-104149 campaign#2, main 813f8cd): the host faulted
8616 times at IsDamageable+0x9 (0xE52949, `[0+0x108]`) from the chase step 0xE79C30 of one NPC.
PickTargets 0xE70CF0 runs after the liveness check of the target timers 0xE70E30 and makes a
group member copy its leader's chase target unchecked; a leader whose target died and that has
not checked it yet hands the follower a dead index, the chase step gets NULL for it. Wine
swallows the fault and aborts the frame, so the leader never runs its check again and the
follower faults at every frame.

`coop.pool staletarget 40` (host): the fixture's groups are clusters without AI, whose leader never
runs the behaviours (as a leader that skips its check); up to 40 AI cells join such a group and the
leader is given a dead chase target (its own slot with another generation).

1. AI cells follow such a leader.
2. Within 10 s the host logs no new caught fault (`exc`), and its game still draws.
SPORE_FIX_OFF=cell.pick_guard must FAIL (faults at 0x00E52949, one per frame).

  SPORE_LANE=2 python3 tools/tests/coop_cell_stale.py
"""
import re
import time

from twoinst import Checks, start


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def frame(con):
    m = re.search(r'frame=(\d+)', con.call('frame').get('out', ''))
    return int(m.group(1)) if m else -1


def main():
    ck = Checks()
    host, guest = start('cell')
    time.sleep(5)
    out, t0 = '', time.time()
    while True:  # AI cells near the host (a fresh pool may have few): wait for some
        out = host.call('coop.pool staletarget 40')['out'].strip()
        print('  host:', out, flush=True)
        m = re.search(r'(\d+) followers', out)
        if (m and int(m.group(1)) > 0) or time.time() - t0 > 60:
            break
        time.sleep(5)
    f0, fr0 = faults(host), frame(host)  # from here: the faults of the stale copies only
    ck(m and int(m.group(1)) > 0, f'1. AI cells follow a leader holding a dead target ({out})')
    time.sleep(10)
    f1, fr1 = faults(host), frame(host)
    stale = re.search(r'stale_targets=(\d+)', host.call('coop.pool')['out'])
    print(f'  host: faults {f0} -> {f1}, frames {fr0} -> {fr1}, {stale.group(0) if stale else "stale_targets=?"}', flush=True)
    exc = host.call('exc')['out']
    ck(f1 == f0, f'2. no new caught fault on the host ({f0} -> {f1}' + (', at IsDamageable 0x00E52949' if '00E52949' in exc else '') + ')')
    ck(fr1 > fr0 + 100, f'2. the host still draws ({fr0} -> {fr1})')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
