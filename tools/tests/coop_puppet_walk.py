#!/usr/bin/env python3
"""What a player sees of the partner's creature while the partner walks by real input (BUG-0360).

Player report (0.7.3.2, Creature, 2 players, 60 fps, rtt 20-30 ms): "the partner's character
teleports: sometimes it moves smoothly, mostly it jumps, about every half second". The other tests
compare positions at a few moments (coop.sync); this one samples the puppet in the observer's game
every frame (`coop.track`: the game's position, where the model is drawn, the game's speed of the
creature, its animation and the animation's walk gait) while the other player walks with WASD keys
and clicks on the ground (turns, stops, ~25 s), in both directions (the guest watches the host, the
host watches the guest).

Checks per direction, over the time in which the walker's latest state says it moves:
* no jump: no frame step of the drawn puppet longer than TP_FACTOR x the walker's top speed x the
  frame time or the frame before's (a hitch is made up in the next frame; and at least TP_MIN m);
* no steps: in 0.1 s windows the drawn puppet moves at 0.6-1.4x the speed of the walker's path (the
  interpolated target) in at least MATCHED_MIN of them and stands (< 0.3x) in at most STAND_MAX (the old driving: 18-23 % standing,
  57-63 % matched: the "updates about every half second, not steadily");
* it walks: the animation's walk gait (Anim::AnimatedCreature +0x17C -> +0x15B8, what the game's
  idle chooser 0xC0C790 reads; > 0 on a walking avatar in 94-97 % of its moving frames) is on in at
  least GAIT_MIN of the frames in which the puppet is on the observer's screen (the walker stays
  within TETHER m, the observer's camera follows it: a creature behind the camera is animated rarely).
The drawn position is the puppet's position before the mod's frame (the mod runs at Present).

  coop_puppet_walk.py [--dir host|guest|both] [--seed N] [--secs S]
  PUPPET_WALK_CMDS="cmd;cmd": console commands on both games before the walks (experiments)
  (SPORE_FIX_OFF=creature.puppet_walk must FAIL)"""
import argparse
import math
import random
import re
import threading
import time

import json
import os

import lane
from twoinst import Checks, shot, start

TP_FACTOR, TP_MIN = 3.0, 0.75
# Anim::AnimatedCreature::IsIdleWalk (ModAPI Animations.cpp): the main animations of a walking
# creature (idle_walk_lookat_start / _loop / _stop, idle_walkstop_01..03, csa_ and epic_ variants)
WALK_ANIMS = {0x04866D8E, 0x0452B634, 0x05CB2176, 0x04866DB9, 0x04866DC3, 0x04866DCB, 0x04866DD4, 0x05CB21AE,
              0x05CB21AF, 0x05CB21B0, 0x0452B651, 0x04482911, 0x04482918, 0x0448291F, 0x04866DAB, 0x0452B64A,
              0x05CB2177, 0x044828E9, 0x04482906}
STAND_MAX, MATCHED_MIN = 0.05, 0.85
GAIT_MIN = 0.80
GAIT_PPM = 10.0  # BUG-0436: pixels per metre on the observer's screen (a walking puppet near its camera: 25-90)


# BUG-0394: a window spans at least WIN_FRAMES frames: at 21-29 fps a 0.1 s window held 1-3 frames and its speed
# ratio swung 0.3-2x with the frame times (the game moves a creature by its own frame time), 79-81 % matched
# with drawn errors of 0.01-0.6 m; over 4 frames 89-98 %. At 60 fps 4 frames are shorter than 0.1 s (unchanged);
# a puppet that stands 4 frames while its path moves still counts as standing
WIN_FRAMES = 4

def pup_rows(rows):
    return [r for r in rows if r['who'] != 255]


def screen(con):
    m = re.search(r'viewport x=\d+ y=\d+ w=(\d+) h=(\d+)', con.call('coop.cam')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else (1440, 1080)


def dump(con):
    rows = []
    n = int(re.search(r'samples=(\d+)', con.call('coop.track count')['out']).group(1))
    for start_i in range(0, n, 1500):
        for line in con.call(f'coop.track dump {start_i} 1500')['out'].splitlines():
            f = line.split()
            if len(f) < 23:  # 23 columns, then hd ht (BUG-0366), then id (BUG-0365): new ones go at the end
                continue
            rows.append({'hd': float(f[23]) if len(f) > 23 else None, 'ht': float(f[24]) if len(f) > 23 else None,
                         't': float(f[0]), 'who': int(f[1]), 'flags': int(f[2]), 'tp': int(f[3]),
                         'p': tuple(map(float, f[4:7])), 'd': tuple(map(float, f[7:10])),
                         'tgt': tuple(map(float, f[10:13])), 'gv': float(f[13]), 'rv': float(f[14]),
                         'anim': f[15], 'gait': int(f[16]), 'gait2': int(f[17]), 'moving': int(f[18]),
                         'ss': int(f[19]), 'vis': int(f[20]), 'sx': float(f[21]), 'sy': float(f[22]),
                         'ppm': float(f[28]) if len(f) > 28 else None})
    return rows


def partner_dist(con):
    """distance from our avatar to the partner's latest state (m), None when unknown"""
    a = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    b = re.search(r'remote \d+ .*? pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.status')['out'])
    return math.dist(tuple(map(float, a.groups())), tuple(map(float, b.groups()))) if a and b else None


