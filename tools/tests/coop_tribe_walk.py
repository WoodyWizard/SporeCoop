#!/usr/bin/env python3
"""What a player sees of the tribe members walking in Tribe, frame by frame (BUG-0365).

The host simulates every member; a guest shows mirrors of all of them (its own members too: their
orders go to the host), driven towards the host's latest snapshot. The owner's report on the
partner's creature in Creature (BUG-0360: "updates about every half second, mostly teleports,
sometimes walks") asked to re-check the other modes; the Tribe mirrors were driven by the same
`drive::step` that made the Creature puppet move in steps.

One player walks its members the way a player does (box-select one member or a group, right-click
on the ground, new orders while they walk, ~30 s), the other keeps them in view with the minimap;
both games sample the player tribe's members every frame (`coop.track`: in Tribe one row per member
with its id and owner; on a guest where the game drew the mirror, the target the driving aims at, the
host's speed). Directions: the host's members (the guest's mirrors), the guest's members (the host's
game, and the guest's own mirrors of them).

Checks for every view of mirrors, over the frames in which the host's state says the member walks:
* no jump: no frame step of the drawn member longer than TP_FACTOR x the members' top speed x the
  frame time (or the time of the frame before: a hitch of the game is made up in the next frame; and
  at least TP_MIN m);
* no steps: in 0.1 s windows the drawn member moves at 0.6-1.4x the speed of the target's path (the
  host's state the driving aims at) in at least MATCHED_MIN of them and stands (< 0.3x) in at most
  STAND_MAX;
* no stop-and-go: in the frames in which the target moves at 1-SLOW m/s (the member speeds up or
  slows down: every start and end of a walk) the drawn step is off the target's step of the frame before
  by more than half of it + 1 m/s x the frame time in at most SLOW_UNEVEN_MAX (stands, then surges);
* it walks: the animation's walk gait on in at least GAIT_MIN of the frames in which it is on the
  observer's screen.
The host's own game (real members, not mirrors) is measured the same way as a reference.

  coop_tribe_walk.py [--dir host|guest|both] [--seed N] [--secs S] [--fps N]
  --fps N: both games run at ~N frames a second (frame.limit 1000/N)
  SPORE_NET=mobile|awful: over a bad network (netem.py)
  (SPORE_FIX_OFF=tribe.mirror_walk must FAIL)"""
import argparse
import json
import math
import os
import random
import re
import threading
import time

import lane
from play_tribe import Player, screen as objs_on_screen, tribe_state
from twoinst import Checks, shot, start

TP_FACTOR, TP_MIN = 3.0, 0.75
STAND_MAX, MATCHED_MIN = 0.05, 0.85
GAIT_MIN = 0.80
# while the target moves at 1-SLOW m/s (the member speeds up or slows down: every start and end of a walk),
# per frame: uneven = the drawn step off the target's step of the frame before (what this frame draws
# was placed then) by more than half of it + 1 m/s x the frame time (0.7.3.3, drive::step: 14-29 %, it
# stands and surges; the fix: 1-5 %; the host's own game 0-5 %)
SLOW, SURGE = 5.0, 4.0
HITCH_QUIET = 0.2  # s after a frame over 0.1 s
# more frames over 0.1 s than this share of the observer's frames: the machine is too loaded to judge the
# steps (lab load 31 on 12 cores: 5-19 % of the frames, the rest jittering 25-120 ms; a calm machine,
# also at 30 fps over a mobile network: 0-2.2 %, the 0.7.3.3 driving's failing runs 0.1-1.6 %)
LOADED = 0.03
SLOW_UNEVEN_MAX = 0.10
# BUG-0407: a frame of the stop-and-go check whose time differs from the frame before's by more than STEADY x is left
# out (counted): the drawn step follows the frame before's time, the target's this frame's, and on a loaded machine
# (lab load 26-31 on 12 cores, frames jittering 15-90 ms) that alone made the host's own game, which no driving
# moves, "uneven" in 5-10 % of its frames and failed a fix-on run at 11 %. Left out: the host's own game 0-2 %, the
# fix 0-7 %, the 0.7.3.3 driving (tribe.mirror_walk,tribe.mirror_time off) unchanged 10-31 %
STEADY = 1.5
WALK_MORE = 2  # BUG-0407: walks of `secs` more when the first measured too little
W, H = 1440, 1080


