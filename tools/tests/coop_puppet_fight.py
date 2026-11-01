#!/usr/bin/env python3
"""What a player sees of the partner's creature while the partner fights (audit item 15).

puppet_walk covers walking by WASD and clicks. A fight is faster and rougher: the fighter clicks on an
NPC (its creature runs there and bites), presses its ability keys 1-5 and 0 (charge, strike, spit,
sprint ...: lunges and dashes carried by the game's animations), turns with WASD between the attacks
and goes for the next NPC when one dies. The observer's game samples the fighter's puppet every frame
(`coop.track`, the drawn position before the mod's driving), the fighter's game samples its own avatar.
Both directions (the guest watches the host fight, the host the guest).

Checks per direction over the fight (FIGHT_S s):
* jumps: frame steps of the drawn puppet over TP_FACTOR x the fighter's own top speed (p99 of its own
  game's speed, sprints included) x the frame time (or the frame before's): at most 1 per minute;
* teleports of the driver (`coop.track` tp column, the mod putting the puppet in place): at most 1 per
  minute;
* error: the drawn puppet against the fighter's own position in its game at the same moment (both tracks
  start together), over the frames in which the fighter moves faster than 1 m/s: p95 <= ERR_P95 m;
* delay: the constant delay (0-0.8 s) that fits the drawn path best over those frames: <= LAG_MAX s (a fight
  the other player sees half a second late: the bite lands before the creature arrives).

  coop_puppet_fight.py [--dir host|guest|both] [--seed N] [--secs S]
  (SPORE_FIX_OFF=creature.puppet_path,creature.puppet_walk fails)"""
import argparse
import json
import math
import os
import random
import re
import time

import lane
from coop_puppet_walk import Watch, back_to_partner, partner_dist, screen, settle
from twoinst import Checks, shot, start

TP_FACTOR, TP_MIN = 3.0, 0.75
ERR_P95 = 2.5
LAG_MAX = 0.3  # s: the delay that fits the drawn path best (fix on 0.06-0.14 s; puppet_path + puppet_walk off 0.50-0.54 s)
PER_MIN = 1.0
ABILITY_KEYS = [0x31, 0x32, 0x33, 0x34, 0x35, 0x30]  # 1-5, 0


def dump_anims(con):
    """coop_puppet_walk.dump with the columns ranims / ranim (BUG-0414: the partner's animations played on the
    puppet so far and the last one's id; on the own avatar's rows its own animations)"""
    rows = []
    n = int(re.search(r'samples=(\d+)', con.call('coop.track count')['out']).group(1))
    for start_i in range(0, n, 1500):
        for line in con.call(f'coop.track dump {start_i} 1500')['out'].splitlines():
            f = line.split()
            if len(f) < 26:
                continue
            rows.append({'hd': float(f[23]), 'ht': float(f[24]), 't': float(f[0]), 'who': int(f[1]),
                         'flags': int(f[2]), 'tp': int(f[3]), 'p': tuple(map(float, f[4:7])),
                         'd': tuple(map(float, f[7:10])), 'tgt': tuple(map(float, f[10:13])), 'gv': float(f[13]),
                         'rv': float(f[14]), 'anim': f[15], 'gait': int(f[16]), 'gait2': int(f[17]),
                         'moving': int(f[18]), 'ss': int(f[19]), 'vis': int(f[20]), 'sx': float(f[21]),
                         'sy': float(f[22]), 'ranims': int(f[26]) if len(f) > 27 else 0,
                         'ranim': f[27] if len(f) > 27 else ''})
    return rows


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else 0.0


def npc_targets(con):
    """[(dist, id, sx, sy)] of NPCs on the fighter's screen (`coop.targets all`)"""
    out = []
    W, H = screen(con)
    for line in con.call('coop.targets all')['out'].splitlines():
        f = line.split()
        if len(f) == 5 and f[0] == 'obj' and f[1].startswith('n'):
            d, x, y = float(f[2]), int(f[3]), int(f[4])
            if 30 < x < W - 30 and 90 < y < H - 170 and d < 30:
                out.append((d, f[1], x, y))
    return sorted(out)


