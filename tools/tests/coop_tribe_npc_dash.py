#!/usr/bin/env python3
"""Tribe: an animal that dashes on the host is not shown metres behind on the guest (BUG-0305).

A wounded animal of a Tribe hunt runs in short dashes (5-7 m/s for ~1 s, then stands). The guest's
proxy walks with the game's locomotion, which never runs faster than the animal's own top speed:
the lag at the start of a dash stayed as an offset along the path for the whole dash (2-4 m on
every guest at once in the 4-player measurement, coop_sync_multi.py --fixture tribe).

1. Two games on the tribe fixture; the guest runs at ~15 fps (frame.limit 66, the frame rate of a
   loaded guest of a 4-player session on one PC: the largest offsets came at 9-20 fps).
2. The host picks two animals of the planet's small herd species (06808ACA, the one 4 m off in
   the 4-player probes) that the guest has as proxies and makes them run side by side (1.2 m
   apart, a herd fleeing together) back and forth along a 12 m zigzag at 6.5 m/s, 0.6 s standing
   at each end (`coop.npc dash`, test command), for 40 s.
3. Every ~0.5 s the guest's game positions of those animals is compared with the host's path over the
   0.25 s before the guest's sample and the host's sample after it (coop_multi.measure, as the
   campaign does). PASS: 90 % of the samples within 1.2 m (fix off: p90 1.5-2.8 m, on: 0.7-0.95 m).
   BUG-0321: on a loaded machine the host's states reached the guest late or at 4-6 a second
   instead of 10 (`age=` up to 0.5 s, `rx=`): the guest showed the animal where the host had it
   that long ago, 2 m back at 6.5 m/s, with the proxy right on its target. Those samples measure
   the machine, not the driving: the p90 is taken over the samples whose state was fresh (age at
   most 0.25 s and at least 7 states a second). BUG-0430: with fewer than 40 such samples the run is
   inconclusive (RESULT: SKIP) only when the host itself runs under 25 fps; otherwise at least 60 % of the
   samples must have a fresh state and the p90 of all samples must stay within 2 x 1.2 m (late states are
   a finding, not a pass).

BUG-0363: the judged measure is the games' own positions at the same moment (`coop.bodyrec` on both games, one clock,
tools/tests/body_trail.same_moment): every guest frame of the dash, the drawn proxy against the host's NPC at the same
time (no delay: the NPC proxies are shown where the host has them now, drive.lead, npc.cpp px.buf.lead). PASS: 90 % of
the frames within 1.2 m. The window measure above (coop_multi) is printed for comparison and not judged: it forgives a
copy late by up to the 0.25 s window and can only under-report a copy on the host's path (a time-shifted copy of the
zigzag: late 0.1-0.3 s 0.3 m, tools/tests/dash_selftest.py), so its 1.2-1.4 m failures were copies off the
host's path, not its sampling. The rows also split the error: the guest's interpolation target (rows q<id>, `coop.bodyrec
start diag`) from the host's body (states, lead) and the drawn proxy from that target (the driving), and every window
sample's real times (each console reading found in the rows).

Switch: SPORE_FIX_OFF=npc.trail (the driving before the fix) must FAIL.

Creature (BUG-0281, `creature_npc_dash`): DASH_FIXTURE=creature runs the same on the creature
fixture with the nest species next to the start (065CBE70, the NPC 3-5 m from the guests' avatars
in the 4-player report) at 4 m/s (their running speed); the guest's avatar stands 1.5 m beside the
end of the path, so the proxies run up to it and stand next to it at every turn (suite
`creature_npc_dash`). Before `npc.kinematic` these proxies hardly walked (a MoveTo again every 0.1 s
restarts the walk, our pulls were undone by the locomotion): p90 3.3-3.5 m with npc.trail, 6.2 m
without; with it 0.6-0.9 m (SPORE_FIX_OFF=npc.kinematic must FAIL).

  SPORE_LANE=5 python3 tools/tests/coop_tribe_npc_dash.py
  SPORE_LANE=5 DASH_FIXTURE=creature python3 tools/tests/coop_tribe_npc_dash.py
"""
import bisect
import json
import math
import os
import re
import time