# BUG-0394: a window spans at least WIN_FRAMES frames: at 21-29 fps a 0.1 s window held 1-3 frames and its speed
# ratio swung 0.3-2x with the frame times (the game moves a creature by its own frame time), 79-81 % matched
# with drawn errors of 0.01-0.6 m; over 4 frames 89-98 %. At 60 fps 4 frames are shorter than 0.1 s (unchanged);
# a puppet that stands 4 frames while its path moves still counts as standing
WIN_FRAMES = 4
# BUG-0394: the frame counts below (300 walking, 150 on the screen, 50 windows) were for 60 fps; --fps N scales them
FPS_K = 1.0

def dump(con):
    rows = []
    n = int(re.search(r'samples=(\d+)', con.call('coop.track count')['out']).group(1))
    for start_i in range(0, n, 1500):
        for line in con.call(f'coop.track dump {start_i} 1500')['out'].splitlines():
            f = line.split()
            if len(f) < 26:  # 23 columns, hd ht (BUG-0366), id (BUG-0365)
                continue
            rows.append({'t': float(f[0]), 'who': int(f[1]), 'flags': int(f[2]), 'tp': int(f[3]),
                         'p': tuple(map(float, f[4:7])), 'd': tuple(map(float, f[7:10])),
                         'tgt': tuple(map(float, f[10:13])), 'gv': float(f[13]), 'rv': float(f[14]),
                         'anim': f[15], 'gait': int(f[16]), 'ss': int(f[19]), 'vis': int(f[20]),
                         'sx': float(f[21]), 'sy': float(f[22]), 'hd': float(f[23]), 'ht': float(f[24]),
                         'id': int(f[25])})
    return rows


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else 0.0


