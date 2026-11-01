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
  frame time (and at least TP_MIN m);
* no steps: in 0.1 s windows the puppet moves at 0.6-1.4x the walker's speed in at least
  MATCHED_MIN of them and stands (< 0.3x) in at most STAND_MAX (the old driving: 18-23 % standing,
  57-63 % matched: the "updates about every half second, not steadily");
* it walks: the animation's walk gait (Anim::AnimatedCreature +0x17C -> +0x15B8, what the game's
  idle chooser 0xC0C790 reads; > 0 on a walking avatar in 94-97 % of its moving frames) is on in at
  least GAIT_MIN of the frames.

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


def screen(con):
    m = re.search(r'viewport x=\d+ y=\d+ w=(\d+) h=(\d+)', con.call('coop.cam')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else (1440, 1080)


def dump(con):
    rows = []
    n = int(re.search(r'samples=(\d+)', con.call('coop.track count')['out']).group(1))
    for start_i in range(0, n, 1500):
        for line in con.call(f'coop.track dump {start_i} 1500')['out'].splitlines():
            f = line.split()
            if len(f) != 23:
                continue
            rows.append({'t': float(f[0]), 'who': int(f[1]), 'flags': int(f[2]), 'tp': int(f[3]),
                         'p': tuple(map(float, f[4:7])), 'd': tuple(map(float, f[7:10])),
                         'tgt': tuple(map(float, f[10:13])), 'gv': float(f[13]), 'rv': float(f[14]),
                         'anim': f[15], 'gait': int(f[16]), 'gait2': int(f[17]), 'moving': int(f[18]),
                         'ss': int(f[19]), 'vis': int(f[20]), 'sx': float(f[21]), 'sy': float(f[22])})
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
            con.call(f'click {xy[0]} {min(H - 180, xy[1] + 60)}')
            time.sleep(2.0)
            return True
        con.call(f'drag {W // 2} {H // 2} {W // 2 + 380} {H // 2} right')
        time.sleep(0.4)
    return False


TETHER = 12.0  # m: the walker turns back to the observer beyond this


def walk(con, rng, secs):
    """a player walking: WASD holds with turns, stops, clicks on the ground ahead, staying near the
    partner"""
    W, H = screen(con)
    t_end = time.time() + secs
    log = []
    while time.time() < t_end:
        d = partner_dist(con)
        if d is not None and d > TETHER:
            log.append(f'back {d:.0f}m' + ('' if back_to_partner(con, W, H) else ' (not found)'))
            continue
        r = rng.random()
        if r < 0.45:
            keys = rng.choice([['W'], ['W'], ['W', 'A'], ['W', 'D'], ['A'], ['D'], ['S']])
            hold = rng.uniform(1.0, 3.0)
            for k in keys:
                con.call(f'key 0x{ord(k):02X} down')
            time.sleep(hold)
            for k in keys:
                con.call(f'key 0x{ord(k):02X} up')
            log.append(f'{"+".join(keys)} {hold:.1f}s')
        elif r < 0.85:
            x, y = rng.randint(int(W * 0.2), int(W * 0.8)), rng.randint(int(H * 0.45), int(H * 0.75))
            con.call(f'click {x} {y}')
            wait = rng.uniform(1.5, 3.0)
            time.sleep(wait)
            log.append(f'click {x},{y} {wait:.1f}s')
        else:
            stop = rng.uniform(0.5, 1.5)
            time.sleep(stop)
            log.append(f'stop {stop:.1f}s')
    return log


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else 0.0


def analyse(obs_rows, walker_rows, label):
    """per-frame motion of the walker's puppet in the observer's game"""
    own = [r for r in walker_rows if r['who'] == 255]
    top = pct([r['gv'] for r in own if r['gv'] > 0.3], 0.95) or 5.0  # the walker's own top speed
    pup = [r for r in obs_rows if r['who'] != 255]
    frames, moving, anim_ok, walk_anim, gait, tps, steps, dts = 0, 0, 0, 0, 0, [], [], []
    anims_moving = {}
    for a, b in zip(pup, pup[1:]):
        dt = b['t'] - a['t']
        if dt <= 0 or dt > 0.25:
            continue
        dts.append(dt)
        if b['rv'] < 0.5 or not (b['flags'] & 1):  # the walker's state: it walks (PF_IN_WORLD)
            continue
        frames += 1
        step = math.dist(a['d'], b['d'])
        steps.append(step / dt)
        if step > max(TP_MIN, TP_FACTOR * top * dt):
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
            if r['vis'] and 0 < r['sx'] < 1440 and 0 < r['sy'] < 1080:
                near += 1
                near_gait += r['gait'] > 0
    # 0.1 s windows while the walker walks (> 1 m/s): the puppet's drawn speed / the walker's speed
    win, i = [], 0
    while i < len(pup):
        j = i
        while j < len(pup) and pup[j]['t'] - pup[i]['t'] < 0.1:
            j += 1
        if j >= len(pup):
            break
        rv = sum(r['rv'] for r in pup[i:j + 1]) / (j - i + 1)
        if rv > 1.0 and pup[j]['t'] - pup[i]['t'] < 0.3:
            win.append(math.dist(pup[i]['d'], pup[j]['d']) / (pup[j]['t'] - pup[i]['t']) / rv)
        i = j
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
        'drive_tp': (pup[-1]['tp'] - pup[0]['tp']) if pup else 0,
        'anims_moving': sorted(anims_moving.items(), key=lambda kv: -kv[1])[:5],
        'own_anims': sorted(own_anims.items(), key=lambda kv: -kv[1])[:5],
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

    def run(self):
        while not self.stop.is_set():
            xy = partner_screen(self.con)
            if not xy or not (60 < xy[0] < self.W - 60 and 60 < xy[1] < self.H - 170):
                dx = 300 if not xy or xy[0] > self.W // 2 else -300
                self.con.call(f'drag {self.W // 2} {self.H // 2} {self.W // 2 + dx} {self.H // 2} right')
                self.drags += 1
            self.stop.wait(0.6)


def direction(walker, observer, wname, oname, rng, secs, ck):
    watch = Watch(observer)
    watch.start()
    time.sleep(2.0)
    for c in (walker, observer):
        c.call('coop.track start')
    time.sleep(0.5)
    log = walk(walker, rng, secs)
    watch.stop.set()
    watch.join()
    time.sleep(1.0)
    shot(observer, f'puppet_walk_{oname}_sees_{wname}')
    for c in (walker, observer):
        c.call('coop.track stop')
    obs, wal = dump(observer), dump(walker)
    path = os.path.join(lane.out_dir('track'), f'puppet_walk_{oname}_sees_{wname}.json')
    with open(path, 'w') as f:
        json.dump({'observer': obs, 'walker': wal}, f)
    print(f'  per-frame samples: {path}', flush=True)
    r = analyse(obs, wal, f'{oname} watches {wname}')
    print(f'  {wname} walked: {"; ".join(log)}', flush=True)
    print(f'  {r["label"]}: {r["frames"]} frames while it walks of {r["samples"]} puppet samples at '
          f'{r["fps"]:.0f} fps; walker top speed {r["top"]:.2f} m/s', flush=True)
    print(f'    drawn speed p50 {r["speed_p50"]:.2f} p99 {r["speed_p99"]:.2f} max {r["speed_max"]:.1f} m/s; '
          f'moved in {r["moving"]:.0%} of the frames; game speed > 0.5 in {r["anim"]:.0%}; '
          f'off the target p50 {r["err_p50"]:.2f} p90 {r["err_p90"]:.2f} max {r["err_max"]:.2f} m; '
          f'driver teleports {r["drive_tp"]}', flush=True)
    print(f'    0.1 s windows while it walks: {r["win"]}, the puppet stands (< 0.3 of the walker\'s speed) in '
          f'{r["stand"]:.0%}, moves at 0.6-1.4x its speed in {r["matched"]:.0%}', flush=True)
    print(f'    jumps (t, m, frame ms): {len(r["teleports"])} {r["teleports"][:12]}', flush=True)
    print(f'    walk gait (animation gait state > 0) in {r["gait"]:.0%} of the frames, in {r["near_gait"]:.0%} of the '
          f'{r["near"]} on the observer\'s screen; the walker\'s own avatar while it moves: '
          f'{r["own_gait"]:.0%}', flush=True)
    print(f'    walk animation (IsIdleWalk) in {r["walk_anim"]:.0%} of the frames; the walker\'s own avatar while it moves: '
          f'{r["own_walk"]:.0%}', flush=True)
    print(f'    puppet animations while it walks {r["anims_moving"]}; the walker\'s own {r["own_anims"]}', flush=True)
    ck(r['frames'] > 200, f'{r["label"]}: the walker walked ({r["frames"]} frames of its states moving)')
    ck(not r['teleports'], f'{r["label"]}: no jump of the puppet while it walks ({len(r["teleports"])})')
    ck(r['win'] > 50 and r['stand'] <= STAND_MAX and r['matched'] >= MATCHED_MIN,
       f'{r["label"]}: the puppet moves with the walker, not in steps (stands in {r["stand"]:.0%} <= {STAND_MAX:.0%}, '
       f'at its speed in {r["matched"]:.0%} >= {MATCHED_MIN:.0%} of {r["win"]} windows)')
    ck(r['near'] >= 150 and r['near_gait'] >= GAIT_MIN,
       f'{r["label"]}: the puppet walks where the observer sees it (walk gait in {r["near_gait"]:.0%} >= {GAIT_MIN:.0%} '
       f'of {r["near"]} frames on its screen)')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default='both', choices=('host', 'guest', 'both'), help='who walks')
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--secs', type=float, default=25)
    a = ap.parse_args()
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
    time.sleep(3)
    for cmd in filter(None, os.environ.get('PUPPET_WALK_CMDS', '').split(';')):
        for c in (host, guest):
            print('  ' + cmd + ': ' + c.call(cmd)['out'].strip(), flush=True)
    if a.dir in ('host', 'both'):
        direction(host, guest, 'host', 'guest', rng, a.secs, ck)
    if a.dir in ('guest', 'both'):
        direction(guest, host, 'guest', 'host', rng, a.secs, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