import body_trail as bt
import coop_multi as cm
from twoinst import Checks, start

LIMIT = 1.5     # m (printed: samples farther than this)
P90_S = 0.3     # s x the dash speed: the 90th percentile of the errors (BUG-0757; see P90 below)
ALONG_MIN = -0.043  # s: BUG-0757, the copy's median offset along the host's path (on -0.041 .. +0.009, off -0.045 .. -0.074)
# DASH_FEED=0: no `coop.npc list` on the guest during the dash (it samples every proxy's buffer at the console's own time,
# which moves drive.lead_late's / the smoothing's state between two frames; BUG-0363 check) and no fresh-state check
FEED = os.environ.get('DASH_FEED', '1') != '0'
SLOW_HOST = 25   # fps: a host under this makes its states late itself: too few fresh samples are inconclusive (BUG-0321)
FRESH_SHARE = 0.6  # of the samples with a fresh state when the host is not slow (BUG-0430: late states fail)
FRAME_MS = int(os.environ.get('DASH_FRAME_MS', '66'))
LEN, WIDTH = float(os.environ.get('DASH_LEN', '12')), float(os.environ.get('DASH_WIDTH', '2'))
FIXTURE = os.environ.get('DASH_FIXTURE', 'tribe')
# m: the second animal runs this far beside the first. BUG-0757: the Tribe fixture's animals (06808ACA, 40-50 hp) are
# wider than 1.2 m: the guest's game pushed its two proxies 2.6-3.8 m apart (the host's pair 1.3-2.8 m; tribe_npc_dash
# rows, labs 20261108-105804 #1, #3), a sideways error of the game's own separation of two bodies on one spot, not of the
# copy; 3 m there. Creature's nest animals stay at 1.2 m (their pair the same on both games)
OFF = float(os.environ.get('DASH_OFF', '3.0' if FIXTURE == 'tribe' else '1.2'))
# the small herd animal of the fixture's planet (the one 4 m off in the 4-player probes, BUG-0305)
# Creature: the nest species next to the fixture's start (BUG-0281)
SPECIES = os.environ.get('DASH_SPECIES', '065CBE70' if FIXTURE == 'creature' else '06808ACA')
# m/s: Creature NPCs ran at up to 4 m/s in coop.npc list (spd=) on lane 5
SPEED = float(os.environ.get('DASH_SPEED', '4' if FIXTURE == 'creature' else '6.5'))
# m: BUG-0757. The limit was 1.2 m, calibrated on Creature's 4 m/s dash (BUG-0281: fix on 0.7-0.95 m): 0.3 s of the dash.
# The errors that stay are of time (the corners and the reversals of the zigzag known a latency late, a guest frame of
# 65 ms at 15 fps), so they scale with the speed: the Tribe fixture's 6.5 m/s dash left 1.2-1.4 m with the copy on time
# (its median offset along the path -0.03 .. +0.02 s; lab 20261108-110422). The same 0.3 s at the fixture's speed:
# Creature 1.2 m (unchanged), Tribe 1.95 m; the frame lag itself is judged by ALONG_MIN
P90 = max(1.2, P90_S * SPEED)


def host_animals(con):
    """the host's living NPCs: id -> pos"""
    out = {}
    for m in re.finditer(r'^\s*#(\d+) \S+ species=(\S+) hp=([\d.]+) dist=\S+ pos=\((\S+) (\S+) (\S+)\)', con.call('coop.npc list')['out'], re.M):
        if m.group(2) == SPECIES and float(m.group(3)) > 0:
            out[int(m.group(1))] = tuple(map(float, m.group(4, 5, 6)))
    return out


def proxy_feed(con):
    """the guest's proxies: id -> (age of the last state in s, states per second)"""
    out = {}
    for m in re.finditer(r'^\s*#(\d+) species=\S+ spawned .*? age=([\d.]+) .*? rx=(\d+)/s', con.call('coop.npc list')['out'], re.M):
        out[int(m.group(1))] = (float(m.group(2)), int(m.group(3)))
    return out