def partner_screen(con):
    for line in con.call('coop.targets all')['out'].splitlines():
        f = line.split()
        if len(f) == 5 and f[0] == 'player':
            return int(f[3]), int(f[4])
    return None


def back_to_partner(con, W, H):
    """walk back next to the partner (a click on the ground at its tag; the camera turned until it
    is on the screen): the observer's game animates a creature far behind its camera less (the walk
    gait stayed off for a whole walk 20-45 m behind it)"""
    for _ in range(5):
        xy = partner_screen(con)
        if xy and 40 < xy[0] < W - 40 and 90 < xy[1] < H - 170:
            d0 = partner_dist(con)
            con.call(f'click {xy[0]} {min(H - 180, xy[1] + 60)}')
            time.sleep(2.0)
            d1 = partner_dist(con)
            if d0 is not None and d1 is not None and d1 > d0 - 2:
                # BUG-0394: the click did not bring it closer (seen 7 times in a row at 14 m): it walks there
                # with W (the partner is in front of the camera now)
                con.call('key 0x57 down')
                time.sleep(2.0)
                con.call('key 0x57 up')
            return True
        con.call(f'drag {W // 2} {H // 2} {W // 2 + 380} {H // 2} right')
        time.sleep(0.4)
    return False


TETHER = 12.0  # m: the walker turns back to the observer beyond this
WALK_MORE = 2  # BUG-0407: walks of `secs` more when the first measured too little


HOP = 1.5  # m
STOP_WALK = False  # --stop-walk (BUG-0408)
MOUND = None  # --mound x,y,z (BUG-0435): the walker stands there MOUND_LIFT m up (it lands on the mound) first
MOUND_LIFT, MOUND_STAND = 0.5, 10.0  # m, s
IN_MOUND, IN_MOUND_MAX = 0.15, 0.1  # m drawn under the target's height, share of the frames


def jump(con):
    """a jump: the fixture's creatures have no jump ability (Space does nothing, no ability of theirs
    leaves the ground), so the walker's avatar is put HOP m higher and falls back by the game's
    gravity (~1 s): its state carries the fall's mostly vertical velocity, as a jump's does"""
    con.call(f'coop.abilities lift {HOP}')


def walk(con, rng, secs, jumps=0.0):
    """a player walking: WASD holds with turns, stops, clicks on the ground ahead, staying near the
    partner"""
    W, H = screen(con)
    t_end = time.time() + secs
    log = []
    lost = 0
    while time.time() < t_end:
        d = partner_dist(con)
        if d is not None and d > TETHER:
            found = back_to_partner(con, W, H)
            log.append(f'back {d:.0f}m' + ('' if found else ' (not found)'))
            lost = 0 if found else lost + 1
            if lost >= 2:  # the partner's tag is nowhere on the screen (the camera behind a hill): go there
                # (latency_step_ingame, lab 20261101-180254: 'back 17m (not found)' for the whole walk)
                b = re.search(r'remote \d+ .*? pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.status')['out'])
                if b:
                    q = [float(x) for x in b.groups()]
                    r = math.sqrt(sum(x * x for x in q))
                    q = [x + x / r * 1.0 for x in q]  # a metre up: it falls onto the ground
                    con.call('coop.npc gopos %.1f %.1f %.1f' % (q[0] + 3, q[1], q[2]))
                    log.append('to the partner (gopos)')
                    time.sleep(2.0)
                lost = 0
            continue
        r = rng.random()
        if STOP_WALK:  # BUG-0408: walks of 1-2.5 s by W (and a turn), each followed by a stop
            keys = rng.choice([['W'], ['W'], ['W', 'A'], ['W', 'D']])
            hold = rng.uniform(1.0, 2.5)
            for k in keys:
                con.call(f'key 0x{ord(k):02X} down')
            time.sleep(hold)
            halt = rng.random() < 0.6
            for k in keys:
                con.call(f'key 0x{ord(k):02X} up')
            if halt:  # it stops at once (a partner's creature stopped by something, not slowing down by itself)
                con.call('coop.abilities halt')
            time.sleep(1.4)
            log.append(f'{"+".join(keys)} {hold:.1f}s, {"halt" if halt else "stop"}')
            continue
        if r < 0.45:
            keys = rng.choice([['W'], ['W'], ['W', 'A'], ['W', 'D'], ['A'], ['D'], ['S']])
            hold = rng.uniform(1.0, 3.0)
            for k in keys:
                con.call(f'key 0x{ord(k):02X} down')
            if jumps and rng.random() < jumps:  # BUG-0366: it jumps while it walks
                t1 = time.time() + hold
                n = 0
                while time.time() < t1 - 0.3:
                    jump(con)
                    n += 1
                    time.sleep(min(rng.uniform(0.6, 1.2), max(0.0, t1 - time.time())))
                keys = keys + [f'jump x{n}']
            else:
                time.sleep(hold)
            for k in keys:
                if len(k) == 1:
                    con.call(f'key 0x{ord(k):02X} up')
            log.append(f'{"+".join(keys)} {hold:.1f}s')
        elif r < 0.85:
            x, y = rng.randint(int(W * 0.2), int(W * 0.8)), rng.randint(int(H * 0.45), int(H * 0.75))
            con.call(f'click {x} {y}')
            wait = rng.uniform(1.5, 3.0)
            time.sleep(wait)
            log.append(f'click {x},{y} {wait:.1f}s')
        elif jumps and r < 0.93:  # BUG-0366: jumps on the spot
            n = rng.randint(2, 4)
            for _ in range(n):
                jump(con)
                time.sleep(rng.uniform(0.7, 1.3))
            log.append(f'jumps x{n}')
        else:
            stop = rng.uniform(0.5, 1.5)
            time.sleep(stop)
            log.append(f'stop {stop:.1f}s')
    return log


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else 0.0


