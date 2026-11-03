#!/usr/bin/env python3
"""The partner's creature jumps where its own game put it: out of the egg and at the respawn (BUG-0499).

play_creature failed `sync p*: never more than 6 m apart` on single samples right after a death: the
puppet 11-23 m (once 177 m) from where the partner's own game had it, puppet speed 0. The partner's own
game moves its creature at once twice after a death: the respawn at the nest (100+ m) and, ~10 s later,
the hatchling stepping out of its egg (5-11 m in one frame). The states were blended across those steps
and the puppet walked after the target.

Each player dies (`coop.team die`, the death caption answered with Enter like a player); the other game
records its puppet frame by frame (`coop.track`). For every jump of the target (the partner's state
moves more than JUMP m within 0.25 s) the time from its start until the drawn puppet is within NEAR m of
where the target landed must be at most CATCH s; and when the puppet shows again after the death, its
first drawn frames are within NEAR m of the target.

SPORE_FIX_OFF=creature.puppet_jump: blended and walked again (the catch takes 0.5-1 s: the test fails).
"""
import math
import re
import time

import uifind
from coop_puppet_walk import dump
from twoinst import Checks, start

JUMP = 2.0    # m: a jump of the partner's own creature ...
FAST = 25.0   # m/s: ... made of frames faster than any creature walks or charges (the fixture's top: 4.75)
NEAR = 1.0    # m
CATCH = 0.15  # s from the jump's start (0.7.3.6: the target slid for 0.1 s and the puppet walked, 0.30 s for 6 m)


def die(con, name, ck):
    out = ''
    for _ in range(3):
        out = con.call('coop.team die')['out']
        if 'lethal' in out:
            break
        time.sleep(1)
    ck('lethal' in out, f'{name} takes a lethal hit')
    t0 = time.time()
    while time.time() - t0 < 40:
        if uifind.shown(con, 'You were killed'):
            time.sleep(1.0)
            con.call('key enter')
            return True
        time.sleep(0.5)
    return False


def jumps(rows):
    """[(t_start, t_land, landed pos, index of the landing row)]: runs of frames in which the target moves
    faster than FAST m/s, JUMP m or more in all (rows of one time stamp once)"""
    out, i = [], 1
    while i < len(rows):
        def fast(k):
            dt = rows[k]['t'] - rows[k - 1]['t']
            return dt > 0 and math.dist(rows[k]['tgt'], rows[k - 1]['tgt']) / dt > FAST
        if fast(i):
            k = i
            while k + 1 < len(rows) and fast(k + 1):
                k += 1
            if math.dist(rows[i - 1]['tgt'], rows[k]['tgt']) >= JUMP:
                out.append((rows[i - 1]['t'], rows[k]['t'], rows[k]['tgt'], k, math.dist(rows[i - 1]['tgt'], rows[k]['tgt'])))
            i = k + 1
            continue
        i += 1
    return out


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    all_jumps = []
    for name, me, viewer, pid in (('guest', guest, host, 1), ('host', host, guest, 0)):
        viewer.call('coop.track stop')
        viewer.call('coop.track start')
        ck(die(me, name, ck), f'{name}: the death caption came and was answered')
        time.sleep(28)  # the respawn, the hatchling waits ~10 s and steps out of its egg
        viewer.call('coop.track stop')
        rows, seen_t = [], set()
        for r in dump(viewer):
            if r['who'] == pid and r['t'] not in seen_t:
                seen_t.add(r['t'])
                rows.append(r)
        print(f'  {name} died: {len(rows)} frames of its puppet on the other game', flush=True)
        js = jumps(rows)
        for t0, tl, land, k, size in js:
            caught = next((r['t'] for r in rows[k:] if math.dist(r['d'], land) <= NEAR), None)
            dt = None if caught is None else caught - t0
            print(f'  {name}: the target jumped {size:.1f} m at {t0:.2f} s (landed {tl - t0:.2f} s later); the puppet '
                  f'there after {"never" if dt is None else f"{dt:.2f} s"}', flush=True)
            ck(dt is not None and dt <= CATCH, f'{name}: the partner\'s puppet jumps with its creature '
                                               f'({"never" if dt is None else f"{dt:.2f} s"} <= {CATCH} s)')
        all_jumps.extend(js)
        # the first frames after it shows again (a gap of over 1 s in its rows: hidden while dead)
        for a, b in zip(rows, rows[1:]):
            if b['t'] - a['t'] > 1.0:
                first = rows[rows.index(b):rows.index(b) + 3]
                far = max(math.dist(r['d'], r['tgt']) for r in first[1:]) if len(first) > 1 else 0
                print(f'  {name}: shown again at {b["t"]:.2f} s, the next frames {far:.2f} m off the target', flush=True)
                ck(far <= NEAR, f'{name}: shown again where its creature is ({far:.2f} m)')
        time.sleep(3)
    # (the guest's hatchling steps out of its egg ~10 s after the respawn; the host's waits for its family)
    ck(all_jumps, f'a creature jumped on its own game at least once (out of the egg: {len(all_jumps)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