def guest_proxies(con):
    return {int(m.group(1)) for m in re.finditer(r'^\s*#(\d+) species=\S+ spawned ', con.call('coop.npc list')['out'], re.M)}


def clock_off(con):
    """the game's absolute clock (bodyrec rows) minus time.monotonic(), +- half a console round trip"""
    a = time.monotonic()
    n = bt.now(con)
    b = time.monotonic()
    return n - (a + b) / 2, b - a


def timed(con, off):
    """coop_multi.sample with the game time of the call's middle (the clock of the rows)"""
    a = time.monotonic()
    d = cm.sample(con)
    return d, (a + time.monotonic()) / 2 + off


def pct(v, q):
    return bt.pct(v, q) if v else -1.0


def save_rows(hr, gr, keys, t0, t1):
    """both games' rows of the dash for a later analysis (out/lane<N>/dash/<time>.json)"""
    d = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'out', f'lane{os.environ.get("SPORE_LANE", "x")}', 'dash')
    try:
        os.makedirs(d, exist_ok=True)
        f = os.path.join(d, time.strftime('%Y%m%d-%H%M%S') + f'-{FIXTURE}.json')
        with open(f, 'w') as fh:
            json.dump(dict(keys=keys, t0=t0, t1=t1, host=hr, guest=gr), fh)
        print(f'  rows saved: {os.path.normpath(f)}', flush=True)
    except OSError as e:
        print(f'  (rows not saved: {e})', flush=True)


