#!/usr/bin/env python3
"""BUG-0866: wider third-person FOV in Creature and Galactic Adventures (Ctrl+Scroll stepping 0..6, +3 deg/step,
coop.fov console command, App::cViewer::BeginUpdate/EndUpdate lens widening, and GetCameraToPoint picking ray
recomputation).

1. Creature stage: `coop.fov` reports hooked=1, step=0, follow camera 0xB03AF999; reads stock viewWindow.
2. Ctrl+Scroll down 3 notches (`key ctrl down`, `wheel cx cy -3`, `key ctrl up`): `coop.fov` advances to step=3
   (+9 deg); during draw Camera+0x5C/+0x60 (`draw=`) is widened by +9 deg vertically while preserving aspect ratio,
   and outside draw (`stock=`) remains at the stock lens.
   Twin (`SPORE_FIX_OFF=camera.fov`): Ctrl+Scroll does not step and draw lens is not widened -> FAIL.
3. Picking ray verification at step 6 (+18 deg): reads the live camera-to-world matrix, viewOffset, viewWindow,
   and viewport from `coop.cam`; projects an off-center point using the wide lens and queries `coop.fov ray <x> <y>`
   (live hooked App::cViewer::GetCameraToPoint). Verifies angular error < 0.25 deg.
   Twin (`SPORE_FIX_OFF=camera.fov_pick`): GetCameraToPoint uses the stock lens instead of the wide lens, so the
   ray misses the off-center target by several degrees -> FAIL.
4. `coop.fov reset` restores step=0; setting step=4 and leaving Creature to the galaxy menu resets step to 0.

  SPORE_LANE=2 python3 tools/tests/coop_fov.py
"""
import math
import re
import time

from harness import mode, waiter
from twoinst import Checks, start

wait = waiter(step=0.5)


def parse_fov(txt):
    d = {}
    for k in ('hooked', 'step', 'active', 'fix', 'pick_fix', 'applied', 'picked', 'wheel'):
        m = re.search(rf'\b{k}=(-?\d+)', txt)
        if m:
            d[k] = int(m.group(1))
    for k in ('mode', 'cam'):
        m = re.search(rf'\b{k}=0x([0-9A-Fa-f]+)', txt)
        if m:
            d[k] = int(m.group(1), 16)
    for k in ('stock', 'draw'):
        m = re.search(rf'\b{k}=\(([-.\d]+)\s+([-.\d]+)\)', txt)
        if m:
            d[k] = (float(m.group(1)), float(m.group(2)))
    m = re.search(r'\bvp=\((\d+)\s+(\d+)\s+(\d+)\s+(\d+)\)', txt)
    if m:
        d['vp'] = tuple(int(m.group(i)) for i in range(1, 5))
    return d


def parse_cam(txt):
    rows = []
    for r in range(4):
        m = re.search(rf'view\[{r}\]\s*=\s*([-.\d]+)\s+([-.\d]+)\s+([-.\d]+)\s+([-.\d]+)', txt)
        if m:
            rows.append([float(m.group(i)) for i in range(1, 5)])
    moff = re.search(r'viewOffset=\(([-.\d]+)\s+([-.\d]+)\)', txt)
    mvw = re.search(r'viewWindow=\(([-.\d]+)\s+([-.\d]+)\)', txt)
    mvp = re.search(r'viewport x=(\d+) y=(\d+) w=(\d+) h=(\d+)', txt)
    return {
        'rows': rows,
        'off': (float(moff.group(1)), float(moff.group(2))) if moff else (0.0, 0.0),
        'vw': (float(mvw.group(1)), float(mvw.group(2))) if mvw else None,
        'vp': tuple(int(mvp.group(i)) for i in range(1, 5)) if mvp else None,
    }


def parse_ray(txt):
    m = re.search(
        r'ray ok=(\d+).*origin=\(([-.\d]+)\s+([-.\d]+)\s+([-.\d]+)\)\s+dir=\(([-.\d]+)\s+([-.\d]+)\s+([-.\d]+)\)',
        txt)
    if not m:
        return None
    return (int(m.group(1)),
            (float(m.group(2)), float(m.group(3)), float(m.group(4))),
            (float(m.group(5)), float(m.group(6)), float(m.group(7))))


def widen_vw(stock, step):
    base_deg = 2.0 * math.degrees(math.atan(stock[1]))
    wide_deg = min(165.0, base_deg + step * 3.0)
    wy = math.tan(math.radians(wide_deg * 0.5))
    wx = wy * (stock[0] / stock[1])
    return (wx, wy)


def expected_ray_dir(rows, vw, off, vp, sx, sy):
    nx = 2.0 * (sx - vp[0]) / vp[2] - 1.0
    ny = 1.0 - 2.0 * (sy - vp[1]) / vp[3]
    rx = nx * vw[0] - off[0]
    ry = ny * vw[1] - off[1]
    dx = rows[0][0] * rx + rows[1][0] + rows[2][0] * ry
    dy = rows[0][1] * rx + rows[1][1] + rows[2][1] * ry
    dz = rows[0][2] * rx + rows[1][2] + rows[2][2] * ry
    ln = math.sqrt(dx * dx + dy * dy + dz * dz)
    return (dx / ln, dy / ln, dz / ln)


