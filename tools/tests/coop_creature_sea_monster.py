#!/usr/bin/env python3
"""BUG-0465: the sea monster that takes a player in deep water is seen in the partner's game, upright where it took him.

The host's creature is put into deep water (cSeaMonsterEvent, Drama Manager kind 6: the death of cause 9 plays the
cinematic CRG_AvatarDied_SeaMonster, whose state 0 starts the effect crg_seamonster_attack 0x8A71529C, the monster,
and monster_water_splash 0xB3965D1D at the avatar). Both games watch that effect id (`coop.effects watch`: the game's
effect object, its transform at effect +0x9C). Checked on the guest's game:
  1. it plays the monster effect (the game's effect object, active) within 1 m of where the host's game made it and
     within 3 m of the host's puppet as the guest's game drew it in the water just before;
  2. with the host's orientation and size: each of the 9 rotation entries within 0.05 of the host's effect's and the
     scale within 5 % (the monster rises out of the water on the planet's surface, not tilted into it);
  3. the effect's up axis is the planet's up at that point there too (|cos| >= 0.95), as in the host's game.
SPORE_FIX_OFF=effects.world_xform fails 2 and 3 (identity rotation: the monster lies tilted in the water).
"""
import math
import re
import sys
import time

from twoinst import Checks, shot, start

MONSTER = '8A71529C'


def rot(v, axis, ang):
    ax = [a / math.hypot(*axis) for a in axis]
    c, s = math.cos(ang), math.sin(ang)
    d = sum(a * b for a, b in zip(ax, v))
    cr = (ax[1] * v[2] - ax[2] * v[1], ax[2] * v[0] - ax[0] * v[2], ax[0] * v[1] - ax[1] * v[0])
    return [v[i] * c + cr[i] * s + ax[i] * d * (1 - c) for i in range(3)]


def deep_point(host):
    a = list(map(float, re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.avatar')['out']).groups()))
    for axis in ([0, 1, 0], [1, 0, 0], [0, 0, 1], [1, 1, 0], [1, 0, 1], [0, 1, 1]):
        for deg in range(10, 120, 5):
            p = rot(a, axis, math.radians(deg))
            g = float(re.search(r'ground ([\d.]+)', host.call('coop.abilities ground %.1f %.1f %.1f' % tuple(p))['out']).group(1))
            if 0 < g < 488:
                return p
    return None


def watch(con):
    out = con.call('coop.effects watch')['out']
    res = {}
    for m in re.finditer(r'^(own|rx) fx=\S+ age=([\d.]+) active=(\d) flags=(\S+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) '
                         r'scale=([-\d.]+) rot=\(([^)]*)\)', out, re.M):
        rot = [float(x) for x in m.group(9).replace('|', ' ').split()]
        res[m.group(1)] = {'active': m.group(3) == '1', 'flags': m.group(4), 'pos': tuple(float(m.group(i)) for i in (5, 6, 7)),
                           'scale': float(m.group(8)), 'rot': rot}
    return res, out


def puppet_pos(con):
    m = re.search(r"remote 0 '[^']*'[^\n]*\n\s+puppet pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)", con.call('coop.status')['out'])
    return tuple(map(float, m.groups())) if m else None


def view_angle(con, p):
    """degrees between the camera's centre ray and the point p (coop.cam: App::cViewer::GetCameraToPoint)"""
    W, H = 1440, 1080
    m = re.search(r'viewport x=\d+ y=\d+ w=(\d+) h=(\d+)', con.call('coop.cam')['out'])
    if m:
        W, H = int(m.group(1)), int(m.group(2))
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) dir=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)',
                  con.call(f'coop.cam {W // 2} {H // 2}')['out'])
    if not m:
        return 180.0, W, H
    c = [float(m.group(i)) for i in (1, 2, 3)]
    d = [float(m.group(i)) for i in (4, 5, 6)]
    v = [p[i] - c[i] for i in range(3)]
    nv, nd = math.sqrt(sum(x * x for x in v)) or 1, math.sqrt(sum(x * x for x in d)) or 1
    return math.degrees(math.acos(max(-1, min(1, sum(a * b for a, b in zip(v, d)) / nv / nd)))), W, H


def aim(con, p):
    """the guest's camera turned (right-drags, as a player does) until the point is near the centre of its view (for
    the screenshots only)"""
    best, W, H = view_angle(con, p)
    for _ in range(16):
        if best < 12:
            break
        con.call(f'drag {W // 2} {H // 2} {W // 2 + 120} {H // 2} right')
        time.sleep(0.4)
        best, W, H = view_angle(con, p)
    print(f'  guest camera {best:.0f} degrees off the spot', flush=True)