def analyse(rows, owner, top, label):
    """the members of `owner` as this game drew them"""
    by = {}
    for r in rows:
        if r['who'] == owner and r['id']:
            by.setdefault(r['id'], []).append(r)
    frames, tps, steps, near, near_gait, win, errs, dts, drive_tp = 0, [], [], 0, 0, [], [], [], 0
    slow, slow_still, slow_surge, slow_uneven, hitches, skipped, target_jumps = 0, 0, 0, 0, 0, 0, 0
    jittery = 0
    walked = set()
    all_frames = 0
    for mid, rs in by.items():
        all_frames += max(0, len(rs) - 1)
        drive_tp += rs[-1]['tp'] - rs[0]['tp']
        prev_dt = 0
        # frames after a hitch of the game (a frame over 0.1 s: a loaded machine, a camera jump) show the
        # way made up and are no measure of the driving: steps and windows within HITCH_QUIET s after one
        # are left out of the stop-and-go and window checks (counted and printed)
        # Likewise after a jump of the target itself (faster than 1.5x the top speed: a late or lost state
        # over a bad network corrected the extrapolated path): the driving smooths it out over the next
        # frames on purpose
        calm, last_hitch = [], -1e9
        for k in range(len(rs)):
            if k:
                fdt = rs[k]['t'] - rs[k - 1]['t']
                if fdt > 0.1 or (fdt > 0 and math.dist(rs[k - 1]['tgt'], rs[k]['tgt']) / fdt > 1.5 * top):
                    last_hitch = rs[k]['t']
                    target_jumps += fdt <= 0.1
            calm.append(rs[k]['t'] - last_hitch > HITCH_QUIET)
        for k, (a, b) in enumerate(zip(rs, rs[1:])):
            dt = b['t'] - a['t']
            # what a frame draws was placed in the frame before: after a hitch of the game (a 100-230 ms
            # frame) the next short frame shows the way made up for it
            allow_dt, prev_dt = max(dt, prev_dt), dt
            if dt > 0.1:
                hitches += 1
            if dt <= 0 or dt > 0.25:
                continue
            dts.append(dt)
            if b['rv'] < 0.5:  # the host's state: it walks
                continue
            walked.add(mid)
            frames += 1
            step = math.dist(a['d'], b['d'])
            steps.append(step / dt)
            if step > max(TP_MIN, TP_FACTOR * top * allow_dt):
                tps.append((mid, round(b['t'], 2), round(step, 2), round(dt * 1000)))
            errs.append(math.dist(b['d'], b['tgt']))
            # what this frame draws was placed in the frame before: compare the drawn step with the
            # target's step of the frame before
            if not calm[k + 1]:
                skipped += 1
            elif k >= 1 and dt <= 0.1:
                a0 = rs[k - 1]
                dt0 = a['t'] - a0['t']
                ts = math.dist(a0['tgt'], a['tgt'])
                if 0 < dt0 <= 0.1 and 1.0 <= ts / dt0 < SLOW and STEADY and max(dt, dt0) > STEADY * min(dt, dt0):
                    jittery += 1
                elif 0 < dt0 <= 0.1 and 1.0 <= ts / dt0 < SLOW:  # the member speeds up or slows down
                    slow += 1
                    slow_still += step < 0.3 * ts
                    slow_surge += step > ts + SURGE * dt0
                    slow_uneven += abs(step - ts) > 0.5 * ts + 1.0 * dt0
            if b['vis'] and 0 < b['sx'] < W and 0 < b['sy'] < H:
                near += 1
                near_gait += b['gait'] > 0
        # 0.1 s windows while the host's member walks (its target's path > 1 m/s): drawn speed / path speed
        i = 0
        while i < len(rs):
            j = i
            while j < len(rs) and (rs[j]['t'] - rs[i]['t'] < 0.1 or j - i < WIN_FRAMES):
                j += 1
            if j >= len(rs):
                break
            dt = rs[j]['t'] - rs[i]['t']
            sp = math.dist(rs[i]['tgt'], rs[j]['tgt']) / dt if dt > 0 else 0
            if sp > 1.0 and min(r['rv'] for r in rs[i:j + 1]) > 0.5 and dt < 0.3 and all(calm[i:j + 1]):
                win.append(math.dist(rs[i]['d'], rs[j]['d']) / dt / sp)
            i = j
    return {
        'label': label, 'members': len(by), 'hitches': hitches, 'skipped': skipped, 'target_jumps': target_jumps,
        'loaded': hitches / all_frames if all_frames else 0, 'walked': sorted(walked), 'frames': frames,
        'fps': 1 / (sum(dts) / len(dts) / max(1, len(by))) if dts else 0, 'teleports': tps,
        'speed_p50': pct(steps, 0.5), 'speed_p99': pct(steps, 0.99), 'speed_max': max(steps) if steps else 0,
        'err_p50': pct(errs, 0.5), 'err_p90': pct(errs, 0.9), 'err_max': max(errs) if errs else 0,
        'near': near, 'near_gait': near_gait / near if near else 0,
        'slow': slow, 'slow_still': slow_still / slow if slow else 0, 'slow_surge': slow_surge / slow if slow else 0,
        'slow_uneven': slow_uneven / slow if slow else 0, 'jittery': jittery,
        'win': len(win), 'stand': sum(1 for w in win if w < 0.3) / len(win) if win else 1,
        'matched': sum(1 for w in win if 0.6 <= w <= 1.4) / len(win) if win else 0,
        'ratio_p10': pct(win, 0.1), 'ratio_p50': pct(win, 0.5), 'ratio_p90': pct(win, 0.9), 'drive_tp': drive_tp,
    }