FALL_ANIM = '0692E6A9'  # the game's fall animation (an airborne creature, +0x137 = 0)
UNDER = -0.3  # m: a drawn puppet this far under the observer's ground is under it


def falls(rows):
    """how often the creature starts to fall (the game plays its fall animation): a creature that walks
    on the ground never does; the partner's puppet did walking downhill (BUG-0366)"""
    n, prev = 0, False
    for r in rows:
        a = r['anim'] == FALL_ANIM
        n += a and not prev
        prev = a
    return n


WIN_SLACK = 0.1  # m (BUG-0407)
# BUG-0407: more of the observer's frames over HITCH than this: not measured (the window check skipped, printed). A
# guest at 11 fps with 41 % of its frames over 0.1 s (lab load 26 on 12 cores) left 5 windows; the recorded runs that
# were judged: 0-10 %, the 0.7.3.2 driving's FAIL runs 0-1 %
LOADED = 0.20
HITCH = 0.1  # s: an observer frame this long is a hitch of its game (a loaded machine), not the driving's


def windows(pup, top):
    """0.1 s windows (at least WIN_FRAMES frames) while the walker's path moves (> 1 m/s): the drawn puppet's
    way / the path's way (the path: the interpolated target; the reported speed is no measure, an avatar
    turning on the spot reports one). Returns (ratios, windows left out for a hitch, windows left out for a
    jump of the target itself, the ratios as measured before BUG-0407).
    BUG-0407 (the walk tests were flaky on a loaded machine, puppet_walk 81-91 % matched at 22-24 fps):
    * what a frame draws was placed in the frame before (the mod runs at Present, after the drawing): the
      drawn way of frames i+1..j+1 is compared with the path's way of frames i..j. Measured unshifted, a
      window that ends after a long frame and starts after a short one swung 0.5-2.4x at 22-24 fps with drawn
      errors of 0.1-0.4 m (no visible step); shifted 90-96 % matched there, the old driving unchanged (15-35 %);
    * a window with an observer frame over HITCH (in it or just before it) is left out and counted: after a
      hitch the game moves the creature by the long frame's time and the next frames make up the rest (with
      10 % of the frames over 0.1 s, a 0.1-0.2 s window held 1-2 of them);
    * a window in which the target itself jumped (a late state corrected the extrapolated path) is left out
      and counted, as in npc_walk: it measures the network, the driving closes such a jump on purpose."""
    win, legacy, hitch, tj = [], [], 0, 0
    n, i = len(pup), 0
    while i < n:
        j = i
        while j < n and (pup[j]['t'] - pup[i]['t'] < 0.1 or j - i < WIN_FRAMES):
            j += 1
        if j >= n - 1:
            break
        dt = pup[j]['t'] - pup[i]['t']
        way = math.dist(pup[i]['tgt'], pup[j]['tgt'])
        if dt > 0 and way / dt > 1.0 and dt < 0.3 and (pup[j]['flags'] & 1):
            legacy.append(math.dist(pup[i]['d'], pup[j]['d']) / way)
            fts = [pup[k + 1]['t'] - pup[k]['t'] for k in range(max(0, i - 1), j + 1)]
            if max(fts) > HITCH:
                hitch += 1
            elif any(math.dist(pup[k]['tgt'], pup[k + 1]['tgt']) >
                     max(0.3, 2.5 * top * (pup[k + 1]['t'] - pup[k]['t'])) for k in range(i, j)):
                tj += 1
            else:
                dd = math.dist(pup[i + 1]['d'], pup[j + 1]['d'])
                # a way within WIN_SLACK of the path's counts as at its speed (a 0.15-0.3 s window at the start or
                # end of a walk, 1-2 m/s: 0.4-1.7x were 0.1-0.17 m apart, less than one step of the creature)
                win.append(1.0 if abs(dd - way) <= WIN_SLACK else dd / way)
        i = j
    return win, hitch, tj, legacy


STOP_MOVE, STOP_MOVES = 0.15, 0.1  # BUG-0408 (--stop-walk): m slid on after a stop, share of the stops
STOP_SLIDE = 0.3  # m: a puppet drawn this far from where the walker stopped in the second after the stop slid past it


def stops(pup):
    """BUG-0408: per stop of the walker (its path moved > 1.5 m/s over 0.3 s, then < 0.15 m in the next second): the
    largest drawn distance from the path in that second (over the one at the stop) and whether the fall animation played"""
    ev, k, n = [], 1, len(pup)
    while k < n - 5:
        prev = [r for r in pup[max(0, k - 40):k] if r['t'] > pup[k]['t'] - 0.3]
        if len(prev) >= 3 and math.dist(prev[0]['tgt'], pup[k]['tgt']) / (pup[k]['t'] - prev[0]['t']) > 1.5:
            post = [r for r in pup[k:k + 200] if r['t'] < pup[k]['t'] + 1.0]
            if len(post) > 5 and math.dist(post[0]['tgt'], post[-1]['tgt']) < 0.15:
                # (less the error at the stop: puppet_under's targets are 1 m under the ground all the time)
                e0 = math.dist(pup[k]['d'], pup[k]['tgt'])
                # the drawn way after the target stood (from its third frame on: what the frame before placed),
                # less the target's own way then: a puppet that slides on after the stop
                slide = (sum(math.dist(a['d'], b['d']) for a, b in zip(post[2:], post[3:])) -
                         sum(math.dist(a['tgt'], b['tgt']) for a, b in zip(post[1:], post[2:-1])))
                ev.append((round(pup[k]['t'], 1), max(math.dist(r['d'], r['tgt']) for r in post) - e0,
                           any(r['anim'] == FALL_ANIM for r in post), round(slide, 2)))
                k += len(post)
                continue
        k += 1
    return ev


