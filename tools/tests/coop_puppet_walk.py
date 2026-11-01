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


def screen(con):
    m = re.search(r'viewport x=\d+ y=\d+ w=(\d+) h=(\d+)', con.call('coop.cam')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else (1440, 1080)


def dump(con):
    rows = []
    n = int(re.search(r'samples=(\d+)', con.call('coop.track count')['out']).group(1))
    for start_i in range(0, n, 1500):
        for line in con.call(f'coop.track dump {start_i} 1500')['out'].splitlines():
            f = line.split()
            if len(f) not in (23, 25):
                continue
            rows.append({'hd': float(f[23]) if len(f) > 23 else None, 'ht': float(f[24]) if len(f) > 23 else None,
                         't': float(f[0]), 'who': int(f[1]), 'flags': int(f[2]), 'tp': int(f[3]),
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


HOP = 1.5  # m


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
    # 0.1 s windows while the walker's path moves (> 1 m/s): the drawn puppet's speed / the path's speed
    # (the path: the interpolated target; the reported speed is no measure, an avatar turning on the
    # spot reports one)
    win, i = [], 0
    while i < len(pup):
        j = i
        while j < len(pup) and pup[j]['t'] - pup[i]['t'] < 0.1:
            j += 1
        if j >= len(pup):
            break
        dt = pup[j]['t'] - pup[i]['t']
        path = math.dist(pup[i]['tgt'], pup[j]['tgt']) / dt
        if path > 1.0 and dt < 0.3 and (pup[j]['flags'] & 1):
            win.append(math.dist(pup[i]['d'], pup[j]['d']) / dt / path)
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
        # BUG-0366 / 0367: the game's fall (+0x137 = 0 with the fall animation) and the height over the
        # observer's ground
        'falls': falls(pup), 'own_falls': falls(own),
        'under': [round(r['hd'], 2) for r in pup if r.get('hd') is not None and r['hd'] < UNDER],
        'hd_min': min((r['hd'] for r in pup if r.get('hd') is not None), default=0.0),
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


def direction(walker, observer, wname, oname, rng, secs, ck, stand=0, jumps=0.0, ground=False):
    watch = Watch(observer)
    watch.start()
    time.sleep(2.0)
    for c in (walker, observer):
        c.call('coop.track start')
    time.sleep(0.5)
    log = walk(walker, rng, secs, jumps)
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
    print(f'    0.1 s windows while it walks: {r["win"]}, the puppet stands (< 0.3 of its path\'s speed) in '
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
    print(f'    falls (the game\'s fall animation): the puppet {r["falls"]}, the walker\'s own creature {r["own_falls"]}; '
          f'lowest drawn height over the observer\'s ground {r["hd_min"]:.2f} m, frames under it ({UNDER} m) '
          f'{len(r["under"])} {r["under"][:8]}', flush=True)
    if ground:  # BUG-0366 / BUG-0367
        ck(r['falls'] <= r['own_falls'] + 1, f'{r["label"]}: the puppet does not start falling ("jumping") more '
           f'than the walker\'s own creature ({r["falls"]} <= {r["own_falls"]} + 1)')
        ck(not r['under'], f'{r["label"]}: the puppet is never under the observer\'s ground ({len(r["under"])} frames)')
    ck(r['near'] >= 150 and r['near_gait'] >= GAIT_MIN,
       f'{r["label"]}: the puppet walks where the observer sees it (walk gait in {r["near_gait"]:.0%} >= {GAIT_MIN:.0%} '
       f'of {r["near"]} frames on its screen)')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default='both', choices=('host', 'guest', 'both'), help='who walks')
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--secs', type=float, default=25)
    ap.add_argument('--jumps', type=float, default=0, help='the walker jumps (Space) in this share of its walks and on the spot')
    ap.add_argument('--ground', action='store_true', help='also check falls and the height over the ground (BUG-0366/0367)')
    ap.add_argument('--goto', default='', help='x,y,z: both players go there first (hilly ground, BUG-0366)')
    ap.add_argument('--stand', type=float, default=0, help='the walker stands this long after its walk (s)')
    ap.add_argument('--guest-fps', type=float, default=0, help='the guest at ~N fps (frame.limit 1000/N): a weak PC (BUG-0366)')
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
    if a.goto:  # both onto that ground, the guest 3 m beside the host
        x, y, z = map(float, a.goto.split(','))
        print('  host: ' + host.call(f'coop.abilities goto {x} {y} {z}')['out'].strip(), flush=True)
        print('  guest: ' + guest.call(f'coop.abilities goto {x + 3} {y} {z}')['out'].strip(), flush=True)
        time.sleep(4)
    if a.guest_fps:
        print('  guest: ' + guest.call(f'frame.limit {round(1000 / a.guest_fps)}')['out'].strip(), flush=True)
    if os.environ.get('SPORE_NET'):
        print('  network: ' + os.environ['SPORE_NET'], flush=True)
    for cmd in filter(None, os.environ.get('PUPPET_WALK_CMDS', '').split(';')):
        for c in (host, guest):
            print('  ' + cmd + ': ' + c.call(cmd)['out'].strip(), flush=True)
    if a.dir in ('host', 'both'):
        direction(host, guest, 'host', 'guest', rng, a.secs, ck, a.stand, a.jumps, a.ground)
    if a.dir in ('guest', 'both'):
        direction(guest, host, 'guest', 'host', rng, a.secs, ck, a.stand, a.jumps, a.ground)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
