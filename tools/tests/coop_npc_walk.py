#!/usr/bin/env python3
"""What a guest sees of the host's walking and running NPCs, frame by frame (BUG-0361).

The guest shows the host's NPC animals as proxies (mods/coop/npc.cpp): standing they are driven by
drive::step, moving (Creature stage, npc.kinematic, BUG-0281) they were teleported onto the host's
path every frame. coop_puppet_walk.py (BUG-0360) measures the partner's puppet this way; this test
does the same for NPC proxies (`coop.npctrack`: every frame, every proxy within RADIUS m of the
guest's avatar: the position the game drew, the interpolated host state, the game's speed, the
animation's walk gait, on the screen or not).

1. Two games on the creature fixture (TRACK_FIXTURE=tribe: the tribe fixture).
2. The guest tracks its proxies while
   * wander (WANDER s): the host's NPCs do what their AI does;
   * walk (WALK s): the host's game walks up to 4 NPCs the guest mirrors by their own locomotion
     (`coop.npctrack walk`: SetDesiredSpeed + MoveTo, as the AI does) to points 4-12 m around the
     guest's avatar, a new point every 2-4 s, at walking (2-3 m/s) and running (4.5-6 m/s) speeds;
   * flee (FLEE s): the host's avatar hits two of them (`coop.npc hosthit`, a small hit): they flee
     or fight by their own AI.
   The guest's camera keeps one of the walkers on the screen (right-drags), as a player who watches.
3. Checks over the frames in which the proxy's host state moves (the interpolated target's path
   faster than 1 m/s in a 0.1 s window), all proxies together:
   * no jump: no frame step of the drawn proxy on the guest's screen longer than TP_FACTOR x the path's
     top speed x the frame time (and at least TP_MIN m);
   * no steps: in 0.1 s windows the drawn proxy moves at 0.6-1.4x the path's speed in at least
     MATCHED_MIN of them and stands (< 0.3x) in at most STAND_MAX;
   * it walks: the animation's walk gait (+0xB54 -> +0x17C -> +0x15B8 > 0) is on in at least GAIT_MIN of
     the moving frames in which the proxy is on the guest's screen (a creature behind the camera is
     animated rarely: its gait says nothing).
   The drawn position is the proxy's position before the mod's frame (the mod runs at Present).

  coop_npc_walk.py [--seed N] [--wander S] [--walk S] [--flee S] [--goto x,y,z] [--falls N]
  --goto: both avatars onto that ground first (a slope of the creature fixture: -195,-465,-45); the walkers are
  the host's NPCs within 60 m then. --falls N: also fail when the walked proxies start to fall (+0x137 going
  1 -> 0 while the host's NPC moves) more than N times more often than the host's own moving NPCs
  (BUG-0366's downhill falls, for proxies)
  NPC_WALK_GUEST_CMDS="cmd;cmd": console commands on the guest first (e.g. frame.limit 66)
  The walkers are NPCs outside the host avatar's pack and family (BUG-0417: the flee phase's hit by the host's
  avatar on its own family made the family fight the host back, the monitor's posse_damage). NPC_WALK_PARTY=1:
  the family / pack first (the test before the fix; the monitor gate must fail when one was hit).
  (SPORE_FIX_OFF=npc.walk must FAIL)"""
import argparse
import json
import math
import os
import random
import re
import threading
import time

import lane
from twoinst import Checks, shot, start

