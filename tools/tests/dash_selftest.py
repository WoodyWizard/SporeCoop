#!/usr/bin/env python3
"""BUG-0363 (in make test): the measures of creature_npc_dash / tribe_npc_dash on synthetic tracks of the test's zigzag.

* body_trail.same_moment (the judged measure): a copy drawn on time is ~0 m off, one 0.25 s late at 6.5 m/s is over
  the 1.2 m limit, one 1.5 m beside the path too; the q<id> rows split target and driving; locate / path_time find a
  console reading's frame and the copy's lag.
* the window measure (coop_multi.seg_dist over the host's 0.25 s path, as the test samples it) cannot see a copy that
  is late on the host's path: 0.25 s late it stays far under the limit. So its 1.2-1.4 m failures were copies off the
  host's path, not its sampling (BUG-0363's finding)."""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import body_trail as bt  # noqa: E402
import coop_multi as cm  # noqa: E402

V, R = 6.5, 500.0
PTS = [(0, 0), (3, 2), (6, -2), (9, 2), (12, 0)]  # npc.cpp dash_point: 12 m, 2 m to either side
LEGS = [math.dist(PTS[k], PTS[k + 1]) for k in range(4)]
TOT = sum(LEGS)
RUN = TOT / V
CYC = 2 * (RUN + 0.6)


def path(t, side=0.0):
    t %= CYC
    d = t * V if t < RUN else TOT if t < RUN + 0.6 else TOT - (t - RUN - 0.6) * V if t < 2 * RUN + 0.6 else 0.0
    k = 0
    while k < 3 and d > LEGS[k]:
        d -= LEGS[k]
        k += 1
    f = min(d / LEGS[k], 1.0)
    a, b = PTS[k], PTS[k + 1]
    return (a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f + side, R)


def rows(fps, secs, key, shift=0.0, side=0.0, seed=1, target=False):
    """one game's rows of body `key`: drawn at path(t - shift) (+ side m beside it); target: q rows (the frame before)"""
    rng, t, out, prev = random.Random(seed), 0.0, [], None
    while t < secs:
        t += rng.uniform(0.8, 1.2) / fps
        out.append((t, key, path(t - shift, side)))
        if target and prev is not None:
            out.append((t, 'q' + key[1:], path(prev - shift)))
        prev = t
    return out


def window_p90(host, guest, secs):
    """the test's window measure on these tracks: h0, 0.25 s, h1, the guest, h2 (30 ms console calls)"""
    hb, gb = bt._by_key(host), bt._by_key(guest)

    def at(b, t):
        v = b['n1']
        i = max(0, min(len(v) - 1, next((j for j, x in enumerate(v) if x[0] > t), len(v)) - 1))
        return v[i][1]  # the last frame before the reading
    errs, t = [], 1.0
    while t < secs - 1:
        h0, h1, g, h2 = at(hb, t), at(hb, t + 0.28), at(gb, t + 0.31), at(hb, t + 0.34)
        errs.append(min(cm.seg_dist(g, h0, h1), cm.seg_dist(g, h1, h2)))
        t += 0.62
    return bt.pct(errs, 0.9)


def p90_same(host, guest, secs):
    sm = bt.same_moment(host, guest, ['n1'], 1.0, secs - 1)
    return bt.pct([r['d'] for r in sm], 0.9), sm


def main():
    fails = 0

    def check(ok, what):
        nonlocal fails
        print(('ok   ' if ok else 'FAIL ') + what)
        fails += not ok
    secs = 60.0
    host = rows(60, secs, 'n1', seed=1)
    on_time = rows(15, secs, 'n1', seed=2, target=True)
    late = rows(15, secs, 'n1', shift=0.25, seed=3)
    beside = rows(15, secs, 'n1', side=1.5, seed=4)
    p_on, sm_on = p90_same(host, on_time, secs)
    p_late, _ = p90_same(host, late, secs)
    p_side, _ = p90_same(host, beside, secs)
    check(p_on < 0.05, f'same moment: a copy drawn on time is on the host\'s NPC (p90 {p_on:.2f} m)')
    check(p_late > 1.2, f'same moment: a copy 0.25 s late at {V} m/s is over the 1.2 m limit (p90 {p_late:.2f} m)')
    check(p_side > 1.2, f'same moment: a copy 1.5 m beside the path is over the limit (p90 {p_side:.2f} m)')
    tg = [r['tgt'] for r in sm_on if r['tgt'] is not None]
    dv = [r['drv'] for r in sm_on if r['drv'] is not None]
    check(len(tg) > 500 and bt.pct(tg, 0.9) < 0.05, f'q rows: the target of the frame before is the host\'s NPC then '
          f'(p90 {bt.pct(tg, 0.9):.2f} m of {len(tg)})')
    check(dv and 0.2 < bt.pct(dv, 0.5) < 0.6, f'q rows: the drawn copy is one guest frame of the run from that target '
          f'(p50 {bt.pct(dv, 0.5):.2f} m)')
    w_late, w_side = window_p90(host, late, secs), window_p90(host, beside, secs)
    check(w_late < 0.6, f'window measure: blind to a copy 0.25 s late on the host\'s path (p90 {w_late:.2f} m against '
          f'{p_late:.2f} at the same moment)')
    check(w_side > 1.2, f'window measure: sees a copy off the path (p90 {w_side:.2f} m)')
    gb, hb = bt._by_key(late), bt._by_key(host)
    t_g, p_g = gb['n1'][300]
    t_found, res = bt.locate(gb, 'n1', p_g, t_g + 0.2)
    check(t_found == t_g and res == 0, f'locate: a console reading is found in the rows ({t_found} for {t_g})')
    lags = []
    for t, p in gb['n1'][100:800:20]:
        if path(t - 0.25) != path(t - 0.2) and (t - 0.25) % CYC < RUN - 0.1:  # running, not at a stand
            pt, _ = bt.path_time(hb['n1'], p, t)
            lags.append(t - pt)
    check(lags and abs(bt.pct(lags, 0.5) - 0.25) < 0.02, f'path_time: the copy\'s lag is found (p50 '
          f'{bt.pct(lags, 0.5):.3f} s of {len(lags)}, want 0.25)')
    print('RESULT:', 'FAIL' if fails else 'OK')
    return 1 if fails else 0


if __name__ == '__main__':
    raise SystemExit(main())
