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
import os
import re

MOVING = 0.5  # m/s: the host's body moves
TRAIL_M = 2.0  # m: a copy this far off ...
GAP = 0.25  # s: host rows farther apart than this are not interpolated (a hitch, the body not listed)
# s: a guest frame this long is a hitch of the guest's own game (lab load; BUG-0407, coop_npc_walk.HITCH): a stretch
# over TRAIL_M is counted from after it (a slow guest is BUG-0503's class, npc_walk_slow)
HITCH = 0.1


def record(con, on):
    # SPORE_BODYREC_DIAG=1: the guest's driving targets too (rows a/r/l<id> of Tribe members, K3 diagnostics, q<id> of
    # NPC proxies, BUG-0363; analyse() skips them)
    diag = ' diag' if os.environ.get('SPORE_BODYREC_DIAG') == '1' else ''
    con.call('coop.bodyrec start' + diag if on else 'coop.bodyrec stop')


def now(con):
    """the game's absolute time (the clock of the rows)"""
    return float(re.search(r'now=([\d.]+)', con.call('coop.bodyrec count')['out']).group(1))


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
        last_t = None
        for t, gp in gv:
            hitch = last_t is not None and t - last_t > HITCH
            last_t = t
            if (skip_t and skip_t(t)) or hitch:
                run0 = None
                if not hitch:
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


def host_at(hv, ht, t):
    """the host's body at time t interpolated between its rows (None: outside them or across a gap over GAP)"""
    i = bisect.bisect_left(ht, t)
    if i <= 0 or i >= len(hv) or hv[i][0] - hv[i - 1][0] > GAP:
        return None
    (ta, a), (tb, b) = hv[i - 1], hv[i]
    f = (t - ta) / (tb - ta) if tb > ta else 0.0
    return tuple(a[j] + (b[j] - a[j]) * f for j in range(3))


def _at_radius(p, q):
    """p put at q's distance from the planet's centre (each game grounds its bodies on its own terrain)"""
    rp, rq = _norm(p), _norm(q)
    return tuple(x * rq / rp for x in p) if rp > 1 and rq > 1 else p


def same_moment(host_rows, guest_rows, keys, t0, t1):
    """BUG-0363: every guest frame in [t0, t1] of the bodies `keys` (n<id>) against the host's body at the same time:
    [dict(t, key, d, sp, tgt, drv)]: d the drawn copy from the host's body (m), sp the host's speed (m/s), tgt the
    guest's interpolation target (row q<id>, the frame before's driving, so at the frame before's time) from the host's
    body then, drv the drawn copy from the target it was driven to (None without q rows)"""
    hb, gb = _by_key(host_rows), _by_key(guest_rows)
    frames = sorted({t for t, _, _ in guest_rows})
    out = []
    for key in keys:
        hv, gv = hb.get(key, []), gb.get(key, [])
        if not hv or not gv:
            continue
        ht = [x[0] for x in hv]
        q = dict(gb.get('q' + key[1:], []))
        for t, gp in gv:
            if t < t0 or t > t1:
                continue
            h = host_at(hv, ht, t)
            if h is None:
                continue
            i = bisect.bisect_left(ht, t)
            j0, j1 = max(0, i - 3), min(len(hv) - 1, i + 2)
            dt = hv[j1][0] - hv[j0][0]
            sp = math.dist(hv[j1][1], hv[j0][1]) / dt if dt > 0.02 else 0.0
            r = dict(t=t, key=key, d=math.dist(gp, _at_radius(h, gp)), sp=sp, tgt=None, drv=None)
            w = q.get(t)
            if w is not None:
                r['drv'] = math.dist(gp, _at_radius(w, gp))
                k = bisect.bisect_left(frames, t)
                hp = host_at(hv, ht, frames[k - 1]) if k > 0 else None
                if hp is not None:
                    r['tgt'] = math.dist(w, _at_radius(hp, w))
            out.append(r)
    return out


def locate(rows_by_key, key, pos, t_est, win=0.4):
    """the time of the row of `key` within win s of t_est nearest to pos (a console reading of the body's position):
    (t, residual m) or (None, None)"""
    best = (None, None)
    for t, p in rows_by_key.get(key, []):
        if abs(t - t_est) <= win:
            d = math.dist(p, pos)
            if best[1] is None or d < best[1]:
                best = (t, d)
    return best


def path_time(hv, pos, t_c, win=0.6, step=0.005):
    """the time in t_c +- win at which the host's body (rows hv) came nearest to pos: (t, m) or (None, None)"""
    ht = [x[0] for x in hv]
    best = (None, None)
    t = t_c - win
    while t <= t_c + win:
        h = host_at(hv, ht, t)
        if h is not None:
            d = math.dist(_at_radius(h, pos), pos)
            if best[1] is None or d < best[1]:
                best = (t, d)
        t += step
    return best


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