FIXTURE = os.environ.get('TRACK_FIXTURE', 'creature')
# m around the guest's avatar (Tribe: its player tribe; the animals live 50-120 m from it there)
RADIUS = float(os.environ.get('TRACK_RADIUS', '150' if FIXTURE == 'tribe' else '40'))
TP_FACTOR, TP_MIN = 3.0, 0.75
STAND_MAX, MATCHED_MIN = 0.05, 0.85
GAIT_MIN = 0.80
FAR_OUTSIDE = 100.0  # m: the walkers outside the host's pack / family (BUG-0417)
# Tribe: herd animals 50-120 m from the tribe push each other (the game's own collision moved a proxy 1.3 m
# in one 31 ms frame), the tribe games run at 36-60 fps and the host's path jumps more often (late states): with
# the fix 80-94 % matched (61-77 % without), up to 2 steps of 0.8-2.1 m on the screen per run. Allowed there:
# MATCHED_MIN_TRIBE and up to JUMPS_TRIBE steps under JUMP_MAX_TRIBE m; on the path instead: the drawn proxy's
# median distance from the host's path at most ERR_P50_TRIBE (with the fix 0.01-0.03 m, without 0.23-0.33 m).
MATCHED_MIN_TRIBE, JUMPS_TRIBE, JUMP_MAX_TRIBE, ERR_P50_TRIBE = 0.75, 3, 2.5, 0.10


# BUG-0394: a window spans at least WIN_FRAMES frames: at 21-29 fps a 0.1 s window held 1-3 frames and its speed
# ratio swung 0.3-2x with the frame times (the game moves a creature by its own frame time), 79-81 % matched
# with drawn errors of 0.01-0.6 m; over 4 frames 89-98 %. At 60 fps 4 frames are shorter than 0.1 s (unchanged);
# a puppet that stands 4 frames while its path moves still counts as standing
WIN_FRAMES = 4
HITCH = 0.1  # s: a guest frame this long is a hitch of its game (BUG-0407)

def screen(con):
    m = re.search(r'viewport x=\d+ y=\d+ w=(\d+) h=(\d+)', con.call('coop.cam')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else (1440, 1080)


def dump(con):
    rows = []
    n = int(re.search(r'samples=(\d+)', con.call('coop.npctrack count')['out']).group(1))
    for i in range(0, n, 2000):
        for line in con.call(f'coop.npctrack dump {i} 2000')['out'].splitlines():
            f = line.split()
            if len(f) < 25:
                continue
            rows.append({'t': float(f[0]), 'id': int(f[1]), 'how': int(f[2]), 'flags': int(f[3]), 'tp': int(f[4]),
                         'd': tuple(map(float, f[5:8])), 'p': tuple(map(float, f[8:11])),
                         'tgt': tuple(map(float, f[11:14])), 'gv': float(f[14]), 'rv': float(f[15]),
                         'da': float(f[16]), 'anim': f[17], 'gait': int(f[18]), 'gait2': int(f[19]),
                         'moving': int(f[20]), 'ss': int(f[21]), 'vis': int(f[22]), 'sx': float(f[23]),
                         'sy': float(f[24]), 'm': tuple(map(float, f[25:28])) if len(f) >= 28 else None})
    return rows


def host_npcs(con):
    """the host's living NPCs: id -> pos"""
    out = {}
    for m in re.finditer(r'^\s*#(\d+) \S+ species=\S+ hp=([\d.]+) dist=\S+ pos=\((\S+) (\S+) (\S+)\) spd=([\d.]+)',
                         con.call('coop.npc list')['out'], re.M):
        if float(m.group(2)) > 0:
            out[int(m.group(1))] = (tuple(map(float, m.group(3, 4, 5))), float(m.group(6)))
    return out


def host_party(con):
    """ids of the host avatar's pack and family (`coop.npc list` tags them, BUG-0390)"""
    return {int(m.group(1)) for m in re.finditer(r'^\s*#(\d+) .*? (?:pack|family)\b', con.call('coop.npc list')['out'], re.M)}


def family(con):
    """the host's NPCs in its avatar's herd or pack (`coop.npc list` tags ` family` / ` pack`)"""
    return {int(m.group(1)) for m in re.finditer(r'^\s*#(\d+) .*', con.call('coop.npc list')['out'], re.M)
            if ' family' in m.group(0) or ' pack' in m.group(0)}


def proxies(con):
    """the guest's spawned proxies: id -> (pos, distance to its avatar)"""
    return {int(m.group(1)): (tuple(map(float, m.group(3, 4, 5))), float(m.group(2)))
            for m in re.finditer(r'^\s*#(\d+) species=\S+ spawned hp=\S+ dist=([\d.]+) .*? at=\((\S+) (\S+) (\S+)\)',
                                 con.call('coop.npc list')['out'], re.M)}


def avatar_pos(con):
    """the guest's avatar (Tribe: its player tribe), the centre coop.npctrack measures around"""
    m = re.search(r'centre=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.npctrack count')['out'])
    return tuple(map(float, m.groups())) if m else None