def up_cos(e):
    """the largest |cos| between a rotation axis and the planet's radial at the effect"""
    p = e['pos']
    r = math.sqrt(sum(x * x for x in p)) or 1.0
    u = [x / r for x in p]
    R = e['rot']
    best = 0.0
    for c in range(3):  # its rows (the game's 3x3 is row-major: the host's monster has its up in the third row)
        row = R[3 * c:3 * c + 3]
        n = math.sqrt(sum(x * x for x in row)) or 1.0
        best = max(best, abs(sum(a * b for a, b in zip(row, u))) / n)
    return best


def main():
    host, guest = start(fixture='creature')
    ck = Checks()
    time.sleep(3)
    for c in (host, guest):
        c.call(f'coop.effects watch {MONSTER}')
    deep = deep_point(host)
    print('  deep water at', deep, flush=True)
    if not deep:
        ck(False, 'deep water near the host')
        return ck.result()
    r = math.hypot(*deep)
    s = [x * 499.4 / r for x in deep]
    # the guest on dry ground facing the spot, for the screenshot (not asserted): the dry ground nearest to the spot on
    # the great circle back to where the host stood (`coop.nest look <spot> 0 <from>`: there, facing the spot)
    a0 = list(map(float, re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', guest.call('coop.avatar')['out']).groups()))
    for k in range(1, 40):
        f = k / 40.0
        q = [s[i] * (1 - f) + a0[i] * f for i in range(3)]
        g = float(re.search(r'ground ([\d.]+)', host.call('coop.abilities ground %.1f %.1f %.1f' % tuple(q))['out']).group(1))
        if g > 500.0:
            qn = math.sqrt(sum(x * x for x in q))
            q = [x * (g + 1.0) / qn for x in q]
            print(f'  guest: on the shore {math.dist(q, s):.0f} m from the spot: ' + guest.call(
                'coop.nest look %.2f %.2f %.2f 0 %.2f %.2f %.2f' % (*s, *q))['out'].strip(), flush=True)
            break
    time.sleep(1.5)
    aim(guest, s)
    got, pup, t0 = {}, None, time.time()
    put_at = 0
    while time.time() - t0 < 25:
        if time.time() - put_at > 1.5 and time.time() - t0 < 9:
            host.call('coop.abilities put %.2f %.2f %.2f' % tuple(s))
            put_at = time.time()
        time.sleep(0.25)
        g, _ = watch(guest)
        if 'rx' in g:
            h, _ = watch(host)
            got = {'own': h.get('own'), 'rx': g['rx']}
            break
        p = puppet_pos(guest)  # the host's creature as the guest's game draws it, before the monster came
        if p and math.dist(p, s) < 6:
            pup = p
    shot(host, 'sea_monster_host')
    shot(guest, 'sea_monster_guest')
    for k in (1, 2, 3):
        time.sleep(0.7)
        shot(guest, f'sea_monster_guest_{k}')
    h, hout = watch(host)
    g, gout = watch(guest)
    print('  host: ' + hout.strip().replace('\n', ' | '), flush=True)
    print('  guest: ' + gout.strip().replace('\n', ' | '), flush=True)
    own, rx = got.get('own'), got.get('rx')
    ck(own is not None, 'host (control): its game made the sea monster effect (the death cinematic in deep water)')
    if not own:
        return ck.result()
    d = math.dist(own['pos'], rx['pos']) if rx else None
    ck(rx is not None and d is not None and d <= 1.0,
       f'the guest\'s game plays the sea monster where the host\'s took its creature ({d and round(d, 2)} m)')
    dp = math.dist(pup, rx['pos']) if rx and pup else None
    ck(dp is not None and dp <= 3.0, f'... at the host\'s creature as the guest\'s game drew it in the water ({dp and round(dp, 2)} m)')
    if rx:
        dr = max(abs(a - b) for a, b in zip(own['rot'], rx['rot']))
        ds = abs(own['scale'] - rx['scale']) / max(own['scale'], 1e-6)
        print(f'  host up {up_cos(own):.3f} scale {own["scale"]:.3f}; guest up {up_cos(rx):.3f} scale {rx["scale"]:.3f}; '
              f'rotation differs by {dr:.3f}', flush=True)
        ck(dr <= 0.05 and ds <= 0.05, f'the guest\'s monster has the host\'s orientation and size (rotation {dr:.3f}, '
           f'scale {ds * 100:.1f} %)')
        ck(up_cos(rx) >= 0.95, f'the guest\'s monster stands upright on the planet ({up_cos(rx):.3f}; host {up_cos(own):.3f})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
