"""BUG-0425 (M1): how far a guest's copy of a host body is from the host's body at the same moment.

Both games record every synchronized body as they drew it (`coop.bodyrec`, mods/coop/coop.cpp: the start of the
mod's frame, absolute time from QueryPerformanceCounter, one clock for every game on one PC). For every guest
row of a body the host's body at the same time is interpolated from the host's rows, both taken at the guest
row's distance from the planet's centre (each game grounds its bodies on its own terrain: the height is
V1's rule, BUG-0425's height part, not this measure). The result per kind (n NPC creatures, t Tribe members,
v Civ vehicles, p players) over the samples where the host's body moves:

* dist: the guest's copy from the host's body (m), p50 / p90 / max;
* lag: dist over the host's speed (s): how far back in time the copy is (a copy that trails by its latency
  and the interpolation delay is dist = speed x lag);
* trail: the longest stretch over TRAIL_M (s) of one body (a copy that falls behind and stays there).

`coop.bodyrec` is a test command: it only reads the game."""
import bisect
import collections
import math
import re

MOVING = 0.5  # m/s: the host's body moves
TRAIL_M = 2.0  # m: a copy this far off ...
GAP = 0.25  # s: host rows farther apart than this are not interpolated (a hitch, the body not listed)


def record(con, on):
    con.call('coop.bodyrec start' if on else 'coop.bodyrec stop')


def dump(con):
    """t, key, (x, y, z) rows of one game"""
    rows = []
    n = int(re.search(r'samples=(\d+)', con.call('coop.bodyrec count')['out']).group(1))
    for i in range(0, n, 2000):
        for line in con.call(f'coop.bodyrec dump {i} 2000')['out'].splitlines():
            f = line.split()
            if len(f) == 5:
                try:
                    rows.append((float(f[0]), f[1], (float(f[2]), float(f[3]), float(f[4]))))
                except ValueError:
                    pass
    return rows


def _by_key(rows):
    by = collections.defaultdict(list)
    for t, k, p in rows:
        by[k].append((t, p))
    for v in by.values():
        v.sort()
    return by


def _norm(p):
    return math.sqrt(sum(x * x for x in p))


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * (len(xs) - 1)))] if xs else 0.0


def analyse(host_rows, guest_rows, kinds='ntvp', skip_t=None):
    """per kind: dict(n=samples, bodies, dist=[...], lag=[...], trail=(s, key, t), worst=[...]).
    skip_t(t): True for guest times to leave out (a test's own teleport of a body)"""
    hb, gb = _by_key(host_rows), _by_key(guest_rows)
    out = {}
    for key, gv in gb.items():
        kind = key[0]
        if kind not in kinds or key not in hb:
            continue
        hv = hb[key]
        ht = [x[0] for x in hv]
        r = out.setdefault(kind, dict(n=0, bodies=set(), dist=[], lag=[], trail=(0.0, '', 0.0), worst=[]))
        run0 = None
        for t, gp in gv:
            if skip_t and skip_t(t):
                run0 = None
                continue
            i = bisect.bisect_left(ht, t)
            if i <= 0 or i >= len(hv) or hv[i][0] - hv[i - 1][0] > GAP:
                run0 = None
                continue
            (ta, a), (tb, b) = hv[i - 1], hv[i]
            f = (t - ta) / (tb - ta) if tb > ta else 0.0
            h = tuple(a[j] + (b[j] - a[j]) * f for j in range(3))
            # the host's speed over its frames around t
            j0, j1 = max(0, i - 3), min(len(hv) - 1, i + 2)
            dt = hv[j1][0] - hv[j0][0]
            sp = math.dist(hv[j1][1], hv[j0][1]) / dt if dt > 0.02 else 0.0
            rg, rh = _norm(gp), _norm(h)
            if rg < 1 or rh < 1:
                continue
            hn = tuple(x * rg / rh for x in h)  # at the guest's distance from the centre
            d = math.dist(gp, hn)
            if sp < MOVING or sp > 60:  # standing (or the host's body put somewhere: no speed)
                run0 = None if d <= TRAIL_M else run0
                continue
            r['n'] += 1
            r['bodies'].add(key)
            r['dist'].append(d)
            r['lag'].append(d / sp)
            r['worst'].append((d, key, t, sp))
            if d > TRAIL_M:
                run0 = t if run0 is None else run0
                if t - run0 > r['trail'][0]:
                    r['trail'] = (t - run0, key, run0)
            else:
                run0 = None
        r['worst'] = sorted(r['worst'], reverse=True)[:6]
    return out


def summary(res, t0=0.0):
    lines = []
    names = {'n': 'NPC creatures', 't': 'Tribe members', 'v': 'Civ vehicles', 'p': 'players', 's': 'ships', 'c': 'cells'}
    for kind, r in sorted(res.items()):
        if not r['n']:
            continue
        lines.append(f'{names.get(kind, kind)}: {len(r["bodies"])} bodies, {r["n"]} moving samples: off the host\'s body '
                     f'p50 {pct(r["dist"], .5):.2f} p90 {pct(r["dist"], .9):.2f} max {max(r["dist"]):.2f} m; lag p50 '
                     f'{pct(r["lag"], .5):.3f} p90 {pct(r["lag"], .9):.3f} s; longest over {TRAIL_M:g} m {r["trail"][0]:.2f} s'
                     + (f' ({r["trail"][1]} at {r["trail"][2] - t0:.1f} s)' if r['trail'][1] else '') + '; worst '
                     + ', '.join(f'{k} {d:.1f} m at {t - t0:.1f} s ({s:.1f} m/s)' for d, k, t, s in r['worst'][:3]))
    return lines