def stance(con):
    """the game's ability mode (cCreatureGameData::GetAbilityMode): 0 social, 1 combat"""
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call Simulator::cCreatureGameData::GetAbilityMode cdecl')['out'])
    return int(m.group(1), 16) if m else None


def combat_stance(con):
    """the attack stance (the button right of the social one): a click on an NPC then attacks it"""
    for _ in range(3):
        if stance(con) == 1:
            return True
        con.call('click 750 998')
        time.sleep(0.6)
    return stance(con) == 1


def full_hp(con):
    """the fighter's health back to its maximum (not over it: `coop.team hp 1000` gave it 1000 of 10, which the
    other game's puppet, capped at its maximum, cannot show, and the invariant monitor's puppet_hp rule failed the
    run on it)"""
    m = re.search(r'^own: hp=[-\d.]+/([\d.]+)', con.call('coop.hp')['out'], re.M)
    if m:
        con.call(f'coop.team hp {m.group(1)}')


COME_SKIP = 1.5  # s around a teleport of the test (`coop.team come`) left out of the checks


def fight(con, rng, secs, log, t_track, comes):
    """a player fighting: target an NPC (click), abilities, bites, turns, the next NPC. Beyond 14 m from the partner
    it walks back (a click at its tag); when that fails twice the test puts it next to the partner (`coop.team
    come`, a teleport: its time on the track clock goes to `comes` and is left out of the checks)"""
    W, H = screen(con)
    lost = 0
    t_end = time.time() + secs
    full_hp(con)
    log.append(f'combat stance {combat_stance(con)}')
    while time.time() < t_end:
        d = partner_dist(con)
        if d is not None and d > 14:  # walks back to the partner (the observer's game animates it on screen)
            ok = back_to_partner(con, W, H)
            lost = 0 if ok else lost + 1
            log.append(f'back {d:.0f}m' + ('' if ok else ' (not found)'))
            if lost >= 2:
                comes.append(time.time() - t_track)
                con.call('coop.team come')
                log.append('come (teleport, left out)')
                lost = 0
                time.sleep(0.5)
            continue
        tg = npc_targets(con)
        r = rng.random()
        if tg and r < 0.45:
            dist, nid, x, y = tg[0] if rng.random() < 0.7 else rng.choice(tg)
            if stance(con) != 1:
                combat_stance(con)
            con.call(f'click {x} {y}')  # target it: the creature runs there and bites
            time.sleep(rng.uniform(1.0, 2.0))
            keys = rng.sample(ABILITY_KEYS, rng.randint(1, 3))
            for k in keys:
                con.call(f'key 0x{k:02X} down')
                time.sleep(0.15)
                con.call(f'key 0x{k:02X} up')
                time.sleep(rng.uniform(0.5, 1.2))
            log.append(f'{nid} {dist:.0f}m keys {"".join(chr(k) for k in keys)}')
        elif r < 0.9:
            keys = rng.choice([['W'], ['W', 'A'], ['W', 'D'], ['A'], ['D'], ['S']])
            hold = rng.uniform(0.5, 1.8)
            for k in keys:
                con.call(f'key 0x{ord(k):02X} down')
            if rng.random() < 0.5:  # an ability while it runs (sprint, charge)
                time.sleep(hold / 2)
                k = rng.choice(ABILITY_KEYS)
                con.call(f'key 0x{k:02X} down')
                time.sleep(0.15)
                con.call(f'key 0x{k:02X} up')
                time.sleep(hold / 2)
            else:
                time.sleep(hold)
            for k in keys:
                con.call(f'key 0x{ord(k):02X} up')
            log.append(f'{"+".join(keys)} {hold:.1f}s')
        else:  # a turn of the camera, as players do in a fight
            dx = rng.choice((-300, 300))
            con.call(f'drag {W // 2} {H // 2} {W // 2 + dx} {H // 2} right')
            time.sleep(0.4)
            log.append('turn')
        full_hp(con)
    return log


