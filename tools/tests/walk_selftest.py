#!/usr/bin/env python3
"""The walk tests' window measure on synthetic tracks (BUG-0407, in make test): it passes a puppet that is drawn on
its path one frame late at uneven frame times with hitches (what a loaded observer records), and fails one that
moves in steps (the 0.7.3.2 driving: stands, then catches up every 0.5 s)."""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_puppet_walk as pw  # noqa: E402


def track(kind, seed=1, secs=20.0, speed=4.5):
    rng = random.Random(seed)
    rows, t, x, drawn, last_jump = [], 0.0, 0.0, 0.0, 0.0
    prev_tgt = 0.0
    while t < secs:
        # 22-24 fps with jitter, 1 frame in 10 a 110-180 ms hitch
        dt = rng.uniform(0.11, 0.18) if rng.random() < 0.1 else rng.uniform(0.016, 0.07)
        t += dt
        x += speed * dt
        if kind == 'smooth':  # the frame draws where it was placed in the frame before, moved by its frame time
            drawn = prev_tgt + speed * dt * rng.uniform(0.0, 0.3)
        elif t - last_jump > 0.5:  # steps: stands, then is put on the path
            drawn, last_jump = x, t
        rows.append({'t': t, 'who': 1, 'flags': 3, 'tgt': (x, 0.0, 0.0), 'd': (drawn, 0.0, 0.0), 'rv': speed})
        prev_tgt = x
    return rows


def main():
    bad = 0
    for seed in range(1, 6):
        win, hitch, tj, legacy = pw.windows(track('smooth', seed), 4.75)
        matched = sum(1 for w in win if 0.6 <= w <= 1.4) / len(win)
        lm = sum(1 for w in legacy if 0.6 <= w <= 1.4) / len(legacy)
        ok = matched >= pw.MATCHED_MIN and hitch > 0
        bad += not ok
        print(f'  smooth seed {seed}: {len(win)} windows, matched {matched:.0%} (unshifted, hitches in: {lm:.0%}), '
              f'{hitch} left out for hitches: {"ok" if ok else "FAIL"}')
        win, hitch, tj, legacy = pw.windows(track('steps', seed), 4.75)
        matched = sum(1 for w in win if 0.6 <= w <= 1.4) / len(win)
        stand = sum(1 for w in win if w < 0.3) / len(win)
        ok = matched < pw.MATCHED_MIN and stand > pw.STAND_MAX
        bad += not ok
        print(f'  steps  seed {seed}: {len(win)} windows, matched {matched:.0%}, standing {stand:.0%}: '
              f'{"ok" if ok else "FAIL"}')
    print('walk_selftest: ' + ('FAILED' if bad else 'ok'))
    return 1 if bad else 0


if __name__ == '__main__':
    raise SystemExit(main())
