#!/usr/bin/env python3
"""What a player sees of the partner's creature while the partner walks by real input (BUG-0360).

Player report (0.7.3.2, Creature, 2 players, 60 fps, rtt 20-30 ms): "the partner's character
teleports: sometimes it moves smoothly, mostly it jumps, about every half second". The other tests
compare positions at a few moments (coop.sync); this one samples the puppet in the observer's game
every frame (`coop.track`: the game's position, where the model is drawn, the game's own speed of
the creature and its current animation) while the other player walks with WASD keys and clicks on
the ground (turns, stops, ~25 s), in both directions (the guest watches the host, the host watches
the guest).

Checks per direction, over the frames in which the walker's latest state says it moves:
* no teleport: no frame step of the drawn puppet longer than TP_FACTOR x the walker's top speed x
  the frame time (and at least TP_MIN m);
* it moves: in at least MOVING_MIN of these frames the drawn puppet moved at least a fifth of the
  walker's speed x the frame time;
* it walks: in at least ANIM_MIN of them the puppet's game speed (what the game animates its walk
  from) is above 0.5 m/s.

  coop_puppet_walk.py [--dir host|guest|both] [--seed N]
  (SPORE_FIX_OFF=creature.puppet_kinematic must FAIL)"""
import argparse
import math
import random
import re
import time

from twoinst import Checks, shot, start

TP_FACTOR, TP_MIN = 3.0, 0.75
MOVING_MIN = 0.85
ANIM_MIN = 0.80


def screen(con):
    m = re.search(r'viewport x=\d+ y=\d+ w=(\d+) h=(\d+)', con.call('coop.cam')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else (1440, 1080)


def dump(con):
    rows = []
    n = int(re.search(r'samples=(\d+)', con.call('coop.track count')['out']).group(1))
    for start_i in range(0, n, 1500):
        for line in con.call(f'coop.track dump {start_i} 1500')['out'].splitlines():
            f = line.split()
            if len(f) != 16:
                continue
            rows.append({'t': float(f[0]), 'who': int(f[1]), 'flags': int(f[2]), 'tp': int(f[3]),
                         'p': tuple(map(float, f[4:7])), 'd': tuple(map(float, f[7:10])),
                         'tgt': tuple(map(float, f[10:13])), 'gv': float(f[13]), 'rv': float(f[14]),
                         'anim': f[15]})
    return rows


def walk(con, rng, secs):
    """a player walking: WASD holds with turns, stops, clicks on the ground ahead"""
    W, H = screen(con)
    t_end = time.time() + secs
    log = []
    while time.time() < t_end:
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
    frames, moving, anim_ok, tps, steps, dts = 0, 0, 0, [], [], []
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
        anims_moving[b['anim']] = anims_moving.get(b['anim'], 0) + 1
    errs = [math.dist(r['d'], r['tgt']) for r in pup if r['rv'] > 0.5]
    own_anims = {}
    for r in own:
        if r['gv'] > 0.5:
            own_anims[r['anim']] = own_anims.get(r['anim'], 0) + 1
    res = {
        'label': label, 'frames': frames, 'samples': len(pup), 'top': top,
        'fps': 1 / (sum(dts) / len(dts)) if dts else 0,
        'teleports': tps, 'moving': moving / frames if frames else 0, 'anim': anim_ok / frames if frames else 0,
        'speed_p50': pct(steps, 0.5), 'speed_p99': pct(steps, 0.99), 'speed_max': max(steps) if steps else 0,
        'err_p50': pct(errs, 0.5), 'err_p90': pct(errs, 0.9), 'err_max': max(errs) if errs else 0,
        'drive_tp': (pup[-1]['tp'] - pup[0]['tp']) if pup else 0,
        'anims_moving': sorted(anims_moving.items(), key=lambda kv: -kv[1])[:5],
        'own_anims': sorted(own_anims.items(), key=lambda kv: -kv[1])[:5],
    }
    return res


def direction(walker, observer, wname, oname, rng, secs, ck):
    for c in (walker, observer):
        c.call('coop.track start')
    time.sleep(0.5)
    log = walk(walker, rng, secs)
    time.sleep(1.0)
    shot(observer, f'puppet_walk_{oname}_sees_{wname}')
    for c in (walker, observer):
        c.call('coop.track stop')
    r = analyse(dump(observer), dump(walker), f'{oname} watches {wname}')
    print(f'  {wname} walked: {"; ".join(log)}', flush=True)
    print(f'  {r["label"]}: {r["frames"]} frames while it walks of {r["samples"]} puppet samples at '
          f'{r["fps"]:.0f} fps; walker top speed {r["top"]:.2f} m/s', flush=True)
    print(f'    drawn speed p50 {r["speed_p50"]:.2f} p99 {r["speed_p99"]:.2f} max {r["speed_max"]:.1f} m/s; '
          f'moved in {r["moving"]:.0%} of the frames; game speed > 0.5 in {r["anim"]:.0%}; '
          f'off the target p50 {r["err_p50"]:.2f} p90 {r["err_p90"]:.2f} max {r["err_max"]:.2f} m; '
          f'driver teleports {r["drive_tp"]}', flush=True)
    print(f'    jumps (t, m, frame ms): {len(r["teleports"])} {r["teleports"][:12]}', flush=True)
    print(f'    puppet animations while it walks {r["anims_moving"]}; the walker\'s own {r["own_anims"]}', flush=True)
    ck(r['frames'] > 200, f'{r["label"]}: the walker walked ({r["frames"]} frames of its states moving)')
    ck(not r['teleports'], f'{r["label"]}: no jump of the puppet while it walks ({len(r["teleports"])})')
    ck(r['moving'] >= MOVING_MIN, f'{r["label"]}: the puppet moves in most frames ({r["moving"]:.0%} >= {MOVING_MIN:.0%})')
    ck(r['anim'] >= ANIM_MIN, f'{r["label"]}: the puppet walks (game speed) ({r["anim"]:.0%} >= {ANIM_MIN:.0%})')


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
    if a.dir in ('host', 'both'):
        direction(host, guest, 'host', 'guest', rng, a.secs, ck)
    if a.dir in ('guest', 'both'):
        direction(guest, host, 'guest', 'host', rng, a.secs, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