def interp(rows, t):
    """the position of the time-sorted rows at time t (linear)"""
    lo, hi = 0, len(rows) - 1
    if t <= rows[0]['t']:
        return rows[0]['p']
    if t >= rows[-1]['t']:
        return rows[-1]['p']
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if rows[mid]['t'] <= t:
            lo = mid
        else:
            hi = mid
    a, b = rows[lo], rows[hi]
    k = (t - a['t']) / (b['t'] - a['t']) if b['t'] > a['t'] else 0
    return tuple(x + (y - x) * k for x, y in zip(a['p'], b['p']))


def in_view(r, wh):
    """the puppet is on the observer's screen in this row (`coop.track` vis / sx / sy; wh None: every row counts)"""
    return wh is None or (r['vis'] and 0 <= r['sx'] <= wh[0] and 0 <= r['sy'] <= wh[1])


def radial_step(a, b):
    """the change of the drawn puppet's distance from the planet's centre between two rows (m)"""
    return abs(math.sqrt(sum(x * x for x in b['d'])) - math.sqrt(sum(x * x for x in a['d'])))


def analyse(obs_rows, fighter_rows, label, comes=(), wh=None):
    """wh: the observer's screen size. BUG-0414: a step is a jump only when the observer sees it (the puppet on
    its screen in both frames). The game puts a creature outside the view on the plain terrain (GetHeightAt): on the
    raised ground of a nest mound the puppet then sits 0.4-1 m under its place while the observer looks away and is
    back on the mound when it is in view again; those steps (`off_view`) are not seen. The first frame back in view
    is still drawn low (the game lifts it one update later): a radial step from that frame (`entry`) is listed, not
    judged (BUG-0434)"""
    def near_come(t):
        return any(abs(t - c) <= COME_SKIP for c in comes)
    own = [r for r in fighter_rows if r['who'] == 255]
    pup = [r for r in obs_rows if r['who'] != 255 and (r['flags'] & 1)]
    top = max(pct([r['gv'] for r in own if r['gv'] > 0.3], 0.99), 4.0)
    jumps, off_view, entry, prev_dt, dts = [], [], [], 0.0, []
    for i, (a, b) in enumerate(zip(pup, pup[1:])):
        dt = b['t'] - a['t']
        allow_dt, prev_dt = max(dt, prev_dt), dt
        if dt <= 0 or dt > 0.25:
            continue
        dts.append(dt)
        step = math.dist(a['d'], b['d'])
        if step > max(TP_MIN, TP_FACTOR * top * allow_dt) and not near_come(b['t']):
            j = (round(b['t'], 2), round(step, 2), round(dt * 1000))
            if not (in_view(a, wh) and in_view(b, wh)):
                off_view.append(j)
            elif i > 0 and not in_view(pup[i - 1], wh) and radial_step(a, b) >= 0.7 * step:
                entry.append(j)
            else:
                jumps.append(j)
    span = (pup[-1]['t'] - pup[0]['t']) if len(pup) > 1 else 0.0
    tps = sum(1 for a, b in zip(pup, pup[1:]) if b['tp'] > a['tp'] and not near_come(b['t']))
    # the delay that fits best (over the frames in which the fighter moves faster than 1 m/s: standing frames fit
    # any delay), then the error at it and at no delay
    def own_speed(t):
        lo = [r for r in own if t - 0.15 <= r['t'] <= t + 0.15]
        return max((r['gv'] for r in lo), default=0.0)
    mov = [r for r in pup[::2] if own and r['t'] - 0.8 >= own[0]['t'] and own_speed(r['t']) > 1.0 and not near_come(r['t'])] if own else []
    best = None
    for k in range(0, 41):
        lag = 0.02 * k
        errs = [math.dist(r['d'], interp(own, r['t'] - lag)) for r in mov]
        med = pct(errs, 0.5)
        if errs and (best is None or med < best[1] - 1e-6):
            best = (lag, med)
    lag = best[0] if best else 0.0
    errs = [math.dist(r['d'], interp(own, r['t'] - lag)) for r in pup if own and r['t'] - lag >= own[0]['t']]
    errs0 = [math.dist(r['d'], interp(own, r['t'])) for r in pup if own and r['t'] >= own[0]['t']]
    errs_mov0 = [math.dist(r['d'], interp(own, r['t'])) for r in mov]
    moving = sum(1 for r in own if r['gv'] > 0.5) / len(own) if own else 0
    seen = sum(1 for r in pup if in_view(r, wh)) / len(pup) if pup else 0
    return {'label': label, 'samples': len(pup), 'span': span, 'top': top, 'jumps': jumps, 'tps': tps,
            'off_view': off_view, 'entry': entry, 'seen': seen,
            'lag': lag, 'err_p50': pct(errs, 0.5), 'err_p95': pct(errs, 0.95), 'err_max': max(errs) if errs else 0,
            'now_p95': pct(errs0, 0.95), 'mov_now_p95': pct(errs_mov0, 0.95), 'mov_n': len(mov),
            'fps': 1 / (sum(dts) / len(dts)) if dts else 0, 'moving': moving,
            'own_speed_p50': pct([r['gv'] for r in own if r['gv'] > 0.3], 0.5)}