def tangent_point(c, rng, rmin, rmax):
    """a random point rmin..rmax m from c on the planet's tangent plane (the game puts it on the ground)"""
    r = math.sqrt(sum(x * x for x in c))
    up = [x / r for x in c]
    a = [0.0, -up[2], up[1]] if abs(up[0]) < 0.9 else [-up[2], 0.0, up[0]]
    al = math.sqrt(sum(x * x for x in a))
    a = [x / al for x in a]
    b = [up[1] * a[2] - up[2] * a[1], up[2] * a[0] - up[0] * a[2], up[0] * a[1] - up[1] * a[0]]
    ang, d = rng.uniform(0, 2 * math.pi), rng.uniform(rmin, rmax)
    return [c[i] + (a[i] * math.cos(ang) + b[i] * math.sin(ang)) * d for i in range(3)]


class Watch(threading.Thread):
    """the guest's camera turns towards the NPC it watches (right-drags), as a watching player does"""
    def __init__(self, con):
        super().__init__(daemon=True)
        self.con, self.stop, self.drags, self.nid = con, threading.Event(), 0, None
        self.W, self.H = screen(con)
        # BUG-0407: walk_phase asks the guest for its avatar while this thread turns its camera: one console, one
        # caller at a time (two threads on one socket can take each other's replies)
        self.lock = threading.Lock()

    def run(self):
        while not self.stop.is_set():
            try:
                with self.lock:
                    self.turn()
            except OSError as e:  # a console call that timed out on a loaded machine
                print(f'  watch: {e!r}', flush=True)
            self.stop.wait(0.6)

    def turn(self):
        if self.nid is not None:
            m = re.search(r'samples=(\d+)', self.con.call('coop.npctrack count')['out'])
            if not m:
                return
            n = int(m.group(1))
            xy = None
            for line in self.con.call(f'coop.npctrack dump {max(0, n - 60)} 60')['out'].splitlines():
                f = line.split()
                if len(f) >= 25 and int(f[1]) == self.nid:
                    xy = (int(f[22]), float(f[23]), float(f[24]))
            if not xy or not xy[0] or not (100 < xy[1] < self.W - 100 and 80 < xy[2] < self.H - 150):
                dx = 300 if not xy or not xy[0] or xy[1] > self.W // 2 else -300
                self.con.call(f'drag {self.W // 2} {self.H // 2} {self.W // 2 + dx} {self.H // 2} right')
                self.drags += 1


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else 0.0


