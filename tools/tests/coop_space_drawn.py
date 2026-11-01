#!/usr/bin/env python3
"""Space: where each game DRAWS the partner's ship and the host's NPC ships, against where they are
(BUG-0378, BUG-0190).

Both games record every ship each frame at the start of the mod's frame (`coop.fleet track`: after the
game's own update, i.e. what the frame showed, before the mod writes any pose), on the session clock.
The observer's drawn rows (p<id> = a partner's puppet, s<id> = a host NPC ship's mirror) are compared
with the owner's own rows (m<id> = its own ship, s<id> on the host = the ship itself):

  now   the owner's ship at the same session time: what the player sees, lag included
  path  the closest point of the owner's path over the LAG_S before (lag taken out): a lasting
        offset (a desync, a wrong height) stays, interpolation / network delay does not

Phases: in the start system (or on the fixture's planet with --planet), the host flies with real input
(clicks into the view away from its ship, WASD) while the guest stays, then the guest flies; the host
spawns NPC ships first.

  SPORE_LANE=10 python3 tools/tests/coop_space_drawn.py [--planet] [--secs N]

Checks: per kind, the `path` error's 90th percentile under PATH_LIMIT (a ship drawn somewhere else);
the `now` error is printed with the owner's speed (how much is lag) and checked against
LAG_S x speed + PATH_LIMIT.
"""
import math
import re
import sys
import time

from fixtures import planet_of
from twoinst import Checks, shot, start

LAG_S = 0.5        # interpolation delay 0.10 s + snapshot interval + one way + extrapolation, generous
PATH_LIMIT = 3.0   # m: a ship drawn further than this from every point of its recent path is elsewhere
FLY_KEYS = [['0x57'], ['0x44'], ['0x53'], ['0x41'], ['0x57', '0x44'], ['0x53', '0x41']]


ZOOM = {}  # (console, kind, id) -> the ship's mZoomAltitude per row (diagnostics)
DETAIL = {}  # (t, kind, id) -> (offset, zoom) of a row (diagnostics: printed for the worst rows)


def track(con, what):
    return con.call(f'coop.fleet track {what}')['out']


def dump(con):
    """{(kind, id): [(t, (x, y, z))]} of a game's track"""
    m = re.search(r'samples=(\d+)', track(con, 'count'))
    n = int(m.group(1)) if m else 0
    rows = {}
    for start_i in range(0, n, 400):
        for line in con.call(f'coop.fleet track dump {start_i} 400')['out'].splitlines():
            f = line.split()
            if len(f) != 10:
                continue
            try:
                t, kind, i = float(f[0]), f[1], int(f[2])
                p = (float(f[3]), float(f[4]), float(f[5]))
                o = (float(f[6]), float(f[7]), float(f[8]))
                zoom = float(f[9])
            except ValueError:
                continue
            rows.setdefault((kind, i), []).append((t, p))
            ZOOM.setdefault((id(con), kind, i), []).append(zoom)
            DETAIL[(t, kind, i)] = (o, zoom)
    for v in rows.values():
        v.sort()
    return rows


def at(path, t):
    """the position of a path at time t (linear between rows), None outside it"""
    if not path or t < path[0][0] or t > path[-1][0]:
        return None
    lo, hi = 0, len(path) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if path[mid][0] <= t:
            lo = mid
        else:
            hi = mid
    (t0, a), (t1, b) = path[lo], path[hi]
    k = 0 if t1 <= t0 else (t - t0) / (t1 - t0)
    return tuple(x + (y - x) * k for x, y in zip(a, b))


def seg_dist(p, a, b):
    ab = [y - x for x, y in zip(a, b)]
    ll = sum(c * c for c in ab)
    t = 0 if ll == 0 else max(0.0, min(1.0, sum((pc - ac) * c for pc, ac, c in zip(p, a, ab)) / ll))
    return math.dist(p, [ac + t * c for ac, c in zip(a, ab)])