AIR_MIN, AIR_PEAK, AIR_SHOWN = 0.4, 0.6, 0.5  # m, m, share of the peak


def airs(pup):
    """BUG-0376: the walker's jumps as the observer's game sees them: episodes in which the walker's target (its own
    position) is more than AIR_MIN over the observer's ground (peak at least AIR_PEAK), and whether the drawn puppet rose
    to AIR_SHOWN of that peak in the episode or the 0.2 s after it (the puppet that stays on the ground: 0 m)"""
    ev, i, n = [], 0, len(pup)
    while i < n:
        if pup[i].get('ht') is None or pup[i]['ht'] <= AIR_MIN:
            i += 1
            continue
        j = i
        while j + 1 < n and pup[j + 1].get('ht') is not None and pup[j + 1]['ht'] > AIR_MIN:
            j += 1
        peak = max(r['ht'] for r in pup[i:j + 1])
        drawn = max(r['hd'] for r in pup[i:n] if r['t'] <= pup[j]['t'] + 0.2)
        if peak >= AIR_PEAK and j - i >= 2:
            ev.append((round(pup[i]['t'], 1), round(peak, 2), round(drawn, 2), drawn >= AIR_SHOWN * peak))
        i = j + 1
    return ev


def air_frames(pup):
    """BUG-0376, frame by frame: the frames in which the walker's target is more than AIR_MIN over the observer's ground,
    and those of them in which the next frame draws the puppet at least AIR_SHOWN as high (what a frame draws was placed
    in the frame before)"""
    n = ok = 0
    for a, b in zip(pup, pup[1:]):
        if a.get('ht') is not None and a['ht'] > AIR_MIN:
            n += 1
            ok += b['hd'] >= AIR_SHOWN * a['ht']
    return n, ok