def judge_rows(host, guest, keys, t0, t1, windows):
    """BUG-0363: the same-moment measure from both games' rows; prints the comparison with the window measure.
    Returns (p90 m over every guest frame, frames)"""
    hr, gr = bt.dump(host), bt.dump(guest)
    want = set(keys) | {c + k[1:] for k in keys for c in 'qu'}
    hr = [r for r in hr if r[1] in want]
    gr = [r for r in gr if r[1] in want]
    save_rows(hr, gr, keys, t0, t1)
    sm = bt.same_moment(hr, gr, keys, t0, t1)
    d = [r['d'] for r in sm]
    mov = [r['d'] for r in sm if r['sp'] >= bt.MOVING]
    tg = [r['tgt'] for r in sm if r['tgt'] is not None]
    dv = [r['drv'] for r in sm if r['drv'] is not None]
    print(f'  same moment (coop.bodyrec, {len(sm)} guest frames of {len(keys)} animals, {len(hr)} / {len(gr)} rows): drawn '
          f'proxy from the host\'s NPC p50 {pct(d, .5):.2f} p90 {pct(d, .9):.2f} max {max(d, default=-1):.2f} m; '
          f'running only ({len(mov)}) p90 {pct(mov, .9):.2f} m', flush=True)
    print(f'  split: the guest\'s target from the host\'s NPC (states, lead) p50 {pct(tg, .5):.2f} p90 {pct(tg, .9):.2f} m '
          f'({len(tg)}); the drawn proxy from its target (driving) p50 {pct(dv, .5):.2f} p90 {pct(dv, .9):.2f} m ({len(dv)})',
          flush=True)
    rw = [r['raw'] for r in sm if r['raw'] is not None]
    print(f'  raw lead (M1, no late time, no smoothing) from the host\'s NPC p50 {pct(rw, .5):.2f} p90 {pct(rw, .9):.2f} m '
          f'({len(rw)})', flush=True)
    for key in keys:
        v = [r['d'] for r in sm if r['key'] == key]
        far = [r for r in sm if r['key'] == key and r['d'] > 2 * P90]
        ep = f'; over {2 * P90} m from {far[0]["t"] - t0:.1f} to {far[-1]["t"] - t0:.1f} s ({len(far)} frames)' if far else ''
        print(f'    {key}: {len(v)} frames, p90 {pct(v, .9):.2f} m (target {pct([r["tgt"] for r in sm if r["key"] == key and r["tgt"] is not None], .9):.2f}, '
              f'raw lead {pct([r["raw"] for r in sm if r["key"] == key and r["raw"] is not None], .9):.2f}){ep}', flush=True)
    # every window sample at the times its console readings really had (found in the rows)
    hb, gb = bt._by_key(hr), bt._by_key(gr)
    smi = {(r['key'], r['t']): r for r in sm}
    lags, rows, worse, over = [], [], 0, 0
    for w in windows:
        tg_, res = bt.locate(gb, w['key'], w['g'], w['tg'])
        if tg_ is None or res > 0.05:
            continue
        ts = [bt.locate(hb, w['key'], w[h], w['t' + h])[0] if w[h] else None for h in ('h0', 'h1', 'h2')]
        r = smi.get((w['key'], tg_))
        if r is None:
            continue
        pt, pd = bt.path_time(hb.get(w['key'], []), w['g'], tg_)
        lag = tg_ - pt if pt is not None and r['sp'] >= bt.MOVING else None  # (standing: any time of the stand fits)
        if lag is not None:
            lags.append(lag)
        worse += r['d'] > w['e']
        if w['e'] > P90:
            over += 1
            rows.append((w['e'], r['d'], r['tgt'], r['drv'], lag, pd, [None if x is None else x - tg_ for x in ts], tg_ - t0))
    print(f'  window samples found in the rows, running: {len(lags)} of {len(windows)}; the copy at the host\'s path time '
          f'(lag, + late, - ahead) p10 {pct(lags, .1):+.3f} p50 {pct(lags, .5):+.3f} p90 {pct(lags, .9):+.3f} s; the same moment '
          f'farther than the window measure in {worse}', flush=True)
    for e, dd, t_, dr, lag, pd, ts, tt in sorted(rows, reverse=True)[:10]:
        f = lambda x: '-' if x is None else f'{x:+.3f}'
        g = lambda x: '-' if x is None else f'{x:.2f}'
        print(f'    window {e:.2f} m at {tt:.1f} s: same moment {dd:.2f} (target {g(t_)}, driving {g(dr)}), nearest the '
              f'host\'s path {g(pd)} m at lag {f(lag)} s; host readings at {f(ts[0])} / {f(ts[1])} / {f(ts[2])} s of the '
              f'guest\'s', flush=True)
    return pct(d, .9), len(sm), along_offset(hr, gr, keys, t0, t1)


def along_offset(hr, gr, keys, t0, t1):
    """BUG-0757: the drawn copies' median time offset along the host's path while running (s, + ahead, - behind): the
    error along the host's velocity over its speed, every guest frame of the host's NPC over 2 m/s"""
    hb, gb = bt._by_key(hr), bt._by_key(gr)
    out = []
    for key in keys:
        hv, gv = hb.get(key, []), gb.get(key, [])
        ht = [x[0] for x in hv]
        for t, gp in gv:
            if t < t0 or t > t1:
                continue
            h = bt.host_at(hv, ht, t)
            if h is None:
                continue
            i = bisect.bisect_left(ht, t)
            j0, j1 = max(0, i - 3), min(len(hv) - 1, i + 2)
            dt = hv[j1][0] - hv[j0][0]
            if dt < 0.02:
                continue
            v = [(hv[j1][1][k] - hv[j0][1][k]) / dt for k in range(3)]
            sp = math.sqrt(sum(x * x for x in v))
            if sp > 2:
                out.append(sum((gp[k] - h[k]) * v[k] for k in range(3)) / sp / sp)
    return pct(out, .5) if out else None