# ---- BUG-0609: the divergence detector's trail rows (monitor.Watch.detect)
# The detector compared two dumps read one after the other with 15 m / 6 m tolerances: a copy that trails its host body
# by 0.2 s (0.6-2 m at walking speed) never made a row. These rows come from both games' `coop.bodyrec` (one clock),
# so they are the same moment on both machines; a kind is a finding when its copies are late (lag p50) AND far off
# (dist p90), or one copy stayed far off for long. The median lag, not p90: the detector's own census dumps make
# both games hitch, which spreads the lag's tail. Measured per 10 s (lab 20261106-041119 / -041542): without drive.lead
# lag p50 0.15-0.23 s in every window (p90 0.72-0.90 m Creature NPCs, 1.1-1.3 m Tribe animals, 1.9-2.1 m members);
# with it 0.013-0.132 s (p90 0.11-1.79 m on a loaded lab; 0.132 s in one 10 s window of lab 20261106-042100).
DET_STEP = 0.05  # s: one frame per 50 ms is read (the host's body interpolated between frames up to GAP apart)
DET_LAG, DET_DIST, DET_TRAIL = 0.14, 0.5, 1.5  # s (lag p50), m (dist p90), s
DET_MIN = 60  # moving samples of a kind in a period before it is judged
KIND_NAMES = {'n': 'npc', 't': 'member', 'v': 'vehicle', 'p': 'player', 's': 'ship', 'c': 'cell'}


def ensure_on(con):
    """start recording without clearing the rows of a test that records itself; True when it is on"""
    out = con.call('coop.bodyrec count').get('out', '')
    if 'bodyrec on' in out:
        return True
    con.call('coop.bodyrec on')
    return 'bodyrec on' in con.call('coop.bodyrec count').get('out', '')


def since(con, t0, step=DET_STEP, page=2000):
    """rows after absolute time t0 (one frame per step s)"""
    rows = []
    while True:
        got = []
        for line in con.call(f'coop.bodyrec since {t0:.4f} {page} {step}').get('out', '').splitlines():
            f = line.split()
            if len(f) == 5:
                try:
                    got.append((float(f[0]), f[1], (float(f[2]), float(f[3]), float(f[4]))))
                except ValueError:
                    pass
        if len(got) < page:
            return rows + got
        last = got[-1][0]
        whole = [r for r in got if r[0] < last]  # the last frame may be cut: read it again from the frame before
        if not whole:
            return rows + got
        rows += whole
        t0 = whole[-1][0]


def detect_findings(stage, host_rows, guest_rows):
    """the detector's `<stage>:trail:<kind>` findings of one period (dicts as worlddiff.diff's)"""
    out = []
    # (in Tribe and Civ a player's row is its mouse marker on the planet, modes.cpp rts_fill, not a body: seen as
    # Civilization:trail:player p90 8-9 m in every period of play_civ, lab 20261106-071756)
    kinds = 'ntvs' if stage in ('Tribe', 'Civilization') else 'ntvps'
    res = analyse(host_rows, guest_rows, kinds=kinds)
    for kind, r in sorted(res.items()):
        if r['n'] < DET_MIN:
            continue
        p90, lag = pct(r['dist'], 0.9), pct(r['lag'], 0.5)
        if (lag > DET_LAG and p90 > DET_DIST) or r['trail'][0] > DET_TRAIL:
            name = KIND_NAMES.get(kind, kind)
            out.append(dict(stage=stage, sig=f'{stage}:trail:{name}', n=r['n'],
                            text=f'{len(r["bodies"])} {name} copies off the host\'s bodies at the same moment: p90 '
                                 f'{p90:.2f} m, lag p50 {lag:.3f} s, longest over {TRAIL_M:g} m {r["trail"][0]:.2f} s '
                                 f'({r["trail"][1]}), {r["n"]} moving samples'))
    return out


def steps(rows, kinds='t', over=5.0, frame=0.1):
    """BUG-0610: one-frame steps over `over` m of a body in one game's own rows (frames up to `frame` s apart):
    [(m, key, t, dt)], largest first"""
    out = []
    for key, v in _by_key(rows).items():
        if key[0] not in kinds:
            continue
        for (ta, a), (tb, b) in zip(v, v[1:]):
            d = math.dist(a, b)
            if 0 < tb - ta <= frame and d > over:
                out.append((d, key, tb, tb - ta))
    return sorted(out, reverse=True)


def stop_slides(ref_rows, copy_rows, kinds='p', win=1.5):
    """BUG-0408: per stop of a reference body (moving > 1.5 m/s over 0.3 s, then within 0.15 m for the next second): how
    far its copy went past where it stopped, on both games' own rows at the same moment: [(slide m, key, t, off at
    the stop m)]; slide = the copy's largest distance from the stopped body in `win` s less the distance at the stop"""
    out = []
    rb, cb = _by_key(ref_rows), _by_key(copy_rows)
    for key, rv in rb.items():
        if key[0] not in kinds or key not in cb:
            continue
        cv = cb[key]
        ct = [x[0] for x in cv]
        k = 0
        while k < len(rv) - 2:
            t, p = rv[k]
            prev = [x for x in rv[max(0, k - 60):k] if x[0] > t - 0.3]
            post = [x for x in rv[k:k + 400] if x[0] < t + 1.0]
            if (len(prev) >= 3 and math.dist(prev[0][1], p) / (t - prev[0][0]) > 1.5 and len(post) > 5 and
                    post[-1][0] - t > 0.8 and math.dist(post[0][1], post[-1][1]) < 0.15):
                i0, i1 = bisect.bisect_left(ct, t), bisect.bisect_right(ct, t + win)
                seg = cv[i0:i1]
                if len(seg) > 5:
                    rg = _norm(p)
                    def off(q):
                        return math.dist(tuple(x * _norm(q) / rg for x in p), q)
                    e0 = off(seg[0][1])
                    out.append((max(off(q) for _, q in seg) - e0, key, t, e0))
                k += len(post)
                continue
            k += 1
    return sorted(out, reverse=True)