def report(r, ck, mirrors=True):
    print(f'  {r["label"]}: {r["members"]} members, {len(r["walked"])} walked {r["walked"]}, {r["frames"]} frames '
          f'while they walk; frames over 0.1 s: {r["hitches"]}, target jumps (network corrections): {r["target_jumps"]} '
          f'({r["skipped"]} walking frames within {HITCH_QUIET} s after one left out of the step checks)', flush=True)
    print(f'    drawn speed p50 {r["speed_p50"]:.2f} p99 {r["speed_p99"]:.2f} max {r["speed_max"]:.1f} m/s; '
          f'off the target p50 {r["err_p50"]:.2f} p90 {r["err_p90"]:.2f} max {r["err_max"]:.2f} m; '
          f'driver teleports {r["drive_tp"]}', flush=True)
    print(f'    0.1 s windows while they walk: {r["win"]}, standing (< 0.3x the target path\'s speed) {r["stand"]:.0%}, '
          f'at 0.6-1.4x {r["matched"]:.0%} (ratio p10 {r["ratio_p10"]:.2f} p50 {r["ratio_p50"]:.2f} '
          f'p90 {r["ratio_p90"]:.2f})', flush=True)
    print(f'    frames while it speeds up or slows down (target 1-{SLOW:.0f} m/s, {r["jittery"]} more left out: a frame time '
          f'{STEADY}x off the one before): {r["slow"]}, uneven (the step off the '
          f'target\'s by more than half + 1 m/s) {r["slow_uneven"]:.0%}: standing {r["slow_still"]:.0%}, faster than '
          f'the target + {SURGE:.0f} m/s {r["slow_surge"]:.0%}', flush=True)
    print(f'    jumps (id, t, m, frame ms): {len(r["teleports"])} {r["teleports"][:12]}', flush=True)
    print(f'    walk gait in {r["near_gait"]:.0%} of the {r["near"]} frames on the screen', flush=True)
    if not mirrors:
        return
    ck(r['frames'] > 300 * FPS_K, f'{r["label"]}: the members walked ({r["frames"]} frames of the host\'s states moving)')
    ck(not r['teleports'], f'{r["label"]}: no jump of a member while it walks ({len(r["teleports"])})')
    if r['loaded'] > LOADED:
        print(f'  NOT MEASURED {r["label"]}: {r["loaded"]:.0%} of the frames over 0.1 s (> {LOADED:.0%}): the machine '
              f'is too loaded to judge the steps; the window and stop-and-go checks are skipped', flush=True)
        ck(r['near'] >= 150 * FPS_K and r['near_gait'] >= GAIT_MIN,
           f'{r["label"]}: the members walk where the player sees them (walk gait in {r["near_gait"]:.0%} >= '
           f'{GAIT_MIN:.0%} of {r["near"]} frames on the screen)')
        return
    ck(r['win'] > 50 * FPS_K and r['stand'] <= STAND_MAX and r['matched'] >= MATCHED_MIN,
       f'{r["label"]}: the members move with the host\'s, not in steps (stand in {r["stand"]:.0%} <= {STAND_MAX:.0%}, '
       f'at its speed in {r["matched"]:.0%} >= {MATCHED_MIN:.0%} of {r["win"]} windows)')
    if r['slow'] < 40:  # (a few walks at 30 fps: 16-26 such frames; 3 uneven ones are no measure)
        print(f'  NOT MEASURED {r["label"]}: only {r["slow"]} frames at 1-{SLOW:.0f} m/s, the stop-and-go check is '
              f'skipped', flush=True)
    else:
        ck(r['slow_uneven'] <= SLOW_UNEVEN_MAX,
           f'{r["label"]}: no stop-and-go while the members start and stop (uneven in {r["slow_uneven"]:.0%} <= '
           f'{SLOW_UNEVEN_MAX:.0%} of {r["slow"]} frames at 1-{SLOW:.0f} m/s)')
    ck(r['near'] >= 150 * FPS_K and r['near_gait'] >= GAIT_MIN,
       f'{r["label"]}: the members walk where the player sees them (walk gait in {r["near_gait"]:.0%} >= '
       f'{GAIT_MIN:.0%} of {r["near"]} frames on the screen)')


def own(con, pid):
    return {i: v for i, v in tribe_state(con).items() if v['tribe'] == 'p' and v['owner'] == pid and v['hp'] > 0}


def last_order(pl):
    out = pl.con.call('coop.status')['out']
    if pl.pid == 0:
        m = re.search(r'tribe last local order: #(\d+) (.*)', out)
        return (m.group(2), int(m.group(1))) if m else ('', 0)
    m = re.search(r'tribe last order: (.*)', out)
    n = re.search(r'cmds_sent=(\d+)', out)
    return (m.group(1) if m else ''), (int(n.group(1)) if n else 0)