def angle_deg(a, b):
    dot = max(-1.0, min(1.0, a[0] * b[0] + a[1] * b[1] + a[2] * b[2]))
    return math.degrees(math.acos(dot))


def main():
    host, guest = start('creature')
    ck = Checks()
    try:
        # 1. Baseline in Creature stage
        st0 = host.call('coop.fov')['out'].strip()
        print('  fov baseline:', st0, flush=True)
        f0 = parse_fov(st0)
        ck(f0.get('hooked') == 1 and f0.get('step') == 0, f'fov hooks active at step 0 ({st0})')
        ck(f0.get('cam') == 0xB03AF999, f'Creature follow camera 0xB03AF999 active ({st0})')
        stock0 = f0.get('stock')
        vp = f0.get('vp')
        ck(stock0 is not None and stock0[0] > 0.1 and stock0[1] > 0.1 and vp and vp[2] > 0 and vp[3] > 0,
           f'stock viewWindow and viewport readable ({st0})')
        if not (stock0 and vp and vp[2] > 0 and vp[3] > 0):
            return ck.result()

        # 2. Ctrl+Scroll down 3 notches -> step 3 (+9 deg)
        cx, cy = vp[0] + vp[2] // 2, vp[1] + vp[3] // 2
        host.call('key ctrl down')
        host.call(f'wheel {cx} {cy} -3')
        host.call('wait.frames 20 10000')
        host.call('key ctrl up')
        host.call('wait.frames 15 10000')

        st1 = host.call('coop.fov')['out'].strip()
        print('  fov after Ctrl+Scroll -3:', st1, flush=True)
        f1 = parse_fov(st1)
        ck(f1.get('step') == 3, f'Ctrl+Scroll -3 advanced FOV to step 3 ({st1})')
        draw1 = f1.get('draw')
        stock1 = f1.get('stock')
        exp3 = widen_vw(stock0, 3)
        ck(draw1 is not None and abs(draw1[1] - exp3[1]) < 0.015 and draw1[1] > stock0[1] + 0.05,
           f'drawn viewWindow widened +9 deg (draw={draw1}, expected~{exp3}, stock={stock0})')
        ck(stock1 is not None and abs(stock1[1] - stock0[1]) < 0.01,
           f'stock viewWindow restored outside draw (stock={stock1}, baseline={stock0})')

        # 3. Picking ray accuracy at step 6 (+18 deg) for an off-center screen point
        host.call('coop.fov 6')
        host.call('wait.frames 15 10000')
        st6 = host.call('coop.fov')['out'].strip()
        print('  fov step 6:', st6, flush=True)
        f6 = parse_fov(st6)
        ck(f6.get('step') == 6 and f6.get('draw') is not None and f6['draw'][1] > stock0[1] + 0.12,
           f'coop.fov 6 widened draw lens +18 deg ({st6})')

        cam_txt = host.call('coop.cam')['out']
        cam = parse_cam(cam_txt)
        ck(len(cam['rows']) == 4 and cam['vw'] is not None and cam['vp'] is not None,
           'coop.cam returned live camera matrix and viewport')
        if len(cam['rows']) == 4 and cam['vw'] and cam['vp']:
            sx = cam['vp'][0] + cam['vp'][2] * 0.15
            sy = cam['vp'][1] + cam['vp'][3] * 0.25
            ray_txt = host.call(f'coop.fov ray {sx:.1f} {sy:.1f}')['out'].strip()
            print('  fov pick ray:', ray_txt, flush=True)
            ray = parse_ray(ray_txt)
            ck(ray is not None and ray[0] == 1, f'GetCameraToPoint returned a valid ray ({ray_txt})')
            if ray and ray[0] == 1:
                wide6 = widen_vw(cam['vw'], 6)
                exp_wide = expected_ray_dir(cam['rows'], wide6, cam['off'], cam['vp'], sx, sy)
                exp_stock = expected_ray_dir(cam['rows'], cam['vw'], cam['off'], cam['vp'], sx, sy)
                err_wide = angle_deg(ray[2], exp_wide)
                err_stock = angle_deg(ray[2], exp_stock)
                print(f'  ray error vs wide={err_wide:.3f} deg, vs stock={err_stock:.3f} deg', flush=True)
                ck(err_wide < 0.25,
                   f'GetCameraToPoint ray matches wide lens (err_wide={err_wide:.3f} deg, err_stock={err_stock:.3f} deg)')

        # 4. Reset command + automatic reset when leaving Creature stage
        host.call('coop.fov reset')
        st_r = host.call('coop.fov')['out'].strip()
        ck(parse_fov(st_r).get('step') == 0, f'coop.fov reset restored step=0 ({st_r})')

        host.call('coop.fov 4')
        guest.call('coop.leave')
        time.sleep(1.5)
        host.call('coop.menu')
        left = wait(lambda: 'GalaxyGameEntry' in mode(host), 60)
        ck(left, f'host left Creature to galaxy menu ({mode(host).strip()})')
        if left:
            host.call('wait.frames 15 10000')
            st_m = host.call('coop.fov')['out'].strip()
            print('  fov in menu:', st_m, flush=True)
            ck(parse_fov(st_m).get('step') == 0, f'leaving Creature reset FOV step to 0 ({st_m})')
    finally:
        try:
            host.call('key ctrl up')
            host.call('coop.fov reset')
        except OSError:
            pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
