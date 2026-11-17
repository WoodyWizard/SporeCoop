#!/usr/bin/env python3
"""A guest's copy of a host creature that stands on steep ground stands still too (BUG-1228).

The 0.7.15 final check (lab 20261116-025154 campaign_tc2, Civ): copies of a standing herd on a steep slope of the
guest's ground (the guest's terrain lay 10 m higher there than the host's) were slid down by the guest's game
(velocity 19-22 m/s), pulled back by the driving and trail-teleported onto their target every ~2.5 s: a player saw
the animals slide and pop back.

The test makes the guest's side of it: a host creature that stands on steep ground, a step up from its copy.
1. Two games on civ_npc (SLOPE_FIXTURE=creature / tribe: those fixtures), both cameras on a herd; a host creature the
   guest shows.
2. Up to 3 steep spots (`coop.ground steep`: 50-70 degrees over 6 m; Civ: near a known one of the fixture's planet,
   else around the guest's avatar / tribe). On each the host's game holds its creature (`coop.npctrack put <id> x y z
   <s>`: put there every frame, no velocity: it stands there for the guest), 3 s later 1.5 m further up the slope (the
   copy is then below its target, as on the final check's guest).
3. 3 s later, 12 s every 0.2 s: its copy on the guest (`coop.npc list`: where the guest's game has it, the driving's
   teleports).
4. Checks: on every spot the copy is teleported at most twice and moves less than 3 m in the 12 s.
   INCONCLUSIVE: no steep ground, or the guest's game slid no standing copy (`coop.npc status` slope_slides).
SPORE_FIX_OFF=npc.slope_hold: the copy slides down and is teleported back every ~1.5 s: FAIL.

  SPORE_LANE=5 python3 tools/tests/coop_civ_npc_slope.py
"""
import os
import re
import time

from coop_civ_wild_reused import rows
from coop_npc_walk import avatar_pos, host_npcs, host_party, proxies
from play_civ import dist, frame_city
from twoinst import Checks, keyframe, start

FIXTURE = os.environ.get('SLOPE_FIXTURE', 'civ_npc')
CIV = FIXTURE.startswith('civ')
SECS = 12          # s measured on each spot
SETTLE = 3         # s after the host's creature stepped up the slope
SLOPE = (55, 68)   # degrees over 2 x BASE m (copies slid at 56-66, a 52 degree spot held; a host creature put on
                   # 70 degrees left the game's list)
BASE = 3
SPOTS = 3
SEARCH = 40 if CIV else 60  # m around ANCHOR (Civ) or the guest's avatar
# civ_npc: steep ground where the guest's game slid a standing copy (lab 20261117-142058, 66 degrees)
ANCHOR = (-335.4, 25.6, 387.8)
HOST_STILL = 1.0   # m the host's creature may move over a measurement
MAX_TP = 2         # driving teleports of the copy allowed in SECS
MAX_PATH = 3.0     # m the copy may move in total in SECS


def vec(s):
    return tuple(float(x) for x in s.split())


def host_npc(con, i):
    for ln in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) \S+ species=\S+ .*? pos=\(([-\d. ]+)\) spd=([-\d.]+) .* agl=([-\d.]+)', ln)
        if m and int(m.group(1)) == i:
            return vec(m.group(2)), float(m.group(3)), float(m.group(4))
    return None


def guest_copy(con, i):
    for ln in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) species=\S+ spawned .*? err=([-\d.]+) .*? want=\(([-\d. ]+)\) at=\(([-\d. ]+)\) '
                     r'agl=([-\d.]+) want_agl=([-\d.]+) .*? spd=([-\d.]+) .*? own=([-\d.]+) tp=(\d+) stall=(\d+) trail=(\d+)', ln)
        if m and int(m.group(1)) == i:
            return {'err': float(m.group(2)), 'want': vec(m.group(3)), 'at': vec(m.group(4)), 'agl': float(m.group(5)),
                    'want_agl': float(m.group(6)), 'spd': float(m.group(7)), 'own': float(m.group(8)),
                    'tp': int(m.group(9)), 'trail': int(m.group(11))}
    return None


def sample(host, guest, vid, secs):
    out, t0 = [], time.time()
    while time.time() - t0 < secs:
        hs, gs = host_npc(host, vid), guest_copy(guest, vid)
        if hs and gs:
            out.append((time.time() - t0, hs, gs))
        time.sleep(0.2)
    return out


def uphill(con, p, m=1.5):
    """the point m metres from p (along the ground) where the ground is highest of 8 directions"""
    import math
    r = math.sqrt(sum(v * v for v in p))
    up = tuple(v / r for v in p)
    ax = (0, 0, 1) if abs(up[2]) < 0.9 else (1, 0, 0)
    e1 = (up[1] * ax[2] - up[2] * ax[1], up[2] * ax[0] - up[0] * ax[2], up[0] * ax[1] - up[1] * ax[0])
    l1 = math.sqrt(sum(v * v for v in e1))
    e1 = tuple(v / l1 for v in e1)
    e2 = (up[1] * e1[2] - up[2] * e1[1], up[2] * e1[0] - up[0] * e1[2], up[0] * e1[1] - up[1] * e1[0])
    best = None
    for k in range(8):
        a = k * math.pi / 4
        q = tuple(p[i] + (e1[i] * math.cos(a) + e2[i] * math.sin(a)) * m for i in range(3))
        g = float(re.search(r'ground ([\d.]+)', con.call(f'coop.ground {q[0]:.2f} {q[1]:.2f} {q[2]:.2f}')['out']).group(1))
        if best is None or g > best[0]:
            best = (g, q)
    return best[1]


