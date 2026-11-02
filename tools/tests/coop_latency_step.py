#!/usr/bin/env python3
"""A latency step while the partner walks: what the other player sees (BUG-0133 in the game; its own test
was a unit test of the interpolation buffer only).

Players' connections change their latency within a session (bufferbloat while somebody uploads, a mobile
cell change). BUG-0133: after the one-way latency rose by ~300 ms the interpolation buffer rendered past its
newest state for ~50 s, every partner puppet moved in 20 Hz steps (extrapolated) all that time. This test
walks one player by real input (coop_puppet_walk.walk: WASD holds, clicks on the ground, turns back to
the other) for --secs seconds (62) while the guest's network proxy (netem, SPORE_NET) changes its one-way delay
--low -> --high -> --low ms (30 -> 300 -> 30) at --rise and --fall seconds (10, 42) into the walk, in both directions (the guest
watches the host, the host watches the guest). The observer's game is sampled every frame (coop.track:
the drawn position of the puppet, before the mod's driving).

Check per direction: in 1 s buckets of the walk, a bucket is BAD when the drawn puppet moves in steps
(of its windows of >= 4 frames while the walker's path moves, under 60 % at 0.6-1.4x the path's speed or
over 20 % standing) or jumps (a frame step over 3x the walker's top speed x the frame time, >= 0.75 m);
GOOD when it walks smoothly. The longest stretch of bad buckets with no good bucket in between must be
<= --max-bad seconds (5 s): a step of the latency may cost a moment, not the ~50 s of BUG-0133. At least
--min-high seconds of the high-latency phase must have been measured (the walker walked).

And against the truth (both games run on this machine, both tracks start together): per walking second of the
high phase, the drawn puppet's median distance from the walker's own position `lag` s earlier, at the lag that
fits best (0-1 s). At most 25 % of those seconds may be over 0.15 m. The game's creature driver hides BUG-0133's
extrapolation from the step check (the drawn puppet stays smooth); the extrapolated puppet is off the real path
at turns and stops instead: fix off 0.15-0.27 m in 13 of the high phase's seconds, best lag 0.22-0.30 s (it should
follow the latency: 0.40-0.42 s with the fix); fix on 0.01-0.13 m (lab 20261101-144311 / 20261101-142943).
The error share alone did not fail a second fix-off run (10 % / 22 %, lab 20261101-144849); the lag does: the
median best lag from 3 s after the rise must be >= the one-way delay (0.30 s at 300 ms): what arrived is
that old, a smaller lag is a guess. Fix off 0.18-0.26 s in the four directions of both runs, fix on 0.36-0.42 s.

  SPORE_LANE=9 SPORE_NET="delay=30,jitter=5" python3 tools/tests/coop_latency_step.py [--seed N]
  SPORE_FIX_OFF=net.offset_window fails (the steps last until the offset crept up: tens of seconds)
"""
import argparse
import json
import math
import os
import random
import re
import threading
import time

import lane
import netem
from coop_puppet_walk import (TP_FACTOR, TP_MIN, WIN_FRAMES, Watch, beside, dump, pct, settle, walk)
from twoinst import Checks, shot, start

GOOD_MATCHED, BAD_MATCHED, BAD_STAND = 0.6, 0.6, 0.2


class Stepper(threading.Thread):
    """sets the proxy's base one-way delay at given seconds after start(): [(at, ms)]"""

    def __init__(self, proxy, plan):
        super().__init__(daemon=True)
        self.proxy, self.plan, self.t0, self.done = proxy, plan, None, []

    def run(self):
        self.t0 = time.time()
        for at, ms in self.plan:
            wait = self.t0 + at - time.time()
            if wait > 0:
                time.sleep(wait)
            self.proxy.p['delay'] = float(ms)
            self.done.append((round(time.time() - self.t0, 2), ms))


def buckets(obs_rows, walker_rows):
    """per 1 s of the observer's track: (windows, matched share, stand share, jumps)"""
    own = [r for r in walker_rows if r['who'] == 255]
    top = pct([r['gv'] for r in own if r['gv'] > 0.3], 0.95) or 5.0
    pup = [r for r in obs_rows if r['who'] != 255]
    out = {}
    prev_dt = 0.0
    for a, b in zip(pup, pup[1:]):
        dt = b['t'] - a['t']
        allow_dt, prev_dt = max(dt, prev_dt), dt
        if dt <= 0 or dt > 0.25 or b['rv'] < 0.5 or not (b['flags'] & 1):
            continue
        step = math.dist(a['d'], b['d'])
        if step > max(TP_MIN, TP_FACTOR * top * allow_dt):
            out.setdefault(int(b['t']), {'w': [], 'jumps': []})['jumps'].append((round(b['t'], 2), round(step, 2)))
    i = 0
    while i < len(pup):
        j = i
        while j < len(pup) and (pup[j]['t'] - pup[i]['t'] < 0.1 or j - i < WIN_FRAMES):
            j += 1
        if j >= len(pup):
            break
        dt = pup[j]['t'] - pup[i]['t']
        path = math.dist(pup[i]['tgt'], pup[j]['tgt']) / dt if dt > 0 else 0
        if path > 1.0 and dt < 0.3 and (pup[j]['flags'] & 1):
            out.setdefault(int(pup[i]['t']), {'w': [], 'jumps': []})['w'].append(
                math.dist(pup[i]['d'], pup[j]['d']) / dt / path)
        i = j
    res = {}
    for sec, v in out.items():
        w = v['w']
        res[sec] = dict(n=len(w), matched=sum(1 for x in w if 0.6 <= x <= 1.4) / len(w) if w else 0.0,
                        stand=sum(1 for x in w if x < 0.3) / len(w) if w else 0.0, jumps=v['jumps'])
    return res, top