def analyse(obs_rows, walker_rows, label, air_exempt=False):
    """per-frame motion of the walker's puppet in the observer's game"""
    own = [r for r in walker_rows if r['who'] == 255]
    top = pct([r['gv'] for r in own if r['gv'] > 0.3], 0.95) or 5.0  # the walker's own top speed
    pup = [r for r in obs_rows if r['who'] != 255]
    frames, moving, anim_ok, walk_anim, gait, tps, steps, dts = 0, 0, 0, 0, 0, [], [], []
    anims_moving = {}
    prev_dt = 0
    for a, b in zip(pup, pup[1:]):
        dt = b['t'] - a['t']
        # BUG-0365: what a frame draws was placed in the frame before; after a hitch (a 100-250 ms frame on
        # a loaded machine) the next short frame shows the way made up (0.77 m in 16 ms after 108 ms)
        allow_dt, prev_dt = max(dt, prev_dt), dt
        if dt <= 0 or dt > 0.25:
            continue
        dts.append(dt)
        if b['rv'] < 0.5 or not (b['flags'] & 1):  # the walker's state: it walks (PF_IN_WORLD)
            continue
        frames += 1
        step = math.dist(a['d'], b['d'])
        steps.append(step / dt)
        if step > max(TP_MIN, TP_FACTOR * top * allow_dt):
            # BUG-0376: the walker's own creature put 1.5 m up (coop.abilities lift: the hops) is a step of its
            # target too; the puppet that follows it to its target is no jump of the driving
            k = pup.index(b)
            tj = [math.dist(pup[m]['tgt'], pup[m + 1]['tgt']) for m in range(max(0, k - 12), k)
                  if math.dist(pup[m]['tgt'], pup[m + 1]['tgt']) > max(0.3, 2.5 * top * max(0.001, pup[m + 1]['t'] - pup[m]['t']))]
            if not (air_exempt and tj and (step <= sum(tj) + 0.3 or math.dist(b['d'], b['tgt']) < 0.3)):
                tps.append((round(b['t'], 2), round(step, 2), round(dt * 1000)))
        if step >= 0.2 * b['rv'] * dt:
            moving += 1
        if b['gv'] > 0.5:
            anim_ok += 1
        if int(b['anim'], 16) in WALK_ANIMS:
            walk_anim += 1
        if b['gait'] > 0:
            gait += 1
        anims_moving[b['anim']] = anims_moving.get(b['anim'], 0) + 1
    errs = [math.dist(r['d'], r['tgt']) for r in pup if r['rv'] > 0.5]
    # the walk gait over the frames in which the puppet is on the observer's screen
    obs_av, near, near_gait = None, 0, 0
    for r in obs_rows:
        if r['who'] == 255:
            obs_av = r
        elif r['rv'] > 0.5 and (r['flags'] & 1) and obs_av:
            # BUG-0436: and not tiny there: in the three runs of that ticket the observer's camera was hundreds of m away
            # (a 30 m walk spanned 20-30 pixels) and the game animated the puppet (and its NPC proxies) without a gait
            if r['vis'] and 0 < r['sx'] < 1440 and 0 < r['sy'] < 1080 and (r.get('ppm') is None or r['ppm'] >= GAIT_PPM):
                near += 1
                near_gait += r['gait'] > 0
    win, win_hitch, win_tj, win_legacy = windows(pup, top)
    own_anims = {}
    own_moving = [r for r in own if r['gv'] > 0.5]
    own_walk = sum(1 for r in own_moving if int(r['anim'], 16) in WALK_ANIMS) / len(own_moving) if own_moving else 0
    own_gait = sum(1 for r in own_moving if r['gait'] > 0) / len(own_moving) if own_moving else 0
    for r in own:
        if r['gv'] > 0.5:
            own_anims[r['anim']] = own_anims.get(r['anim'], 0) + 1
    res = {
        'label': label, 'frames': frames, 'samples': len(pup), 'top': top,
        'fps': 1 / (sum(dts) / len(dts)) if dts else 0,
        'teleports': tps, 'moving': moving / frames if frames else 0, 'anim': anim_ok / frames if frames else 0,
        'speed_p50': pct(steps, 0.5), 'speed_p99': pct(steps, 0.99), 'speed_max': max(steps) if steps else 0,
        'err_p50': pct(errs, 0.5), 'err_p90': pct(errs, 0.9), 'err_max': max(errs) if errs else 0,
        'walk_anim': walk_anim / frames if frames else 0, 'own_walk': own_walk,
        'gait': gait / frames if frames else 0, 'own_gait': own_gait,
        'near': near, 'near_gait': near_gait / near if near else 0,
        'win': len(win), 'stand': sum(1 for w in win if w < 0.3) / len(win) if win else 1,
        'matched': sum(1 for w in win if 0.6 <= w <= 1.4) / len(win) if win else 0,
        'win_hitch': win_hitch, 'win_tj': win_tj, 'stops': stops(pup),
        'hitch_share': (sum(1 for a, b in zip(pup, pup[1:]) if b['t'] - a['t'] > HITCH) / (len(pup) - 1)
                        if len(pup) > 1 else 0),
        'legacy_matched': sum(1 for w in win_legacy if 0.6 <= w <= 1.4) / len(win_legacy) if win_legacy else 0,
        'legacy_win': len(win_legacy),
        'drive_tp': (pup[-1]['tp'] - pup[0]['tp']) if pup else 0,
        'anims_moving': sorted(anims_moving.items(), key=lambda kv: -kv[1])[:5],
        'own_anims': sorted(own_anims.items(), key=lambda kv: -kv[1])[:5],
        # BUG-0366 / 0367: the game's fall (+0x137 = 0 with the fall animation) and the height over the
        # observer's ground
        'falls': falls(pup), 'own_falls': falls(own),
        'under': [round(r['hd'], 2) for r in pup if r.get('hd') is not None and r['hd'] < UNDER],
        'hd_min': min((r['hd'] for r in pup if r.get('hd') is not None), default=0.0),
        'airs': airs(pup), 'air_frames': air_frames(pup),
    }
    return res


class Watch(threading.Thread):
    """the observer keeps the walker on its screen (right-drag turns its camera), as a player who
    watches the partner does: the game animates a creature behind the camera rarely, so its walk
    gait there says nothing (it stayed off for 18 s of walking 8-14 m behind the camera, then on
    while it stood)"""
    def __init__(self, con):
        super().__init__(daemon=True)
        self.con, self.stop, self.drags = con, threading.Event(), 0
        self.W, self.H = screen(con)
        self.lock = threading.Lock()  # BUG-0407: the main thread reads the observer's track while it watches

    def run(self):
        while not self.stop.is_set():
            try:
                with self.lock:
                    xy = partner_screen(self.con)
                    if not xy or not (60 < xy[0] < self.W - 60 and 60 < xy[1] < self.H - 170):
                        dx = 300 if not xy or xy[0] > self.W // 2 else -300
                        self.con.call(f'drag {self.W // 2} {self.H // 2} {self.W // 2 + dx} {self.H // 2} right')
                        self.drags += 1
            except OSError as e:  # BUG-0407: a console call that timed out on a loaded machine ended the watching
                print(f'  watch: {e!r}', flush=True)
            self.stop.wait(0.6)


def beside(walker, observer, d=4.0):
    """the walker next to the observer before its walk (BUG-0394: the first walk ended 15-25 m apart and the
    second walker's clicks on the partner's tag did not bring it back: it never walked)"""
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', observer.call('coop.avatar')['out'])
    dist = partner_dist(walker)
    if not m or dist is None or dist <= TETHER * 0.75:
        return
    p = [float(x) for x in m.groups()]
    r = math.sqrt(sum(x * x for x in p))
    up = [x / r for x in p]
    a = [0.0, -up[2], up[1]] if abs(up[0]) < 0.9 else [-up[2], 0.0, up[0]]
    al = math.sqrt(sum(x * x for x in a))
    q = [p[i] + a[i] / al * d + up[i] * 1.0 for i in range(3)]
    print(f'  walker {dist:.0f} m away: ' + walker.call('coop.npc gopos %.1f %.1f %.1f' % tuple(q))['out'].strip(), flush=True)
    time.sleep(3.0)


AIR_EPISODES, AIR_OK = 5, 0.8  # (0.7.4: 21-22 of 24 hops on the ground)