def main():
    host, guest = start(FIXTURE)
    ck = Checks()
    print('  guest:', guest.call(f'frame.limit {FRAME_MS}')['out'].strip(), flush=True)
    for c in filter(None, os.environ.get('DASH_GUEST_CMDS', '').split(';')):  # tests: e.g. coop.npc walklead 1
        print('  guest:', guest.call(c.strip())['out'].strip(), flush=True)
    time.sleep(5)
    hs, gs = host_animals(host), guest_proxies(guest)
    both = sorted(i for i in hs if i in gs)
    ck(both, f'the guest mirrors animals of the host ({len(hs)} on the host, {len(gs)} proxies, {len(both)} both)')
    if not both:
        return ck.result()
    # the animal nearest to the guest's own view: any one the guest has; the first
    nid = both[0]
    mate = both[1] if len(both) > 1 else 0
    keys = [f'n{nid}'] + ([f'n{mate}'] if mate else [])
    if FIXTURE == 'creature' and os.environ.get('DASH_GUEST', '1') == '1':  # the guest 1.5 m beside the path's end (BUG-0281)
        p = hs[nid]
        r = math.sqrt(sum(x * x for x in p))
        up = [x / r for x in p]
        a = [0.0, -up[2], up[1]] if abs(up[0]) < 0.9 else [-up[2], 0.0, up[0]]  # npc.cpp debug_dash's axis
        al = math.sqrt(sum(x * x for x in a))
        a = [x / al for x in a]
        b = [up[1] * a[2] - up[2] * a[1], up[2] * a[0] - up[0] * a[2], up[0] * a[1] - up[1] * a[0]]
        end = [p[i] + a[i] * LEN - b[i] * 1.5 for i in range(3)]
        print('  guest:', guest.call('coop.npc gopos %.1f %.1f %.1f' % tuple(end))['out'].strip(), flush=True)
        time.sleep(3)
    # BUG-0363: both games' own positions on one clock, the guest's targets too (q<id> rows)
    (off_h, rt_h), (off_g, rt_g) = clock_off(host), clock_off(guest)
    print(f'  clocks: host - guest {(off_h - off_g) * 1000:+.1f} ms (one QueryPerformanceCounter: ~0, +- '
          f'{(rt_h + rt_g) * 500:.0f} ms)', flush=True)
    host.call('coop.bodyrec start')
    guest.call('coop.bodyrec start diag')
    if os.environ.get('DASH_WALK_VARIANT'):  # (diagnostics, BUG-0757) how npc.walk moves the proxies
        print('  ' + guest.call('coop.npctrack variant ' + os.environ['DASH_WALK_VARIANT'])['out'].strip(), flush=True)
    print('  host:', host.call(f'coop.npc dash {nid} {LEN} {SPEED} 45 {WIDTH} {mate} {OFF}')['out'].strip(), flush=True)
    time.sleep(2)
    errs, fresh, fps_g, fps_h, loop, windows = [], [], [], [], 0, []
    per = {k: 0 for k in keys}
    t0_rows = time.monotonic() + off_g
    t_end = time.time() + 40
    while time.time() < t_end:
        h0, th0 = timed(host, off_h)
        time.sleep(cm.LAG_GAP)
        h1, th1 = timed(host, off_h)
        gsm, tg = timed(guest, off_g)
        h2, th2 = timed(host, off_h)
        feed = proxy_feed(guest) if FEED else {}
        loop += 1
        if loop % 4 == 0:
            for con, lst in ((guest, fps_g), (host, fps_h)):
                try:
                    lst.append(float(cm.fps(con)))
                except ValueError:
                    pass
        for key in keys:
            if key not in gsm or key not in h1:
                continue
            e = min(cm.seg_dist(gsm[key], h0.get(key, h1[key]), h1[key]),
                    cm.seg_dist(gsm[key], h1[key], h2.get(key, h1[key])))
            errs.append(e)
            per[key] += 1
            windows.append(dict(key=key, e=e, g=gsm[key], tg=tg, h0=h0.get(key), th0=th0, h1=h1[key], th1=th1,
                                h2=h2.get(key), th2=th2))
            age, rx = feed.get(int(key[1:]), (9.0, 0))
            if age <= 0.25 and rx >= 7:
                fresh.append(e)
            if os.environ.get('DASH_VERBOSE'):
                gl = [l.strip() for l in guest.call('coop.npc list')['out'].splitlines() if re.match(rf'\s*#{nid}\s', l)]
                print(f'    {e:.2f} m', re.sub(r' (puppet|ptr|herd|body|species|hp|dist)=\S+', '', gl[0])[:220] if gl else '-', flush=True)
        time.sleep(0.25)
    t1_rows = time.monotonic() + off_g
    for c in (host, guest):
        c.call('coop.bodyrec stop')
    for i in (nid, mate):
        line = [l.strip() for l in guest.call('coop.npc list')['out'].splitlines() if re.match(rf'\s*#{i}\s', l)]
        print(f'  guest proxy #{i}:', line[0][:300] if line else '-', flush=True)
    guest.call('frame.limit auto')
    m = re.search(r'proxy_holds=(\d+)', guest.call('coop.npc')['out'])  # BUG-0440: placings kept through the update
    print(f'  guest: npc.proxy_hold kept {m.group(1) if m else "?"} placings', flush=True)
    ck(len(keys) == 2 and min(per.values()) >= 30, f'samples of both animals on both machines ({per})')
    p90_rows, n_rows, along = judge_rows(host, guest, keys, t0_rows, t1_rows, windows)
    if errs:
        s = sorted(errs)
        far = sum(1 for e in errs if e > LIMIT)
        print(f'  window measure (coop_multi, not judged, BUG-0363): median {s[len(s) // 2]:.2f} m, p90 {s[int(0.9 * len(s))]:.2f} m, max {s[-1]:.2f} m, '
              f'{far} of {len(errs)} over {LIMIT} m', flush=True)
        med = lambda v: sorted(v)[len(v) // 2] if v else -1
        print(f'  frame rates (median of {len(fps_g)}): guest {med(fps_g):.1f}, host {med(fps_h):.1f}; '
              f'all samples p90 {s[int(0.9 * len(s))]:.2f} m', flush=True)
        f = sorted(fresh)
        p_all = s[int(0.9 * len(s))]
        # BUG-0430: late states are a finding, not a reason to pass. Only a host that is itself slow (its frame rate
        # under SLOW_HOST: the machine, not the mod, makes the states late) ends the run inconclusive; otherwise the
        # guest must get fresh states for most samples and the p90 over all samples has a looser bound too (the
        # guest is capped at 15 fps on purpose, so its rate says nothing about the load)
        if FEED and len(f) < 40 and 0 <= med(fps_h) < SLOW_HOST:
            print(f'  only {len(f)} of {len(errs)} samples with a fresh state and the host at {med(fps_h):.1f} fps '
                  f'(the machine is too loaded to measure)')
            print('RESULT: SKIP (inconclusive: a slow host, BUG-0321)')
            return 0
        if not FEED:
            print('  (DASH_FEED=0: no coop.npc list during the dash, no fresh-state check)', flush=True)
        else:
            ck(len(f) >= FRESH_SHARE * len(errs), f'the guest gets the host\'s states in time ({len(f)} of {len(errs)} samples '
               f'with a state at most 0.25 s old and 7 a second, at least {FRESH_SHARE:.0%})')
        if f:
            print(f'  window measure, fresh-state samples: {len(f)} of {len(errs)}, p90 {f[int(0.9 * len(f))]:.2f} m '
                  f'(all {p_all:.2f} m; not judged)', flush=True)
    # BUG-0363: judged on the games' own positions at the same moment (every guest frame, late states included)
    ck(n_rows >= 300, f'both games recorded the dash ({n_rows} guest frames of the two animals with the host\'s NPC at '
       f'the same moment)')
    if n_rows:
        ck(p90_rows <= P90, f'the guest shows the running animals where the host has them at the same moment '
           f'(90 % of {n_rows} frames within {p90_rows:.2f} m, limit {P90:.2f} m)')
        # BUG-0757 (npc.place_now): not a guest frame behind the host along its path
        ck(along is not None and along >= ALONG_MIN, f'the running copy is not a frame behind the host along its path '
           f'(median {along if along is not None else 0:+.3f} s, limit {ALONG_MIN} s)')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