def verdict(b):
    """'bad', 'good' or None (too little walking in this second to say)"""
    if b['jumps']:
        return 'bad'
    if b['n'] < 3:
        return None
    if b['matched'] < BAD_MATCHED or b['stand'] > BAD_STAND:
        return 'bad'
    return 'good' if b['matched'] >= GOOD_MATCHED else None


def longest_bad(bk):
    """the longest time from a bad second to a later bad second with no good second between (s), its start"""
    best, start_s, run_start, last_bad = 0, None, None, None
    for sec in sorted(bk):
        v = verdict(bk[sec])
        if v == 'good':
            run_start = last_bad = None
        elif v == 'bad':
            if run_start is None:
                run_start = sec
            last_bad = sec
            if last_bad - run_start + 1 > best:
                best, start_s = last_bad - run_start + 1, run_start
    return best, start_s


def truth(obs_rows, walker_rows):
    """per 1 s of the observer's track while the walker walks (its own game speed > 1 m/s): (best lag s, median
    distance m) between the drawn puppet and the walker's own position `lag` s earlier, the lag (0-1 s) that fits
    that second best. Both games run on this machine and both tracks start within a few ms (coop.track start on
    each), so the two track clocks are one: what the observer draws against where the walker really was. A
    puppet rendered past the newest state (extrapolated, BUG-0133) is off the real path at every turn and stop"""
    import bisect
    own = [r for r in walker_rows if r['who'] == 255]
    ts = [r['t'] for r in own]

    def at(t):
        i = bisect.bisect_left(ts, t)
        if i <= 0 or i >= len(ts):
            return None, 0.0
        x, y = own[i - 1], own[i]
        f = (t - x['t']) / (y['t'] - x['t']) if y['t'] > x['t'] else 0.0
        return tuple(x['p'][k] + (y['p'][k] - x['p'][k]) * f for k in range(3)), max(x['gv'], y['gv'])
    by = {}
    for r in obs_rows:
        if r['who'] != 255:
            by.setdefault(int(r['t']), []).append(r)
    out = {}
    for sec, rows in by.items():
        sp = [at(r['t'])[1] for r in rows]
        if not sp or sorted(sp)[len(sp) // 2] < 1.0:
            continue  # the walker stands (or turns on the spot) in this second
        best = None
        for k in range(50):
            lag = k * 0.02
            es = sorted(math.dist(p, r['d']) for r in rows for p in [at(r['t'] - lag)[0]] if p is not None)
            if es and (best is None or es[len(es) // 2] < best[1]):
                best = (lag, es[len(es) // 2])
        if best:
            out[sec] = best
    return out


ERR_M, ERR_SHARE = 0.15, 0.25


def direction(walker, observer, wname, oname, rng, a, ck):
    proxy = netem.proxy()
    proxy.p['delay'] = float(a.low)
    beside(walker, observer)
    watch = Watch(observer)
    watch.start()
    time.sleep(2.0)
    for c in (walker, observer):
        c.call('coop.track start')
    t_track = time.time()
    time.sleep(0.5)
    st = Stepper(proxy, [(a.rise, a.high), (a.fall, a.low)])
    st.start()
    t_walk = time.time()
    log = walk(walker, rng, a.secs)
    watch.stop.set()
    watch.join()
    st.join(5)
    proxy.p['delay'] = float(a.low)
    time.sleep(1.0)
    shot(observer, f'latency_step_{oname}_sees_{wname}')
    for c in (walker, observer):
        c.call('coop.track stop')
    obs, wal = dump(observer), dump(walker)
    path = os.path.join(lane.out_dir('track'), f'latency_step_{oname}_sees_{wname}_{time.strftime("%Y%m%d-%H%M%S")}.json')
    with open(path, 'w') as f:
        json.dump({'observer': obs, 'walker': wal, 'steps': st.done, 'walk_start': t_walk - t_track}, f)
    print(f'  per-frame samples: {path}', flush=True)
    print(f'  {wname} walked: {"; ".join(log)}', flush=True)
    off = t_walk - t_track  # track time of the walk's start (both on this machine's wall clock, ~0.1 s)
    print(f'  latency steps (s into the walk, one-way ms): {st.done}; {proxy.describe()}', flush=True)
    bk, top = buckets(obs, wal)
    hi0, hi1 = off + a.rise, off + a.fall
    line = []
    for sec in sorted(bk):
        v = verdict(bk[sec])
        mark = '^' if hi0 <= sec < hi1 else ' '
        line.append(f'{sec - off:5.0f}{mark}{"B" if v == "bad" else "g" if v == "good" else "."}'
                    f'{bk[sec]["matched"]:.0%}/{bk[sec]["stand"]:.0%}/{len(bk[sec]["jumps"])}')
    print(f'  {oname} watches {wname}, per second (s into the walk, ^ = {a.high} ms, B bad g good . unknown, '
          f'matched/stand/jumps), walker top speed {top:.2f} m/s:', flush=True)
    for k in range(0, len(line), 8):
        print('    ' + '  '.join(line[k:k + 8]), flush=True)
    tr = truth(obs, wal)
    hi = {sec: v for sec, v in tr.items() if hi0 + 1 <= sec < hi1}
    off_path = [sec for sec, v in hi.items() if v[1] > ERR_M]
    print(f'  {oname} watches {wname}, drawn vs the walker\'s real path per walking second (s into the walk: lag s / '
          f'median m): ' + ' '.join(f'{sec - off:.0f}{"^" if hi0 <= sec < hi1 else ""}:{v[0]:.2f}/{v[1]:.2f}'
                                   for sec, v in sorted(tr.items())), flush=True)
    share = len(off_path) / len(hi) if hi else 1.0
    ck(len(hi) >= a.min_high and share <= ERR_SHARE,
       f'{oname} watches {wname}: at {a.high} ms the puppet is drawn on the walker\'s real path (median <= {ERR_M} m '
       f'at the best lag) in all but {share:.0%} <= {ERR_SHARE:.0%} of {len(hi)} walking seconds '
       f'(off: {[round(sec - off) for sec in sorted(off_path)]})')
    # the puppet shows where the walker WAS: what arrived is at least the one-way delay old. A best lag under it
    # means positions the observer's game guessed (rendered past the newest state, BUG-0133), wrong at every
    # turn and stop. From 3 s after the rise (the offset follows a rise at 0.25 s/s)
    lags = sorted(v[0] for sec, v in tr.items() if hi0 + 3 <= sec < hi1)
    med = lags[len(lags) // 2] if lags else 0.0
    want = a.high / 1000
    ck(len(lags) >= 5 and med >= want, f'{oname} watches {wname}: at {a.high} ms the drawn puppet is where the walker '
       f'was, not a guess ahead of the newest state: median lag {med:.2f} s >= {want:.2f} s over {len(lags)} walking '
       f'seconds')
    worst, at = longest_bad(bk)
    high_meas = sum(1 for sec in bk if hi0 <= sec < hi1 and verdict(bk[sec]) is not None)
    after = [sec for sec in bk if hi0 <= sec and verdict(bk[sec]) is not None]
    ck(high_meas >= a.min_high, f'{oname} watches {wname}: the walk was measured in {high_meas} s of the '
       f'{a.fall - a.rise:.0f} s at {a.high} ms (>= {a.min_high})')
    ck(len(after) >= a.min_high, f'{oname} watches {wname}: and in {len(after)} s from the rise on')
    ck(worst <= a.max_bad, f'{oname} watches {wname}: the puppet moves in steps or jumps for at most {a.max_bad:.0f} s '
       f'in a row ({worst} s' + (f' from {at - off:.0f} s into the walk' if at is not None else '') + ')')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default='both', choices=('host', 'guest', 'both'), help='who walks')
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--secs', type=float, default=62, help='length of each walk')
    ap.add_argument('--low', type=float, default=30, help='one-way delay before and after the step (ms)')
    ap.add_argument('--high', type=float, default=300, help='one-way delay during the step (ms)')
    ap.add_argument('--rise', type=float, default=10, help='s into the walk: low -> high')
    ap.add_argument('--fall', type=float, default=42, help='s into the walk: high -> low')
    ap.add_argument('--max-bad', type=float, default=5, help='s of steps / jumps in a row allowed')
    ap.add_argument('--min-high', type=int, default=8, help='s of the high phase that must be measured')
    a = ap.parse_args()
    if not os.environ.get('SPORE_NET'):
        print('RESULT: FAIL (needs SPORE_NET: the guest\'s proxy steps its delay)')
        return 1
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  seed {seed}; network {os.environ["SPORE_NET"]}; steps {a.low} -> {a.high} ms at {a.rise} s -> '
          f'{a.low} ms at {a.fall} s', flush=True)
    rng = random.Random(seed)
    host, guest = start('creature')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 30:
        if re.search(r'puppet pos=', host.call('coop.status')['out']) and \
                re.search(r'puppet pos=', guest.call('coop.status')['out']):
            break
        time.sleep(1)
    settle((host, guest))
    time.sleep(1)
    if a.dir in ('host', 'both'):
        direction(host, guest, 'host', 'guest', rng, a, ck)
    if a.dir in ('guest', 'both'):
        direction(guest, host, 'guest', 'host', rng, a, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