def direction(walker, observer, wname, oname, rng, secs, ck, stand=0, jumps=0.0, ground=False, air=False):
    beside(walker, observer)
    watch = Watch(observer)
    watch.start()
    time.sleep(2.0)
    for c in (walker, observer):
        c.call('coop.track start')
    time.sleep(0.5)
    log = []
    if MOUND:  # BUG-0435: the walker first stands on the lower part of the nest mound, then walks from there
        x, y, z = MOUND
        r0 = math.sqrt(x * x + y * y + z * z)
        k = (r0 + MOUND_LIFT) / r0
        print('  walker onto the mound: ' + walker.call('coop.npc gopos %.2f %.2f %.2f' % (x * k, y * k, z * k))['out'].strip(),
              flush=True)
        time.sleep(MOUND_STAND)
        log.append(f'stood on the mound {MOUND_STAND:.0f}s')
    log += walk(walker, rng, secs, jumps)
    # BUG-0407: a walk that measured too little (the walker's game at 8-18 fps did not take its keys for 6 s and
    # walked 8 of 27 s: 99 frames, 11 windows) is walked on, up to WALK_MORE times, instead of failing a check
    # that measured nothing
    for _ in range(WALK_MORE):
        with watch.lock:
            obs = dump(observer)
        r = analyse(obs, dump(walker), '')
        fps_k = min(1.0, (r['fps'] or 60) / 60)
        if (r['frames'] > 200 * fps_k and r['win'] > 50 * fps_k) or r['hitch_share'] > LOADED:
            break
        print(f'  measured too little ({r["frames"]} frames while it walks, {r["win"]} windows, '
              f'{r["win_hitch"]} left out for hitches): it walks on', flush=True)
        log.append('| on')
        log += walk(walker, rng, secs, jumps)
    if stand:  # then it stands (BUG-0366: "constantly jumping" while the partner stood)
        time.sleep(stand)
        log.append(f'stand {stand:.0f}s')
    watch.stop.set()
    watch.join()
    time.sleep(1.0)
    shot(observer, f'puppet_walk_{oname}_sees_{wname}')
    for c in (walker, observer):
        c.call('coop.track stop')
    obs, wal = dump(observer), dump(walker)
    path = os.path.join(lane.out_dir('track'), f'puppet_walk_{oname}_sees_{wname}_{time.strftime("%Y%m%d-%H%M%S")}.json')
    with open(path, 'w') as f:
        json.dump({'observer': obs, 'walker': wal}, f)
    print(f'  per-frame samples: {path}', flush=True)
    r = analyse(obs, wal, f'{oname} watches {wname}', air_exempt=air)
    print(f'  {wname} walked: {"; ".join(log)}', flush=True)
    print(f'  {r["label"]}: {r["frames"]} frames while it walks of {r["samples"]} puppet samples at '
          f'{r["fps"]:.0f} fps; walker top speed {r["top"]:.2f} m/s', flush=True)
    print(f'    drawn speed p50 {r["speed_p50"]:.2f} p99 {r["speed_p99"]:.2f} max {r["speed_max"]:.1f} m/s; '
          f'moved in {r["moving"]:.0%} of the frames; game speed > 0.5 in {r["anim"]:.0%}; '
          f'off the target p50 {r["err_p50"]:.2f} p90 {r["err_p90"]:.2f} max {r["err_max"]:.2f} m; '
          f'driver teleports {r["drive_tp"]}', flush=True)
    print(f'    0.1 s windows while it walks: {r["win"]}, the puppet stands (< 0.3 of its path\'s speed) in '
          f'{r["stand"]:.0%}, moves at 0.6-1.4x its speed in {r["matched"]:.0%} (drawn one frame after the path); left '
          f'out: {r["win_hitch"]} with an observer frame over {HITCH * 1000:.0f} ms ({r["hitch_share"]:.1%} of its frames), '
          f'{r["win_tj"]} with a jump of the target itself; unshifted, hitches in: {r["legacy_matched"]:.0%} of '
          f'{r["legacy_win"]}', flush=True)
    print(f'    jumps (t, m, frame ms): {len(r["teleports"])} {r["teleports"][:12]}', flush=True)
    st = r['stops']
    sl = sorted(x[3] for x in st)
    print(f'    stops: {len(st)}, drawn past the stop by more than {STOP_SLIDE} m in {sum(1 for x in st if x[1] > STOP_SLIDE)} '
          f'(max {max((x[1] for x in st), default=0):.2f} m), the fall animation at {sum(1 for x in st if x[2])}; '
          f'slid on after the stop (drawn way less the path\'s) p50 {pct(sl, 0.5):.2f} p90 {pct(sl, 0.9):.2f} max '
          f'{max(sl, default=0):.2f} m, over {STOP_MOVE} m in {sum(1 for x in sl if x > STOP_MOVE)}', flush=True)
    if STOP_WALK:  # BUG-0408
        ck(len(st) >= 8 and sum(1 for x in sl if x > STOP_MOVE) <= STOP_MOVES * len(st),
           f'{r["label"]}: the puppet stops where the walker stopped (slid on over {STOP_MOVE} m after '
           f'{sum(1 for x in sl if x > STOP_MOVE)} of {len(st)} stops <= {STOP_MOVES:.0%})')
    print(f'    walk gait (animation gait state > 0) in {r["gait"]:.0%} of the frames, in {r["near_gait"]:.0%} of the '
          f'{r["near"]} on the observer\'s screen; the walker\'s own avatar while it moves: '
          f'{r["own_gait"]:.0%}', flush=True)
    def by_size(rows):  # BUG-0436: the walk gait by the creature's size on its game's screen (pixels per metre)
        out = []
        for lo, hi in ((0, 10), (10, 25), (25, 60), (60, 1e9)):
            m = [x for x in rows if x.get('ppm') is not None and x['vis'] and lo <= x['ppm'] < hi and x['gv'] > 0.5]
            out.append(f'{lo}-{hi if hi < 1e9 else "inf"} px/m: {sum(1 for x in m if x["gait"] > 0)}/{len(m)}')
        return ', '.join(out)
    print(f'    walk gait by size on the screen: the puppet {by_size([x for x in obs if x["who"] != 255 and x["rv"] > 0.5])}; '
          f'the walker\'s own {by_size([x for x in wal if x["who"] == 255])}', flush=True)
    print(f'    walk animation (IsIdleWalk) in {r["walk_anim"]:.0%} of the frames; the walker\'s own avatar while it moves: '
          f'{r["own_walk"]:.0%}', flush=True)
    print(f'    puppet animations while it walks {r["anims_moving"]}; the walker\'s own {r["own_anims"]}', flush=True)
    # (BUG-0394: in seconds of the observer's frames, 200 / 150 frames were at 60 fps; at 30 fps half as many)
    fps_k = min(1.0, (r['fps'] or 60) / 60)
    ck(r['frames'] > 200 * fps_k, f'{r["label"]}: the walker walked ({r["frames"]} frames of its states moving)')
    ck(not r['teleports'], f'{r["label"]}: no jump of the puppet while it walks ({len(r["teleports"])})')
    if r['hitch_share'] > LOADED:
        print(f'  NOT MEASURED {r["label"]}: {r["hitch_share"]:.0%} of the observer\'s frames over {HITCH * 1000:.0f} ms '
              f'(> {LOADED:.0%}, {r["fps"]:.0f} fps): its game hitched too often to judge steps; the window check is '
              f'skipped', flush=True)
    else:
        ck(r['win'] > 50 * fps_k and r['stand'] <= STAND_MAX and r['matched'] >= MATCHED_MIN,
           f'{r["label"]}: the puppet moves with the walker, not in steps (stands in {r["stand"]:.0%} <= '
           f'{STAND_MAX:.0%}, at its speed in {r["matched"]:.0%} >= {MATCHED_MIN:.0%} of {r["win"]} windows)')
    print(f'    falls (the game\'s fall animation): the puppet {r["falls"]}, the walker\'s own creature {r["own_falls"]}; '
          f'lowest drawn height over the observer\'s ground {r["hd_min"]:.2f} m, frames under it ({UNDER} m) '
          f'{len(r["under"])} {r["under"][:8]}', flush=True)
    if ground:  # BUG-0366 / BUG-0367
        ck(r['falls'] <= r['own_falls'] + 1, f'{r["label"]}: the puppet does not start falling ("jumping") more '
           f'than the walker\'s own creature ({r["falls"]} <= {r["own_falls"]} + 1)')
        ck(not r['under'], f'{r["label"]}: the puppet is never under the observer\'s ground ({len(r["under"])} frames)')
    # BUG-0435: on raised ground (the target, a grounded walker, over 0.15 m above GetHeightAt) in the frames the puppet is
    # on the observer's screen: drawn more than IN_MOUND under the walker's own height (in the mound)
    raised = [(a, b) for a, b in zip(pup_rows(obs), pup_rows(obs)[1:]) if a.get('ht') is not None and a['ht'] > 0.15
              and not (a['flags'] & 8) and b['vis'] and 0 < b['sx'] < 1440 and 0 < b['sy'] < 1080]
    low = [round(b['hd'] - a['ht'], 2) for a, b in raised if b['hd'] < a['ht'] - IN_MOUND]
    print(f'    on raised ground (target over 0.15 m, on the screen): {len(raised)} frames, drawn more than {IN_MOUND} m '
          f'under the walker\'s height in {len(low)} {low[:8]}', flush=True)
    if MOUND:
        ck(len(raised) >= 100 and len(low) <= IN_MOUND_MAX * len(raised),
           f'{r["label"]}: the puppet stands on the mound, not in it (drawn {IN_MOUND} m under the walker in {len(low)} of '
           f'{len(raised)} frames <= {IN_MOUND_MAX:.0%})')
    ar = r['airs']
    shown = sum(1 for x in ar if x[3])
    print(f'    jumps of the walker (its target over {AIR_MIN} m, peak >= {AIR_PEAK} m): {len(ar)}, drawn up to at least '
          f'{AIR_SHOWN:.0%} of the peak in {shown}; (t, peak, drawn, shown) {ar[:10]}', flush=True)
    an, aok = r['air_frames']
    print(f'    frames with the walker\'s target over {AIR_MIN} m: {an}, the puppet drawn at least {AIR_SHOWN:.0%} as high '
          f'in the next frame in {aok} ({aok / an if an else 0:.0%})', flush=True)
    if air:  # BUG-0376
        print('    observer ' + observer.call('coop.track air')['out'].strip().replace('\n', '; '), flush=True)
        ck(len(ar) >= AIR_EPISODES and an and aok >= AIR_OK * an,
           f'{r["label"]}: the walker\'s jumps leave the ground on the observer\'s screen too (drawn up in {aok} of {an} '
           f'frames >= {AIR_OK:.0%}, {len(ar)} jumps >= {AIR_EPISODES})')
    far = [x for x in pup_rows(obs) if x['rv'] > 0.5 and x['vis'] and x.get('ppm') is not None and 0 < x['ppm'] < GAIT_PPM]
    if len(far) > r['near']:  # BUG-0436
        print(f'  NOT MEASURED {r["label"]}: the walk gait: the observer\'s camera is far away ({len(far)} frames under '
              f'{GAIT_PPM:.0f} px/m, {r["near"]} near): its game animates a creature that far without a gait', flush=True)
    else:
        ck(r['near'] >= 150 * fps_k and r['near_gait'] >= GAIT_MIN,
           f'{r["label"]}: the puppet walks where the observer sees it (walk gait in {r["near_gait"]:.0%} >= {GAIT_MIN:.0%} '
           f'of {r["near"]} frames on its screen)')