def path_dist(path, t, p, lag=LAG_S):
    """distance from p to the path's piece over [t - lag, t]"""
    pts = [q for (tt, q) in path if t - lag <= tt <= t]
    a, b = at(path, t - lag), at(path, t)
    pts = ([a] if a else []) + pts + ([b] if b else [])
    if not pts:
        return None
    if len(pts) == 1:
        return math.dist(p, pts[0])
    return min(seg_dist(p, x, y) for x, y in zip(pts, pts[1:]))


def speed(path, t, dt=0.2):
    a, b = at(path, t - dt), at(path, t)
    return math.dist(a, b) / dt if a and b else 0.0


def pct(v, p):
    if not v:
        return float('nan')
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p))]


def compare(view, truth, view_key, truth_key, skip_s=1.0):
    """errors of the observer's drawn rows against the owner's own rows: (now, path, speeds)"""
    drawn, own = view.get(view_key, []), truth.get(truth_key, [])
    now_e, path_e, sp = [], [], []
    if not drawn or not own:
        return now_e, path_e, sp
    t0 = drawn[0][0] + skip_s  # the first second after the puppet appeared: its spawn spot
    for t, p in drawn:
        if t < t0:
            continue
        o = at(own, t)
        d = path_dist(own, t, p)
        if o is None or d is None:
            continue
        now_e.append(math.dist(p, o))
        path_e.append(d)
        sp.append(speed(own, t))
        if d > PATH_LIMIT and len(BAD) < 12:
            off, zoom = DETAIL.get((t, view_key[0], view_key[1]), ((0, 0, 0), 0))
            BAD.append(f'    {view_key[0]}{view_key[1]} at s {t:.3f}: drawn ({p[0]:.1f} {p[1]:.1f} {p[2]:.1f}) own '
                       f'({o[0]:.1f} {o[1]:.1f} {o[2]:.1f}) path {d:.1f} m, its offset ({off[0]:.1f} {off[1]:.1f} '
                       f'{off[2]:.1f}) zoom {zoom:.0f}')
    return now_e, path_e, sp


BAD = []  # the first rows drawn off their path (printed by report)


def report(ck, label, now_e, path_e, sp, limit=PATH_LIMIT, need=30, check=True):
    if len(path_e) < need:
        print(f'  {label}: only {len(path_e)} samples', flush=True)
        BAD.clear()
        if check:
            ck(False, f'{label}: drawn on the other game ({len(path_e)} samples, need {need})')
        return
    worst = max(range(len(path_e)), key=lambda i: path_e[i])
    print(f'  {label}: n={len(path_e)} now med {pct(now_e, .5):.1f} p90 {pct(now_e, .9):.1f} max {max(now_e):.1f} m | '
          f'path med {pct(path_e, .5):.2f} p90 {pct(path_e, .9):.2f} max {max(path_e):.1f} m | speed med '
          f'{pct(sp, .5):.0f} p90 {pct(sp, .9):.0f} max {max(sp):.0f} m/s (worst path {path_e[worst]:.1f} m at '
          f'{sp[worst]:.0f} m/s)', flush=True)
    for line in BAD:
        print(line, flush=True)
    BAD.clear()
    if not check:
        return
    ck(pct(path_e, .9) <= limit, f'{label}: drawn on its own path (90% within {limit} m of where it was over the last '
                                 f'{LAG_S} s)')
    bad = [i for i in range(len(now_e)) if now_e[i] > LAG_S * sp[i] + limit]
    ck(len(bad) <= len(now_e) // 10, f'{label}: drawn within its lag ({LAG_S} s x its speed + {limit} m) in 90% '
                                    f'({len(bad)} of {len(now_e)} beyond)')


CLICKS = [(320, 0), (0, 260), (-320, 0), (0, -260), (300, 240), (-300, -240)]


