#!/usr/bin/env python3
"""Tribe: blows on the co-op epic while and after it dies keep both games drawing (BUG-0171).

coop_tribe_play.py on 2026-10-22 (lane 2): the host stopped drawing during `coop.tribe epic hit` (monitor: FROZE),
once in 15 runs. The loop then hit the epic with the host's member and the guest's member in turn and stopped only
on a reply starting at hp 0, so the blow after the lethal one (and one more) went through the epic's TakeDamage at
hp 0, as it lay dying (ba9dc46 made the command stop at a dead victim: tribe.hit_dead). Here, rounds of: a new epic
next to a member, both members' blows until it is at 0, then 20 more blows from both at once (the old loop's
blows, many times over), then the frames of both games for 10 s and the epic's end (+food, the epic gone).

SPORE_FIX_OFF=tribe.hit_dead: the blows at hp 0 go through as before ba9dc46.

  SPORE_LANE=9 python3 tools/tests/coop_tribe_epic_dying.py [--rounds 3]
"""
import argparse
import re
import time

import twoinst
from coop_tribe import members
from twoinst import Checks, shot, start


def frames_go_on(con, secs):
    f0 = twoinst.host_frame(con)
    time.sleep(secs)
    return twoinst.host_frame(con) > f0 + 10


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=3)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    t0 = time.time()
    while True:
        hm = members(host)
        if any(v[0] == 1 for v in hm.values()) or time.time() - t0 > 90:
            break
        time.sleep(3)
    host_m = [i for i, v in hm.items() if v[0] == 0 and v[3] == 'player']
    guest_m = [i for i, v in hm.items() if v[0] == 1 and v[3] == 'player']
    print('  host members', host_m, 'guest members', guest_m, flush=True)
    if not ck(host_m and guest_m, 'both players have members'):
        return ck.result()
    for rnd in range(1, a.rounds + 1):
        r = host.call('coop.tribe epic')['out'].strip()
        print(f'  #{rnd}: {r[:120]}', flush=True)
        if not ck('spawned' in r and 'not spawned' not in r, f'#{rnd}: the epic is spawned'):
            break
        print(f'  #{rnd}:', host.call(f'coop.tribe epic near {host_m[-1]}')['out'].strip()[:100], flush=True)
        time.sleep(2)
        replies = []
        for _ in range(60):
            r1 = host.call(f'coop.tribe epic hit {host_m[-1]} 40')['out'].strip()
            r2 = host.call(f'coop.tribe epic hit {guest_m[-1]} 40')['out'].strip()
            replies += [r1, r2]
            if '-> 0.00' in r1 + r2 or 'dead' in r1 + r2 or 'no epic' in r1 + r2:
                break
            time.sleep(0.1)
        # the blows after the lethal one: as the old loop's, many times over, no wait between them
        after = []
        for _ in range(10):
            after.append(host.call(f'coop.tribe epic hit {host_m[-1]} 40')['out'].strip())
            after.append(host.call(f'coop.tribe epic hit {guest_m[-1]} 40')['out'].strip())
        print(f'  #{rnd}: {len(replies)} blows to 0 (last {replies[-1] if replies else None}); after: '
              f'{sum(1 for x in after if "->" in x and "dead" not in x)} landed, {after[:3]}', flush=True)
        hd = frames_go_on(host, 10)
        gd = frames_go_on(guest, 3)
        ck(hd, f'#{rnd}: the host keeps drawing after the epic\'s death')
        ck(gd, f'#{rnd}: the guest keeps drawing')
        st = host.call('coop.status')['out']
        print(f'  #{rnd}: ' + ' | '.join(ln.strip()[:120] for ln in st.splitlines() if 'epic' in ln.lower() or 'down:' in ln)[:300],
              flush=True)
        if not (hd and gd):
            shot(host, f'epic_dying_host{rnd}.png')
            break
        time.sleep(3)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (TimeoutError, OSError) as e:
        print(f'  FAIL a game stopped answering its console ({e!r})', flush=True)
        print('RESULT: FAIL', flush=True)
        raise SystemExit(1)