def walk(pl, rng, secs):
    """a player walking its members: box-select one or a group, right-click the ground some way off,
    sometimes a new order before they arrive"""
    t_end = time.time() + secs
    log = []
    while time.time() < t_end:
        mine = own(pl.con, pl.pid)
        if not mine:
            log.append('no members')
            time.sleep(1)
            continue
        group = rng.random() < 0.5
        for i in mine:  # a fresh selection (a click on the ground would keep the old one)
            pl.con.call(f'coop.tribe select {i} 0')
        m = rng.choice(sorted(mine))
        pl.look(mine[m]['pos'], tol=15)
        objs = objs_on_screen(pl.con)
        sel = pl.select(list(mine) if group else [m], objs, group=group)
        if not sel:
            log.append('no selection')
            pl.cur = None
            misses = getattr(pl, 'misses', 0) + 1
            pl.misses = misses
            if misses % 3 == 0:  # the minimap calibration may have failed at the start (seen once)
                log.append('calibrate ' + ('ok' if pl.calibrate() else 'failed'))
            continue
        on = [o for o in pl.on_screen(objs) if o[0].startswith('member') and int(o[0][6:]) in sel]
        cx = sum(o[1] for o in on) / len(on) if on else W / 2
        cy = sum(o[2] for o in on) / len(on) if on else H / 2
        busy = [(o[1], o[2]) for o in objs if not o[0].startswith('tree') and o[0] != 'tree']
        orders = 1 + (rng.random() < 0.4)
        for k in range(orders):
            for _ in range(12):  # the ground some way off, in the open (not on a member, the hut, a tree)
                ang = rng.uniform(0, 2 * math.pi)
                d = rng.uniform(220, 420)
                x, y = int(cx + d * math.cos(ang)), int(cy + 0.7 * d * math.sin(ang))
                if 250 < x < W - 250 and 250 < y < H - 280 and all(math.hypot(x - bx, y - by) > 60 for bx, by in busy):
                    break
            else:
                x, y = W // 2, H // 2 - 100
            n0 = last_order(pl)[1]
            pl.con.call(f'move {x} {y}')
            time.sleep(0.3)
            pl.con.call(f'click {x} {y} right')
            time.sleep(0.5)
            last, n = last_order(pl)
            kind = 'walk' if n != n0 and ('walk' in last or 'kind=1' in last) else ('order ' + last[:40] if n != n0 else 'nothing')
            wait = rng.uniform(1.5, 3.0) if k + 1 < orders else rng.uniform(3.5, 6.0)
            log.append(f'{"group" if group else "one"} {sel} -> ({x},{y}) {kind} {wait:.1f}s')
            time.sleep(wait)
            cx, cy = x, y
    return log


class Watch(threading.Thread):
    """the observer keeps the walker's members in view (the minimap), as a player watching them"""
    def __init__(self, pl, owner):
        super().__init__(daemon=True)
        self.pl, self.owner, self.stop, self.looks = pl, owner, threading.Event(), 0
        self.lock = threading.Lock()  # BUG-0407: the main thread reads the observer's track while it watches

    def run(self):
        while not self.stop.is_set():
            try:
                with self.lock:
                    ms = own(self.pl.con, self.owner)
                    if ms:
                        c = [sum(v['pos'][i] for v in ms.values()) / len(ms) for i in range(3)]
                        if not self.pl.cur or math.dist(self.pl.cur, c) > 12:
                            self.pl.look(c, tol=12)
                            self.looks += 1
            except OSError as e:  # BUG-0407: a console call that timed out on a loaded machine
                print(f'  watch: {e!r}', flush=True)
            self.stop.wait(1.0)