def pick(host, guest):
    """(a host creature the guest shows, the centre to look for steep ground around)"""
    if CIV:
        h = {i: r for i, r in rows(host, 'npc').items() if r['alive']}
        if len(h) < 2:
            return None, None
        pts = [r['pos'] for r in h.values()]
        c0 = max(pts, key=lambda p: sum(1 for q in pts if dist(p, q) < 30))
        frame_city({'host': host, 'guest': guest}, c0, answer=False)
        time.sleep(4)
        h, g = rows(host, 'npc'), rows(guest, 'proxy')
        cand = sorted((dist(r['pos'], c0), i) for i, r in h.items() if r['alive'] and i in g and g[i]['alive'])
        return (cand[0][1] if cand else None), c0
    c0 = avatar_pos(guest)
    hs, gs, party = host_npcs(host), proxies(guest), host_party(host)
    cand = sorted((dist(gs[i][0], c0), i) for i in hs if i in gs and c0 and i not in party)
    return (cand[0][1] if cand else None), c0


def slides(guest):
    m = re.search(r'slope_slides=(\d+)', guest.call('coop.npc status')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start(FIXTURE)
    ck = Checks()
    time.sleep(5)
    vid, c0 = pick(host, guest)
    if not ck(vid is not None, f'a host creature the guest shows ({vid}, centre {c0})'):
        return ck.result()
    x, y, z = ANCHOR if CIV else c0
    out = host.call(f'coop.ground steep {x:.2f} {y:.2f} {z:.2f} {SEARCH} {BASE} 400')['out']
    steep = [(float(a), vec(b)) for a, b in re.findall(r'steep ([\d.]+) deg at \(([-\d. ]+)\)', out)]
    if CIV:  # (the known spot first)
        steep.sort(key=lambda e: dist(e[1], ANCHOR))
    spots = []
    for d, p in steep:
        if SLOPE[0] <= d <= SLOPE[1] and all(dist(p, q) > 10 for _, q in spots):
            spots.append((d, p))
    spots = spots[:SPOTS]
    print(f'  host: {len(steep)} steep points, spots {[round(d, 1) for d, _ in spots]}', flush=True)
    if not spots:
        return ck.inconclusive(f'no ground of {SLOPE[0]}-{SLOPE[1]} degrees near {x:.0f} {y:.0f} {z:.0f}')
    s0 = slides(guest)
    bad, measured = [], 0
    for d, sp in spots:
        out = host.call(f'coop.npctrack put {vid} {sp[0]:.2f} {sp[1]:.2f} {sp[2]:.2f} 60')['out'].strip()
        if 'no such' in out:  # (the game took it out of the world: another one)
            vid, _ = pick(host, guest)
            out = host.call(f'coop.npctrack put {vid} {sp[0]:.2f} {sp[1]:.2f} {sp[2]:.2f} 60')['out'].strip()
        print('  host:', out, flush=True)
        if CIV:
            frame_city({'host': host, 'guest': guest}, sp, answer=False)
        time.sleep(3)
        up = uphill(host, sp)
        print('  host:', host.call(f'coop.npctrack put {vid} {up[0]:.2f} {up[1]:.2f} {up[2]:.2f} 60')['out'].strip(), flush=True)
        time.sleep(SETTLE)
        samples = sample(host, guest, vid, SECS)
        for t, hs, gs in samples[::10]:
            print(f'  {t:5.1f} s guest at ({gs["at"][0]:.1f} {gs["at"][1]:.1f} {gs["at"][2]:.1f}) err {gs["err"]:.2f} '
                  f'host state spd {gs["spd"]:.1f} its game\'s speed {gs["own"]:.1f} tp {gs["tp"]} trail {gs["trail"]}', flush=True)
        if len(samples) < SECS * 2:
            print(f'  spot {d:.1f} deg: only {len(samples)} samples', flush=True)
            continue
        measured += 1
        want_moved = max(dist(q[2]['want'], samples[0][2]['want']) for q in samples)
        path = sum(dist(a[2]['at'], b[2]['at']) for a, b in zip(samples, samples[1:]))
        tps = samples[-1][2]['tp'] - samples[0][2]['tp']
        print(f'  spot {d:.1f} deg: the host\'s state moved {want_moved:.2f} m; the copy moved {path:.1f} m, teleported '
              f'{tps} times (trail {samples[-1][2]["trail"] - samples[0][2]["trail"]}), its game\'s speed max '
              f'{max(q[2]["own"] for q in samples):.1f} m/s', flush=True)
        if want_moved <= HOST_STILL and (tps > MAX_TP or path >= MAX_PATH):
            bad.append(f'{d:.0f} deg: moved {path:.1f} m, {tps} teleports')
        if not bad:
            keyframe('slope', 'the creature on the slope on both screens', players={'host': host, 'guest': guest})
    host.call(f'coop.npctrack put {vid} {x:.2f} {y:.2f} {z:.2f} 0')
    n = slides(guest) - s0
    print(f'  guest: standing copies its game slid: {n}', flush=True)
    if not measured:
        return ck.inconclusive(f'no spot measured (the host\'s creature or its copy left the world on {len(spots)} spots)')
    ck(not bad, f'the copy of the standing creature stands on every slope ({"; ".join(bad) or "all"}: at most {MAX_TP} '
       f'teleports and {MAX_PATH} m in {SECS} s)')
    if not bad and n <= 0:
        return ck.inconclusive('the guest\'s game slid no standing copy: no slope tested the hold')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
