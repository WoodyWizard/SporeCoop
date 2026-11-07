#!/usr/bin/env python3
"""The end of a long session on the 2 GB space: a full, fragmented address space gets one last big block back and
the player is told to save and restart (BUG-0631; docs/06-testing.md#testing-mem-census).

The host of the report (GOG 2024, large-address flag off) crashed in Civ after ~2 h with 133 MB free but no free
block over 12 MB: the game's allocator grows in 16 MB segments, a model's vertex buffer came back null and the game
wrote through it. core.mem_reserve keeps a 128 MB block of address space from the start (reserved, never
committed) and gives it back when the space runs low (under 300 MB free or no block of 48 MB), with a notice.

1. Two players in Creature; the host's game keeps its reserve (`coop.diag reserve`).
2. The host's address space is filled the way hours of play leave it: 16 MB blocks reserved and never freed
   (`coop.diag fill <MB>`) until under --left MB are free, so no free block is much over 16 MB.
3. Within 2.5 minutes (the check runs with the minute's perf line): the game's own address space (a VirtualQuery
   census of the host's process) has a free block of at least 100 MB again, and the host's screen shows the notice
   (a notification panel: `coop.status` lists the visible ones).
4. Both play on for --play minutes (the play_creature bots, real input): no fault, no crash.
SPORE_FIX_OFF=core.mem_reserve must FAIL (step 3: the largest free block stays under 32 MB, no notice).

  SPORE_LANE=4 python3 tools/tests/coop_mem_reserve.py [--left 260] [--play 3]
"""
import argparse
import random
import re
import sys
import time

import lane
from coop_mem_census import census
from twoinst import Checks, Monitor, start

NOTICE = 'running out of memory'
MIN_BLOCK = 100  # MB: the reserve (128) back in one piece


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--left', type=int, default=260, help='fill until under this many MB are free')
    ap.add_argument('--play', type=float, default=3)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)  # (a switch turned off by SPORE_FIX_OFF gives the reserve back within a second)
    print('  host:', host.call('coop.diag reserve')['out'].strip(), flush=True)
    c0 = census(host, 1)
    print(f'  host before: free {c0["free_mb"]} MB, largest block {c0["block_mb"]} MB', flush=True)
    t_fill = time.time()
    print('  host:', host.call(f'coop.diag fill {a.left} 16')['out'].strip(), flush=True)
    c1 = census(host, 1)
    print(f'  host filled: free {c1["free_mb"]} MB, largest block {c1["block_mb"]} MB ({time.time() - t_fill:.0f} s)',
          flush=True)
    ck(c1['free_mb'] < a.left + 40 and c1['block_mb'] < 64,
       f'2. the host\'s address space is full and fragmented ({c1["free_mb"]} MB free, largest block {c1["block_mb"]} MB)')
    seen, best, t0 = False, c1['block_mb'], time.time()
    while time.time() - t0 < 150 and not Monitor.events:
        st = host.call('coop.status')['out']
        if NOTICE in st:
            if not seen:
                line = [x for x in st.splitlines() if NOTICE in x]
                print(f'  {time.time() - t0:.0f} s: the host\'s screen shows: {line[0].strip()[:160]}', flush=True)
            seen = True
        if time.time() - t0 > 10 and int(time.time() - t0) % 10 < 2:
            c = census(host, 1)
            best = max(best, c['block_mb'])
            if seen and best >= MIN_BLOCK:
                break
        time.sleep(1.5)
    c2 = census(host, 1)
    best = max(best, c2['block_mb'])
    print(f'  host after {time.time() - t0:.0f} s: free {c2["free_mb"]} MB, largest block {c2["block_mb"]} MB | '
          + host.call('coop.diag reserve')['out'].strip(), flush=True)
    ck(best >= MIN_BLOCK, f'3. the game\'s address space has a free block of {MIN_BLOCK} MB or more again (largest {best} MB)')
    ck(seen, f'3. the host\'s screen tells the player to save and restart ("{NOTICE}")')
    # 4. play on in the full space
    import play_creature as pc
    world, sh = pc.World(lane.HOST), pc.Shared()
    sh.exit_result = None
    deadline = time.time() + a.play * 60
    s = random.randrange(1 << 30)
    players = [pc.Player('host', lane.HOST, 1, world, sh, s, deadline, None),
               pc.Player('guest', lane.GUEST, 0, world, sh, s + 1, deadline, None)]
    for p in players:
        p.start()
    for p in players:
        p.join(a.play * 60 + 120)
        if p.error:
            print(f'  note: {p.name} bot: {p.error}', flush=True)
    c3 = census(host, 1)
    print(f'  host after {a.play} min of play: free {c3["free_mb"]} MB, largest block {c3["block_mb"]} MB', flush=True)
    ck(not Monitor.events, '4. the games played on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