def anim_events(rows, who_own):
    """[(t, anim)] of the animations counted in the rows (ranims grows): the partner's ones played on the puppet
    (observer) or the own ones sent (fighter)"""
    out, last = [], None
    for r in rows:
        if (r['who'] == 255) != who_own or 'ranims' not in r:
            continue
        if last is not None and r['ranims'] > last:
            out.append((r['t'], r['ranim'], r['ranims'] - last))
        last = r['ranims']
    return out


def jump_context(obs, fig, jumps):
    """BUG-0414: per jump, the partner's animations played on the puppet in the 0.6 s before it, its state flags
    and the fighter's own animations around then (the tracks start together)"""
    pa, fa = anim_events(obs, False), anim_events(fig, True)
    pup = {round(r['t'], 2): r for r in obs if r['who'] != 255}
    out = []
    for t, step, ms in jumps:
        r = pup.get(t)
        near = [f'{a}@{t - ta:.2f}' for ta, a, _ in pa if t - 0.6 <= ta <= t]
        own = [f'{a}@{ta - t:+.2f}' for ta, a, _ in fa if t - 1.0 <= ta <= t + 0.3]
        out.append(f'jump {t} {step} m: flags {r["flags"] if r else "?"} anim {r["anim"] if r else "?"}; played on it '
                   f'before (s): {near or "none"}; the fighter\'s own (s from it): {own or "none"}')
    return out


def to_nest(host, guest):
    """both players next to the nearest nest of another species (the NPCs at the start are the players'
    family: a pack does not fight its own)"""
    fam = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    fam = fam.group(1).zfill(8) if fam else ''
    cand = sorted((float(m.group(3)), m.group(1)) for m in re.finditer(
        r'^\s*#(\d+) \S+ species=(\S+) hp=[\d.]+ dist=([\d.]+) pos=', host.call('coop.npc list')['out'], re.M)
        if m.group(2) != fam)
    if not cand:
        return
    host.call(f'coop.npc gonpc {cand[0][1]} 10')
    time.sleep(1)
    guest.call('coop.team come')
    time.sleep(3)
    print(f'  both 10 m from NPC #{cand[0][1]} ({cand[0][0]:.0f} m from the start)', flush=True)


