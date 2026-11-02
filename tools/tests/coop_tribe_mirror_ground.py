#!/usr/bin/env python3
"""Tribe: the guest's mirrors of its members stay on the guest's ground while the guest's game does not
update its citizens (BUG-0444).

After a cold join the guest's game time stays paused for 10-40 s (cGameTimeManager +0x48 = 3, no
BehaviorTreeTick): only the mod moves the mirrors. They kept their own distance from the planet's centre
(own_height, BUG-0329), so a member the host walked up a slope in that time was placed on the host's path
at its old height, under the guest's ground (tribe_coop: 2.6-3.3 m for ~20 s during the hunt run;
a 60 m walk up a 7-9 m rise: 9 m under).

1. Two games on the tribe fixture. The guest's game time paused: as left by the cold join, else paused
   here (the same flag, `mem.write`), and held paused until the end (then resumed).
2. The steepest rise within 60 m of the guest's own member on the guest's ground (16 directions, 30-60 m);
   the guest orders its member there (`coop.tribe walk`, executed by the host), then back.
   Every ~0.3 s: the mirror's height over the guest's ground (`coop.ground` at its position).
3. Then the host adds 2 members while the guest's copy of the host's positions is 2 m lower
   (`coop.tribe lift -2`: as if the guest's ground were 2 m higher, BUG-0329/BUG-0332): the new mirrors'
   heights over the guest's ground.
PASS: the walked mirror never under -0.5 m nor over 1 m (and on the host's path at the end, under 3 m off), the new
mirrors not under -0.5 m. `SPORE_FIX_OFF=tribe.mirror_ground` must FAIL.

  SPORE_LANE=10 python3 tools/tests/coop_tribe_mirror_ground.py [--rise 60]
"""
import argparse
import math
import re
import time

from twoinst import Checks, start
from coop_tribe import members, dist

TIME_MGR = 0x167EB04  # GOG 2024: cGameTimeManager::Get = mov eax,[0x167EB04] (overrides_dc04aee5.sym)


def norm(p):
    return math.sqrt(sum(x * x for x in p))


def agl(con, p):
    """height of p over this game's ground (coop.ground), None without a planet"""
    m = re.search(r'ground ([-\d.]+) point ([-\d.]+) above ([-\d.]+)', con.call('coop.ground %.2f %.2f %.2f' % tuple(p))['out'])
    return (float(m.group(1)), float(m.group(3))) if m else (None, None)