def settle(cons, secs=40, need=20, runs=3):
    """BUG-0394: right after the join a guest loads for seconds (18 fps on average, one 5.6 s frame) and its
    game did not update the partner's puppet for 7 s (it moved only where it was placed, no walk gait):
    measure from when every game has run `runs` 1 s windows of at least `need` frames in a row"""
    import re as _re
    def frame(c):
        m = _re.search(r'frame=(\d+)', c.call('frame')['out'])
        return int(m.group(1)) if m else None
    ok = {id(c): 0 for c in cons}
    last = {id(c): (frame(c), time.time()) for c in cons}
    t0 = time.time()
    while time.time() - t0 < secs and min(ok.values()) < runs:
        time.sleep(1.0)
        for c in cons:
            f, t = frame(c), time.time()
            f0, t1 = last[id(c)]
            if f is not None and f0 is not None:
                ok[id(c)] = ok[id(c)] + 1 if (f - f0) / (t - t1) >= need else 0
            last[id(c)] = (f, t)
    print(f'  settled after {time.time() - t0:.0f} s', flush=True)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default='both', choices=('host', 'guest', 'both'), help='who walks')
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--secs', type=float, default=25)
    ap.add_argument('--jumps', type=float, default=0, help='the walker jumps (Space) in this share of its walks and on the spot')
    ap.add_argument('--air', action='store_true', help='also check that the walker\'s jumps are shown (BUG-0376)')
    ap.add_argument('--ground', action='store_true', help='also check falls and the height over the ground (BUG-0366/0367)')
    ap.add_argument('--goto', default='', help='x,y,z: both players go there first (hilly ground, BUG-0366)')
    ap.add_argument('--stand', type=float, default=0, help='the walker stands this long after its walk (s)')
    ap.add_argument('--guest-fps', type=float, default=0, help='the guest at ~N fps (frame.limit 1000/N): a weak PC (BUG-0366)')
    ap.add_argument('--zoom', type=int, default=0, help='both cameras zoomed out by N wheel turns of 5 first (BUG-0436)')
    ap.add_argument('--stop-walk', action='store_true', help='short walks, each followed by a stop, and check the stops (BUG-0408)')
    ap.add_argument('--mound', default='', help='x,y,z: the walker stands there (lifted, it lands on a nest mound) before its walk (BUG-0435)')
    a = ap.parse_args()
    global STOP_WALK, MOUND
    STOP_WALK = a.stop_walk
    MOUND = tuple(map(float, a.mound.split(','))) if a.mound else None
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  seed {seed}', flush=True)
    rng = random.Random(seed)
    host, guest = start('creature')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 30:
        st = host.call('coop.status')['out']
        if re.search(r'puppet pos=', st) and re.search(r'puppet pos=', guest.call('coop.status')['out']):
            break
        time.sleep(1)
    settle((host, guest))
    time.sleep(1)
    if a.goto:  # both onto that ground, the guest 3 m beside the host
        x, y, z = map(float, a.goto.split(','))
        print('  host: ' + host.call(f'coop.abilities goto {x} {y} {z}')['out'].strip(), flush=True)
        print('  guest: ' + guest.call(f'coop.abilities goto {x + 3} {y} {z}')['out'].strip(), flush=True)
        time.sleep(4)
    if a.guest_fps:
        print('  guest: ' + guest.call(f'frame.limit {round(1000 / a.guest_fps)}')['out'].strip(), flush=True)
    if os.environ.get('SPORE_NET'):
        print('  network: ' + os.environ['SPORE_NET'], flush=True)
    if a.zoom:  # BUG-0436: both cameras far out (mouse wheel), as a player zoomed out
        for c in (host, guest):
            W, H = screen(c)
            for _ in range(a.zoom):
                c.call(f'wheel {W // 2} {H // 2} -5')
                time.sleep(0.15)
        time.sleep(1.5)
    for cmd in filter(None, os.environ.get('PUPPET_WALK_CMDS', '').split(';')):
        for c in (host, guest):
            print('  ' + cmd + ': ' + c.call(cmd)['out'].strip(), flush=True)
    if a.dir in ('host', 'both'):
        direction(host, guest, 'host', 'guest', rng, a.secs, ck, a.stand, a.jumps, a.ground, a.air)
    if a.dir in ('guest', 'both'):
        direction(guest, host, 'guest', 'host', rng, a.secs, ck, a.stand, a.jumps, a.ground, a.air)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