def analyse(rows, W, H, phases):
    by = {}
    for r in rows:
        by.setdefault(r['id'], []).append(r)
    res = {'ids': 0, 'frames': 0, 'jumps': [], 'win': [], 'near': 0, 'near_gait': 0, 'errs': [], 'dts': [],
           'how': {}, 'per': {}, 'phase': {}}
    tops = [r['rv'] for r in rows if r['rv'] > 0.3]
    top = pct(tops, 0.95) or 5.0
    res['top'] = top

    def phase_of(t):
        for name, a, b in phases:
            if a <= t < b:
                return name
        return '-'
    for nid, rs in by.items():
        rs.sort(key=lambda r: r['t'])
        moving_frames = 0
        pw = {'win': 0, 'stand': 0, 'matched': 0, 'near': 0, 'gait': 0, 'jumps': 0}
        pdt = 0.0
        for a, b in zip(rs, rs[1:]):
            dt = b['t'] - a['t']
            # the game moves a creature by its own frame time, which is the frame before ours: with uneven
            # frames (86 ms, then 42 ms) a creature at 7.5 m/s moved 0.74 m between two samples 42 ms apart
            jdt, pdt = max(dt, pdt), dt
            if dt <= 0 or dt > 0.25 or b['how'] == 2 or a['how'] == 2:
                continue
            res['dts'].append(dt)
            path = math.dist(a['tgt'], b['tgt']) / dt
            if b['rv'] < 0.5 and path < 0.5:
                continue
            moving_frames += 1
            res['how'][b['how']] = res['how'].get(b['how'], 0) + 1
            step = math.dist(a['d'], b['d'])
            onscr = all(x['vis'] and 0 < x['sx'] < W and 0 < x['sy'] < H for x in (a, b))
            # BUG-0407: TP_FACTOR x the speed the host's NPC has now when that is over the run's top speed (the p95
            # of every reported speed, 4 m/s): a proxy that caught up 0.76 m in a 62 ms frame behind its host NPC
            # fleeing at 9.2 m/s (1.3x its speed) was a "jump" (npc_walk, lab 20261101-135017)
            lim = max(TP_MIN, TP_FACTOR * max(top, a['rv'], b['rv']) * jdt)
            if not onscr:
                # off the screen the game updates a far creature in bursts (no move for 0.2 s, then 1.9 m at
                # once, Tribe animals 70-100 m away): counted, not judged
                if step > lim:
                    res.setdefault('jumps_off', []).append((nid, round(b['t'], 2), round(step, 2)))
            elif step > lim and b['tp'] == a['tp']:
                res['jumps'].append((nid, round(b['t'], 2), round(step, 2), round(dt * 1000)))
                pw['jumps'] += 1
            elif step > lim:
                res['jumps'].append((nid, round(b['t'], 2), round(step, 2), round(dt * 1000), 'driver tp'))
                pw['jumps'] += 1
            res['errs'].append(math.dist(b['d'], b['tgt']))
            if b['vis'] and 0 < b['sx'] < W and 0 < b['sy'] < H and b['rv'] > 0.5:
                res['near'] += 1
                pw['near'] += 1
                if b['gait'] > 0:
                    res['near_gait'] += 1
                    pw['gait'] += 1
        res['frames'] += moving_frames
        i = 0
        while i < len(rs):
            j = i
            while j < len(rs) and (rs[j]['t'] - rs[i]['t'] < 0.1 or j - i < WIN_FRAMES):
                j += 1
            if j >= len(rs):
                break
            dt = rs[j]['t'] - rs[i]['t']
            path = math.dist(rs[i]['tgt'], rs[j]['tgt']) / dt
            # the target itself jumped in the window (a late state, the extrapolation past the newest state
            # corrected: 1.3 m in one frame on a loaded 36 fps guest): the window measures the network, not the
            # driving (the proxy closes such a jump at the catch-up speed); counted, not judged (as BUG-0321)
            tj = any(math.dist(rs[k]['tgt'], rs[k + 1]['tgt']) > max(0.3, 2.5 * top * (rs[k + 1]['t'] - rs[k]['t']))
                     for k in range(i, j))
            # BUG-0407: drawn one frame after the path (what a frame draws was placed in the frame before), and a
            # window with a guest frame over HITCH in it or just before it left out (counted), as in puppet_walk
            sh = j + 1 < len(rs) and rs[j + 1]['t'] - rs[j]['t'] < 0.25 and rs[j + 1]['how'] != 2
            hitch = any(rs[k + 1]['t'] - rs[k]['t'] > HITCH for k in range(max(0, i - 1), min(j + 1, len(rs) - 1)))
            if path > 1.0 and dt < 0.3 and rs[i]['how'] != 2 and rs[j]['how'] != 2 and j - i >= 2 and tj:
                res['win_tj'] = res.get('win_tj', 0) + 1
            elif path > 1.0 and dt < 0.3 and rs[i]['how'] != 2 and rs[j]['how'] != 2 and j - i >= 2 and (hitch or not sh):
                res['win_hitch'] = res.get('win_hitch', 0) + 1
            elif path > 1.0 and dt < 0.3 and rs[i]['how'] != 2 and rs[j]['how'] != 2 and j - i >= 2:
                res.setdefault('win_legacy', []).append(math.dist(rs[i]['d'], rs[j]['d']) / dt / path)
                w = math.dist(rs[i + 1]['d'], rs[j + 1]['d']) / (dt * path)
                res['win'].append(w)
                if all(r['vis'] and 0 < r['sx'] < W and 0 < r['sy'] < H for r in rs[i:j + 1]):
                    res.setdefault('win_on', []).append(w)
                ph = phase_of(rs[i]['t'])
                p = res['phase'].setdefault(ph, [0, 0, 0])
                p[0] += 1
                p[1] += w < 0.3
                p[2] += 0.6 <= w <= 1.4
                pw['win'] += 1
                pw['stand'] += w < 0.3
                pw['matched'] += 0.6 <= w <= 1.4
            i = j
        if moving_frames:
            res['ids'] += 1
            res['per'][nid] = pw
    return res