def bt_ticks(con):
    m = re.search(r'ai_skipped=\d+/(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def time_flags(con):
    out = con.call(f'mem.ptr 0x{TIME_MGR:X} 0')['out']
    a = re.findall(r'0x([0-9A-Fa-f]+)', out)
    if len(a) < 2:
        return None, None
    tm = int(a[-1], 16)
    r = re.findall(r'\b([0-9A-Fa-f]{2})\b', con.call(f'mem.read 0x{tm + 0x48:X} 1')['out'].split(' ', 1)[-1])
    return tm, (int(r[0], 16) if r else None)


def steepest(con, p0, reach):
    """the point within `reach` m of p0 (on the tangent plane) whose ground rises most"""
    r0 = norm(p0)
    up = [x / r0 for x in p0]
    ref = (0, 1, 0) if abs(up[1]) < 0.9 else (1, 0, 0)
    e1 = [up[1] * ref[2] - up[2] * ref[1], up[2] * ref[0] - up[0] * ref[2], up[0] * ref[1] - up[1] * ref[0]]
    n1 = norm(e1)
    e1 = [x / n1 for x in e1]
    e2 = [up[1] * e1[2] - up[2] * e1[1], up[2] * e1[0] - up[0] * e1[2], up[0] * e1[1] - up[1] * e1[0]]
    g0 = agl(con, p0)[0]
    best = None
    for k in range(16):
        a = k * math.pi / 8
        for d in (reach / 2, reach * 3 / 4, reach):
            q = tuple(p0[i] + d * (math.cos(a) * e1[i] + math.sin(a) * e2[i]) for i in range(3))
            g = agl(con, q)[0]
            if g is not None and g0 is not None and (best is None or g - g0 > best[0]):
                best = (g - g0, d, q)
    return best


def hold_pause(con, tm):
    """keep the guest's game time paused (a cold join's pause ends by itself 10-40 s after the join)"""
    if tm:
        con.call(f'mem.write 0x{tm + 0x48:X} 03')


def walk(host, guest, me, dest, secs, rows, tm):
    """the guest orders its member to dest; samples (t, guest agl, position) for secs"""
    guest.call(f'coop.tribe select {me} 1')
    print('  ', guest.call('coop.tribe walk %.2f %.2f %.2f x' % dest)['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < secs:
        hold_pause(guest, tm)
        gm = members(guest)
        if me in gm:
            p = gm[me][2]
            rows.append((time.time() - t0, agl(guest, p)[1], p))
        time.sleep(0.3)
    hm, gm = members(host), members(guest)
    return dist(hm[me][2], gm[me][2]) if me in hm and me in gm else 99


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rise', type=float, default=60, help='how far to look for the slope (m)')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    t0 = time.time()
    while True:
        hm, gm = members(host), members(guest)
        if (hm and set(hm) == set(gm)) or time.time() - t0 > 60:
            break
        time.sleep(1)
    mine = [i for i, v in gm.items() if v[0] == 1 and v[3] == 'player']
    ck(mine, f'the guest owns members of the player tribe ({mine})')
    if not mine:
        return ck.result()
    me = mine[0]
    p0 = gm[me][2]
    best = steepest(guest, p0, a.rise)
    ck(best and best[0] > 3, f'a slope near the guest\'s member: the ground rises {best[0] if best else 0:.1f} m in {best[1] if best else 0:.0f} m')
    if not best:
        return ck.result()

    tm, flags = time_flags(guest)
    forced = False
    if tm is None or flags is None:
        ck(False, 'the guest\'s game time manager is readable')
        return ck.result()
    if not flags & 1:  # warm games: the state a cold join leaves for 10-40 s
        forced = True
    hold_pause(guest, tm)  # (a cold join's own pause would end during the walks)
    b0 = bt_ticks(guest)
    time.sleep(1.0)
    still = bt_ticks(guest) - b0
    print(f'  guest game time paused ({"paused here" if forced else "after the cold join"}; '
          f'citizen AI ticks in 1 s: {still})', flush=True)
    rows = []
    try:
        off_up = walk(host, guest, me, best[2], 12, rows, tm)
        off_back = walk(host, guest, me, p0, 12, rows, tm)
        # new mirrors where the host's positions lie under this game's ground
        guest.call('coop.tribe lift -2')
        before = set(members(guest))
        print('  ', host.call('coop.tribe spawn 2')['out'].strip()[:200], flush=True)
        new_agl = []
        t1 = time.time()
        while time.time() - t1 < 6:
            hold_pause(guest, tm)
            gm = members(guest)
            for i in set(gm) - before:
                h = agl(guest, gm[i][2])[1]
                if h is not None:
                    new_agl.append((i, h))
            time.sleep(0.5)
    finally:
        guest.call('coop.tribe lift 0')
        # resumed: as before when paused here; after a cold join the game had resumed by itself by now
        guest.call(f'mem.write 0x{tm + 0x48:X} {flags & ~3 & 0xFF:02X}')
    hs = [h for _, h, _ in rows if h is not None]
    lo = min(hs) if hs else 0
    under = sum(h < -0.5 for h in hs)
    print(f'  walked mirror: {len(hs)} samples, lowest {lo:.2f} m, highest {max(hs) if hs else 0:.2f} m over the '
          f'guest\'s ground; {under} under -0.5 m; off the host\'s member after the walks {off_up:.1f} / {off_back:.1f} m'
          , flush=True)
    for t, h, p in rows[::4]:
        print(f'    {t:5.1f} s agl={h if h is None else round(h, 2)} at ({p[0]:.1f} {p[1]:.1f} {p[2]:.1f})', flush=True)
    ck(still == 0, f'the guest\'s game did not update its citizens while the member walked ({still} AI ticks in 1 s)')
    ck(len(hs) >= 20, f'the walked mirror was sampled ({len(hs)})')
    ck(under == 0, f'the guest\'s walked member stays on its ground (lowest {lo:.2f} m, {under} of {len(hs)} samples under -0.5 m)')
    hi = max(hs) if hs else 0
    ck(hi <= 1.0, f'... and does not float over it walking down (highest {hi:.2f} m)')
    ck(off_back < 3, f'the guest\'s member is where the host has it after the walks ({off_back:.1f} m)')
    lows = {}
    for i, h in new_agl:
        lows[i] = min(h, lows.get(i, h))
    print(f'  new mirrors (host positions 2 m under the guest\'s ground): lowest over the ground {lows}', flush=True)
    ck(lows, f'the host\'s new members are mirrored ({len(lows)})')
    ck(lows and min(lows.values()) >= -0.5, f'the new mirrors are made on the guest\'s ground (lowest {min(lows.values()) if lows else 0:.2f} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