def direction(walker, observer, rng, secs, ck, host_pl):
    for i in own(observer.con, observer.pid):
        observer.con.call(f'coop.tribe select {i} 0')
    watch = Watch(observer, walker.pid)
    watch.start()
    time.sleep(2.0)
    for p in (walker, observer):
        p.con.call('coop.track start')
    time.sleep(0.3)
    log = walk(walker, rng, secs)
    # BUG-0407: a walk that measured too little on the guest (a loaded machine at 21-29 fps, clicks that gave no
    # walk: 294 frames, 135 on the screen) is walked on instead of failing checks that measured nothing
    for _ in range(WALK_MORE):
        with watch.lock:
            hrows = dump(host_pl.con)
            grows = dump(observer.con if observer.name == 'guest' else walker.con)
        top = pct([r['rv'] for r in hrows if r['who'] == walker.pid and r['id'] and r['rv'] > 0.3], 0.95) or 5.0
        r = analyse(grows, walker.pid, top, '')
        if r['frames'] > 300 * FPS_K and r['near'] >= 150 * FPS_K and r['win'] > 50 * FPS_K:
            break
        print(f'  measured too little on the guest ({r["frames"]} frames while they walk, {r["near"]} on the screen, '
              f'{r["win"]} windows): {walker.name} walks on', flush=True)
        log.append('| on')
        log += walk(walker, rng, secs)
    watch.stop.set()
    watch.join()
    time.sleep(1.0)
    shot(observer.con, f'tribe_walk_{observer.name}_sees_{walker.name}')
    for p in (walker, observer):
        p.con.call('coop.track stop')
    rows = {p.name: dump(p.con) for p in (walker, observer)}
    path = os.path.join(lane.out_dir('track'), f'tribe_walk_{walker.name}_walks_{time.strftime("%H%M%S")}.json')
    with open(path, 'w') as f:
        json.dump(rows, f)
    print(f'  per-frame samples: {path}', flush=True)
    print(f'  {walker.name} walked: {"; ".join(log)}', flush=True)
    print(f'  {observer.name} looked {watch.looks} times', flush=True)
    hrows = rows[host_pl.name]
    top = pct([r['rv'] for r in hrows if r['who'] == walker.pid and r['id'] and r['rv'] > 0.3], 0.95) or 5.0
    print(f'  the members\' top speed on the host {top:.2f} m/s', flush=True)
    report(analyse(hrows, walker.pid, top, f'host game: {walker.name}\'s members (the game\'s own, reference)'), ck,
           mirrors=False)
    grows = rows['guest']
    who = 'the host\'s members' if walker.pid == 0 else 'its own members'
    report(analyse(grows, walker.pid, top, f'guest sees {who} (mirrors)'), ck)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default='both', choices=('host', 'guest', 'both'), help='who walks')
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--secs', type=float, default=30)
    ap.add_argument('--fps', type=float, default=0)
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  seed {seed}', flush=True)
    rng = random.Random(seed)
    host, guest = start('tribe')
    ck = Checks()
    # the fixture's tribe has 3 members; 3 more give each player 3 to walk (as play_tribe.py)
    print('  setup:', host.call('coop.tribe spawn 3')['out'], flush=True)
    t0 = time.time()
    while time.time() - t0 < 90:
        if len(own(guest, 1)) >= 2 and len(own(host, 0)) >= 2:
            break
        time.sleep(2)
    time.sleep(3)
    print(f'  host owns {sorted(own(host, 0))}, guest owns {sorted(own(guest, 1))}', flush=True)
    global FPS_K
    if a.fps:
        FPS_K = min(1.0, a.fps / 60)
        for c in (host, guest):
            print('  frame.limit:', c.call(f'frame.limit {round(1000 / a.fps)}')['out'].strip(), flush=True)
    log = []
    hp = Player('host', host, 0, random.Random(seed + 1), log, W, H)
    gp = Player('guest', guest, 1, random.Random(seed + 2), log, W, H)
    for p in (hp, gp):
        ok = any(p.calibrate() for _ in range(3))
        ck(ok, f'{p.name}: the minimap moves the view')
    if a.dir in ('host', 'both'):
        direction(hp, gp, rng, a.secs, ck, hp)
    if a.dir in ('guest', 'both'):
        direction(gp, hp, rng, a.secs, ck, hp)
    if a.fps:
        for c in (host, guest):
            c.call('frame.limit auto')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