def direction(fighter, observer, fname, oname, rng, secs, ck):
    if (partner_dist(fighter) or 0) > 10:  # (before the track starts: its teleport is not judged)
        fighter.call('coop.team come')
        time.sleep(2)
    watch = Watch(observer)
    watch.start()
    time.sleep(1.5)
    for c in (fighter, observer):
        c.call('coop.track start')
    t_track = time.time()
    time.sleep(0.5)
    comes = []
    log = fight(fighter, rng, secs, [], t_track, comes)
    watch.stop.set()
    watch.join()
    time.sleep(1.0)
    shot(observer, f'puppet_fight_{oname}_sees_{fname}')
    for c in (fighter, observer):
        c.call('coop.track stop')
    obs, fig = dump_anims(observer), dump_anims(fighter)
    path = os.path.join(lane.out_dir('track'), f'puppet_fight_{oname}_sees_{fname}_{time.strftime("%Y%m%d-%H%M%S")}.json')
    with open(path, 'w') as f:
        json.dump({'observer': obs, 'fighter': fig}, f)
    print(f'  per-frame samples: {path}', flush=True)
    r = analyse(obs, fig, f'{oname} watches {fname} fight', comes, screen(observer))
    print(f'  {fname} fought: {"; ".join(log)}', flush=True)
    mins = max(r['span'] / 60, 0.5)
    allowed = math.ceil(PER_MIN * mins)
    print(f'  {r["label"]}: {r["samples"]} puppet samples over {r["span"]:.0f} s at {r["fps"]:.0f} fps; the fighter\'s top '
          f'speed {r["top"]:.1f} m/s (median while moving {r["own_speed_p50"]:.1f}, moving {r["moving"]:.0%} of its frames)', flush=True)
    print(f'    drawn vs the fighter\'s own path: at the same moment p95 {r["now_p95"]:.2f} m (while it moves: '
          f'{r["mov_now_p95"]:.2f} m, {r["mov_n"]} samples); after the best delay {r["lag"]:.2f} s: p50 {r["err_p50"]:.2f} p95 '
          f'{r["err_p95"]:.2f} max {r["err_max"]:.2f} m; driver teleports {r["tps"]}; jumps {len(r["jumps"])} {r["jumps"][:10]}', flush=True)
    print(f'    the puppet on the observer\'s screen in {r["seen"]:.0%} of the frames; steps out of its view (not seen, '
          f'BUG-0414) {len(r["off_view"])} {r["off_view"][:6]}; low first frames back in view (BUG-0434, not judged) '
          f'{len(r["entry"])} {r["entry"][:6]}', flush=True)
    for line in jump_context(obs, fig, r['jumps'] + r['off_view'] + r['entry']):
        print('    ' + line, flush=True)
    ck(r['samples'] > 300 and r['moving'] > 0.12, f'{r["label"]}: it fought and moved ({r["samples"]} samples, moving '
       f'{r["moving"]:.0%} of its own frames)')
    ck(len(r['jumps']) <= allowed, f'{r["label"]}: drawn jumps within the bound ({len(r["jumps"])} <= {allowed})')
    ck(r['tps'] <= allowed, f'{r["label"]}: the puppet is put in place (teleported) at most {PER_MIN:g} per minute '
       f'({r["tps"]} <= {allowed})')
    ck(r['mov_n'] >= 50 and r['mov_now_p95'] <= ERR_P95,
       f'{r["label"]}: drawn where the fighter is at the same moment while it moves (p95 {r["mov_now_p95"]:.2f} m <= '
       f'{ERR_P95} m over {r["mov_n"]} samples)')
    ck(r['lag'] <= LAG_MAX, f'{r["label"]}: the drawn creature trails the fighter by at most {LAG_MAX} s ({r["lag"]:.2f} s)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default='both', choices=('host', 'guest', 'both'), help='who fights')
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--secs', type=float, default=50)
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  seed {seed}', flush=True)
    rng = random.Random(seed)
    host, guest = start('creature')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 30:
        if re.search(r'puppet pos=', host.call('coop.status')['out']) and re.search(r'puppet pos=', guest.call('coop.status')['out']):
            break
        time.sleep(1)
    settle((host, guest))
    to_nest(host, guest)
    if os.environ.get('SPORE_NET'):
        print('  network: ' + os.environ['SPORE_NET'], flush=True)
    if a.dir in ('host', 'both'):
        direction(host, guest, 'host', 'guest', rng, a.secs, ck)
    if a.dir in ('guest', 'both'):
        direction(guest, host, 'guest', 'host', rng, a.secs, ck)
    for c in (host, guest):  # full health again (the next warm test)
        full_hp(c)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