def ship_xy(con):
    for line in con.call('coop.targets all')['out'].splitlines():
        f = line.split()
        if f and f[0] == 'me' and len(f) == 5:
            return int(f[3]), int(f[4])
    return None


def fly(con, secs, rng_i=0, wheel=False):
    """real input: a click into the view away from the ship (it flies there, as a player's "go
    there" click) and key holds (WASD), in turns; wheel: also the mouse wheel at the ship (on a planet
    it changes the ship's height, its zoom altitude: docs/re/space.md 7.10)"""
    t_end = time.time() + secs
    i = rng_i
    while time.time() < t_end:
        if wheel and i % 3 == 2:
            me = ship_xy(con) or (720, 540)
            con.call(f'wheel {me[0]} {me[1]} {5 if (i // 3) % 2 == 0 else -5}')
            time.sleep(1.5)
        elif i % 2 == 0:
            me = ship_xy(con) or (720, 540)
            dx, dy = CLICKS[(i // 2) % len(CLICKS)]
            x, y = max(60, min(1380, me[0] + dx)), max(80, min(860, me[1] + dy))
            con.call(f'click {x} {y}')
            time.sleep(2.5)
        else:
            keys = FLY_KEYS[i % len(FLY_KEYS)]
            for k in keys:
                con.call(f'key {k} down')
            time.sleep(1.5)
            for k in keys:
                con.call(f'key {k} up')
            time.sleep(0.2)
        i += 1


def measure(host, guest, ck, secs, where):
    for c in (host, guest):
        track(c, 'on')
    print('  host flies', flush=True)
    fly(host, secs, wheel=where == 'planet')
    time.sleep(1)
    print('  guest flies', flush=True)
    fly(guest, secs, 3, wheel=where == 'planet')
    time.sleep(1)
    for c in (host, guest):
        track(c, 'off')
    hv, gv = dump(host), dump(guest)
    for who, v in (('host', hv), ('guest', gv)):
        for k, rows in sorted(v.items()):
            r = [math.hypot(*p) for _, p in rows]
            z = ZOOM.get((id(host if who == 'host' else guest), k[0], k[1]), [0])
            print(f'  {who} {k[0]}{k[1]}: {len(rows)} rows over {rows[-1][0] - rows[0][0]:.1f} s, |pos| {min(r):.0f}..'
                  f'{max(r):.0f}, zoom altitude {min(z):.0f}..{max(z):.0f}', flush=True)
    print(f'  rows: host {sum(map(len, hv.values()))} {sorted(hv)[:8]}, guest {sum(map(len, gv.values()))} '
          f'{sorted(gv)[:8]}', flush=True)
    report(ck, f'{where}: the host ship on the guest', *compare(gv, hv, ('p', 0), ('m', 0)))
    report(ck, f'{where}: the guest ship on the host', *compare(hv, gv, ('p', 1), ('m', 1)))
    ships = sorted(k for k in gv if k[0] == 's' and k in hv)
    allp, alln, alls = [], [], []
    for k in ships:
        n_e, p_e, s_e = compare(gv, hv, k, k)
        alln += n_e
        allp += p_e
        alls += s_e
    if ships:
        report(ck, f'{where}: the host NPC ships on the guest ({len(ships)})', alln, allp, alls)
    else:
        print('  (no NPC ship on both games)', flush=True)


def main():
    args = sys.argv[1:]
    secs = float(args[args.index('--secs') + 1]) if '--secs' in args else 12
    planet = '--planet' in args
    host, guest = start('space', planet=planet_of('space') if planet else None)
    ck = Checks()
    time.sleep(3)
    for _ in range(2):
        print('  host:', host.call('coop.fleet spawn 2')['out'].strip(), flush=True)
    time.sleep(3)
    measure(host, guest, ck, secs, 'planet' if planet else 'system')
    shot(host, 'space_drawn_host.png')
    shot(guest, 'space_drawn_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