def walk_phase(host, guest, rng, secs, watch, log, near=30):
    """the host's game walks up to 4 NPCs the guest mirrors around the guest's avatar"""
    hs, gs, party = host_npcs(host), proxies(guest), host_party(host)
    g = avatar_pos(guest)
    # the family lives next to the avatar, the other species at their nests: the walkers outside the party come from
    # up to FAR_OUTSIDE m (the walk takes them to the guest; within 30 m of it there were only the family's 4)
    lim = 120 if FIXTURE == 'tribe' else (near if os.environ.get('NPC_WALK_PARTY') == '1' else max(near, FAR_OUTSIDE))
    both = sorted((math.dist(gs[i][0], g), i) for i in hs if i in gs and g and math.dist(gs[i][0], g) < lim)
    # BUG-0417: the flee phase's hit (`coop.npc hosthit`: TakeDamage with the host's avatar as the attacker) on a
    # member of the avatar's own family or pack is the player attacking its own party, which the phase does not
    # mean to test; the family fought the host back and the monitor (posse_damage) failed the test. The walkers
    # are NPCs outside both (as the creature test's hit, BUG-0390). NPC_WALK_PARTY=1: the family / pack first.
    if os.environ.get('NPC_WALK_PARTY') == '1':
        both.sort(key=lambda x: (x[1] not in party, x[0]))
    else:
        both = [x for x in both if x[1] not in party]
    ids = [i for _, i in both[:4]]
    home = {i: hs[i][0] for i in ids}  # Tribe: they walk around where they were (far from the tribe)
    log.append(f'walkers {ids} (of {len(hs)} host NPCs, {len(gs)} proxies; host pack / family {sorted(party)}, '
               f'walkers in it: {sorted(set(ids) & party)})')
    if not ids:
        return ids
    watch.nid = ids[0]
    t_end = time.time() + secs
    nxt = {i: 0 for i in ids}
    while time.time() < t_end:
        with watch.lock:
            g = avatar_pos(guest)
        for i in ids:
            if time.time() < nxt[i] or not g:
                continue
            sp = rng.choice([2.0, 3.0, 4.5, 6.0])
            p = tangent_point(home[i], rng, 3, 10) if FIXTURE == 'tribe' else tangent_point(g, rng, 4, 12)
            out = host.call(f'coop.npctrack walk {i} {p[0]:.1f} {p[1]:.1f} {p[2]:.1f} {sp}')['out'].strip()
            if 'no such' in out:
                nxt[i] = t_end + 1
                continue
            nxt[i] = time.time() + rng.uniform(2, 4)
            log.append(f'#{i} {sp} m/s')
        time.sleep(0.2)
    return ids



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
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--wander', type=float, default=15)
    ap.add_argument('--walk', type=float, default=30)
    ap.add_argument('--flee', type=float, default=15)
    ap.add_argument('--goto', default='', help='x,y,z: both players onto that ground first (a slope)')
    ap.add_argument('--falls', type=float, default=-1, help='fail over this many fall starts per 100 s of walked proxy '
                    'over the host\'s own NPCs\' rate (-1: report only)')
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  seed {seed}', flush=True)
    rng = random.Random(seed)
    host, guest = start(FIXTURE)
    ck = Checks()
    for c in filter(None, os.environ.get('NPC_WALK_GUEST_CMDS', '').split(';')):
        print('  guest:', c, guest.call(c.strip())['out'].strip(), flush=True)
    if os.environ.get('NPC_WALK_INTRO'):  # experiment: the menu intro played in the stage and ended by world.cpp
        m = re.search(r'eax=0x([0-9A-Fa-f]+)', guest.call('call Simulator::cCinematicManager::Get cdecl')['out'])
        if m:
            print('  guest:', guest.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall 0x{m.group(1)} '
                                         's:GGE_Intro 1 1 0 0 0')['out'].strip(), flush=True)
            if os.environ.get('NPC_WALK_INTRO') == '2':  # the proxies made again while it plays
                print('  guest:', guest.call('coop.npc off')['out'].strip(), flush=True)
                time.sleep(0.3)
                print('  guest:', guest.call('coop.npc on')['out'].strip(), flush=True)
            time.sleep(4)
    t0 = time.time()
    while time.time() - t0 < 30 and not proxies(guest):
        time.sleep(1)
    # the centre coop.npctrack measures around (the avatar; Tribe: the player tribe) on both games: one lab run
    # had none on either game for the whole test (0 rows; cause unknown, the trace said avatar=yes)
    t0 = time.time()
    while time.time() - t0 < 40 and not (avatar_pos(host) and avatar_pos(guest)):
        time.sleep(1)
    for name, c in (('host', host), ('guest', guest)):
        if not avatar_pos(c):
            print(f'  {name}: no centre: {c.call("coop.npctrack count")["out"].strip()} | '
                  f'{c.call("coop.avatar")["out"].strip()[:160]} | {c.call("game.mode")["out"].strip()}', flush=True)
    if a.goto:  # both onto that ground, the guest 3 m beside the host (coop_puppet_walk.py --goto)
        x, y, z = map(float, a.goto.split(','))
        print('  host: ' + host.call(f'coop.abilities goto {x} {y} {z}')['out'].strip(), flush=True)
        print('  guest: ' + guest.call(f'coop.abilities goto {x + 3} {y} {z}')['out'].strip(), flush=True)
        time.sleep(3)
    settle((host, guest))
    W, H = screen(guest)
    watch = Watch(guest)
    watch.start()
    for c in (host, guest):
        c.call(f'coop.npctrack start {RADIUS}')
    tt0 = time.time()
    phases, log = [], []
    time.sleep(a.wander)
    phases.append(('wander', 0, time.time() - tt0))
    t1 = time.time() - tt0
    ids = walk_phase(host, guest, rng, a.walk, watch, log, near=60 if a.goto else 30)
    ck(len(ids) >= 2, f'walkers for the walk and flee phases ({next((x for x in log if x.startswith("walkers")), ids)})')
    phases.append(('walk', t1, time.time() - tt0))
    t2 = time.time() - tt0
    # BUG-0407: not a member of the host's own family or pack: hit, it fights back and its hits on the host's avatar
    # are hits by the player's own party (the posse_damage monitor gate failed npc_walk, lab 20261101-143049)
    fam = family(host)
    for i in [i for i in ids if i not in fam][:2]:
        log.append(f'hit #{i}: ' + host.call(f'coop.npc hosthit {i} 0.5')['out'].strip()[:80])
    time.sleep(a.flee)
    phases.append(('flee', t2, time.time() - tt0))
    watch.stop.set()
    watch.join()
    shot(guest, 'npc_walk_guest')
    for c in (host, guest):
        c.call('coop.npctrack stop')
    rows, hrows = dump(guest), dump(host)
    path = os.path.join(lane.out_dir('track'), f'npc_walk_{FIXTURE}_{time.strftime("%Y%m%d-%H%M%S")}.json')
    with open(path, 'w') as f:
        json.dump({'rows': rows, 'host': hrows, 'phases': phases, 'log': log}, f)
    print(f'  per-frame samples: {path} ({len(rows)} rows)', flush=True)
    print('  ' + '; '.join(log)[:1500], flush=True)
    r = analyse(rows, W, H, phases)
    fps = 1 / (sum(r['dts']) / len(r['dts'])) if r['dts'] else 0
    win = r['win']
    stand = sum(1 for w in win if w < 0.3) / len(win) if win else 1
    matched = sum(1 for w in win if 0.6 <= w <= 1.4) / len(win) if win else 0
    near_gait = r['near_gait'] / r['near'] if r['near'] else 0
    print(f'  {r["ids"]} proxies moved, {r["frames"]} moving frames at {fps:.0f} fps (guest); host top speed '
          f'{r["top"]:.2f} m/s; frames by driving {r["how"]} (0 driven, 1 placed, 3 walked); camera drags {watch.drags}', flush=True)
    print(f'    drawn off the target p50 {pct(r["errs"], 0.5):.2f} p90 {pct(r["errs"], 0.9):.2f} max '
          f'{max(r["errs"]) if r["errs"] else 0:.2f} m', flush=True)
    print(f'    0.1 s windows while the host NPC moves: {len(win)}, the proxy stands in {stand:.0%}, moves at 0.6-1.4x '
          f'in {matched:.0%}; by phase (windows, stand, matched): {r["phase"]}', flush=True)
    print(f'    windows left out because the host\'s path itself jumped in them (late states): {r.get("win_tj", 0)}; with a '
          f'guest frame over {HITCH * 1000:.0f} ms: {r.get("win_hitch", 0)} ({sum(1 for x in r["dts"] if x > HITCH) / max(1, len(r["dts"])):.1%} '
          f'of its frames); unshifted (BUG-0407): matched {sum(1 for w in r.get("win_legacy", []) if 0.6 <= w <= 1.4) / max(1, len(r.get("win_legacy", []))):.0%}', flush=True)
    won = r.get('win_on', [])
    if won:
        print(f'    of them on the guest\'s screen: {len(won)}, stand {sum(1 for w in won if w < 0.3) / len(won):.0%}, '
              f'matched {sum(1 for w in won if 0.6 <= w <= 1.4) / len(won):.0%}', flush=True)
    print(f'    jumps on the guest\'s screen (id, t, m, frame ms): {len(r["jumps"])} {r["jumps"][:12]}; off it: '
          f'{len(r.get("jumps_off", []))} {r.get("jumps_off", [])[:6]}', flush=True)
    print(f'    walk gait in {near_gait:.0%} of the {r["near"]} moving frames on the guest\'s screen', flush=True)
    walked = [x for x in rows if x['how'] == 3 and x['rv'] > 0.5]
    if walked:  # diagnostics (one lab run had gait 0 on every proxy, cause unknown)
        cnt = {}
        for x in walked:
            k = (x['anim'], x['gait'], x['moving'], x['ss'])
            cnt[k] = cnt.get(k, 0) + 1
        print(f'    walked proxies (anim, gait, +0x137, +0xF98): {sorted(cnt.items(), key=lambda kv: -kv[1])[:4]}', flush=True)
    # falls (BUG-0366 for proxies): +0x137 going 1 -> 0 while the host's NPC moves, per 100 s of such frames
    def falls(rs, how):
        by, n, t = {}, 0, 0.0
        for x in rs:
            if x['how'] in how:
                by.setdefault(x['id'], []).append(x)
        for xs in by.values():
            xs.sort(key=lambda x: x['t'])
            for u, v in zip(xs, xs[1:]):
                if v['rv'] > 0.5 and 0 < v['t'] - u['t'] < 0.25:
                    t += v['t'] - u['t']
                    n += u['moving'] == 1 and v['moving'] == 0
        return n, t
    fp, tp_ = falls(rows, (3,))
    fh, th = falls(hrows, (4,))
    rp, rh = fp / tp_ * 100 if tp_ else 0, fh / th * 100 if th else 0
    print(f'    fall starts (+0x137 1 -> 0) while moving: walked proxies {fp} in {tp_:.0f} s ({rp:.1f} per 100 s), '
          f'the host\'s own NPCs {fh} in {th:.0f} s ({rh:.1f} per 100 s)', flush=True)
    hm = [x for x in hrows if x['rv'] > 0.5 and x['how'] == 4]
    hv = [x for x in hm if x['vis'] and 0 < x['sx'] < W and 0 < x['sy'] < H]
    print(f'    reference, the host\'s own NPCs while they move: walk gait in {sum(x["gait"] > 0 for x in hm) / max(1, len(hm)):.0%} '
          f'of {len(hm)} frames, {sum(x["gait"] > 0 for x in hv) / max(1, len(hv)):.0%} of {len(hv)} on the host\'s screen', flush=True)
    for nid, pw in sorted(r['per'].items(), key=lambda kv: -kv[1]['win'])[:10]:
        print(f'    #{nid}: windows {pw["win"]} stand {pw["stand"]} matched {pw["matched"]} on screen {pw["near"]} '
              f'gait {pw["gait"]} jumps {pw["jumps"]}', flush=True)
    ck(len(win) > 100, f'the host\'s NPCs moved near the guest ({len(win)} windows of moving NPCs)')
    if FIXTURE == 'tribe':
        big = [j for j in r['jumps'] if j[2] > JUMP_MAX_TRIBE]
        ck(len(r['jumps']) <= JUMPS_TRIBE and not big,
           f'no jump of a moving proxy (Tribe: at most {JUMPS_TRIBE} under {JUMP_MAX_TRIBE} m: {len(r["jumps"])}, '
           f'{len(big)} longer)')
    else:
        ck(not r['jumps'], f'no jump of a moving proxy ({len(r["jumps"])})')
    if FIXTURE == 'tribe':
        e50 = pct(r['errs'], 0.5)
        ck(e50 <= ERR_P50_TRIBE, f'the proxies are on the host\'s path (drawn error p50 {e50:.2f} m <= {ERR_P50_TRIBE} m)')
    mmin = MATCHED_MIN_TRIBE if FIXTURE == 'tribe' else MATCHED_MIN
    ck(stand <= STAND_MAX and matched >= mmin,
       f'the proxies move with the host\'s NPCs, not in steps (stand in {stand:.0%} <= {STAND_MAX:.0%}, '
       f'at their speed in {matched:.0%} >= {mmin:.0%} of {len(win)} windows)')
    ck(r['near'] >= 150 and near_gait >= GAIT_MIN,
       f'the proxies walk where the guest sees them (walk gait in {near_gait:.0%} >= {GAIT_MIN:.0%} of {r["near"]} '
       f'moving frames on its screen)')
    if a.falls >= 0:
        ck(tp_ >= 30 and rp <= rh + a.falls, f'the walked proxies do not keep falling (fall starts {rp:.1f} per 100 s '
           f'<= the host\'s own NPCs {rh:.1f} + {a.falls:g}; {tp_:.0f} s of walked frames)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
