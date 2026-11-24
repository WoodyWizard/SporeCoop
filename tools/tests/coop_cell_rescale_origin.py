#!/usr/bin/env python3
"""BUG-1394: Cell stage >1000 m level-change world recentering.

Cell_RescaleAbout (0x00E6DB70, docs/re/cell.md#cell-rescale-centre) shifts the whole world by a
snapped recentering vector C whenever the local camera point cCellUI+0x48 has |p|^2 > 1e6 (1000 m
from origin). In co-op, if one peer swims >1000 m from origin while its partner stays near (0,0),
a level-change rescale recentered only the far peer, shifting its coordinate origin by ~1000 m
relative to the partner.

With cell.rescale_origin ON, h_WorldScale holds cCellUI+0x48 at (0,0,0) across WorldScaleStep for
connected peers whenever |cam|^2 > 1e6, keeping all peers in the shared origin frame and restoring
cCellUI+0x48 scaled by old_scale / new_scale.

Two games in the Cell fixture:
1. Move host to +1250 m from the guest (`coop.pool tp 1250 0`), keeping the guest near origin.
2. Record host and guest world positions and scale (`coop.pool cam`), then advance food level
   (`coop.pool food 120` on host) and wait until both games finish rescaling to the higher scale.
3. Verify:
   - host recorded `origin_holds > 0`,
   - host's world-space invariant `pos * scale` stayed near its pre-rescale coordinate (did not
     jump by ~1000 m toward origin),
   - host and guest separation in `pos * scale` space stayed > 800 m (did not collapse to ~0 m),
   - host camera stays centred on its own cell (`off < 1.5 m`).
   With `SPORE_FIX_OFF=cell.rescale_origin`, host recenters to near (0, 0), breaking the invariant
   and collapsing the coordinate frame: must FAIL."""
import math
import re
import time

from harness import cell_pos as own_pos, waiter
from twoinst import Checks, start

wait = waiter(step=0.5)


def playing(con):
    m = re.search(r'input_lock=(-?\d+)', con.call('coop.uistate')['out'])
    return m and int(m.group(1)) == 0 and 'cine=0' in con.call('coop.pool')['out']


def cam_info(con):
    out = con.call('coop.pool cam')['out'].strip()
    scale = re.search(r'scale=([\d.]+)', out)
    off = re.search(r'off=([\d.]+)', out)
    holds = re.search(r'origin_holds=(\d+)', out)
    return {
        'scale': float(scale.group(1)) if scale else None,
        'off': float(off.group(1)) if off else None,
        'holds': int(holds.group(1)) if holds else 0,
        'raw': out,
    }


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: own_pos(host) and own_pos(guest), 20), 'both games have their own cell')
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (hatch finished)')
    time.sleep(1.0)

    # 1. Move host >1000 m from origin while guest stays near origin
    r = host.call('coop.pool tp 1250 0')['out'].strip()
    print(f'  host tp 1250 0 -> {r}', flush=True)
    time.sleep(1.0)
    hp0 = own_pos(host)
    gp0 = own_pos(guest)
    hc0 = cam_info(host)
    gc0 = cam_info(guest)
    print(f'  before: host={hp0} {hc0["raw"]}', flush=True)
    print(f'  before: guest={gp0} {gc0["raw"]}', flush=True)
    r_host0 = math.hypot(hp0[0], hp0[1]) if hp0 else 0.0
    r_guest0 = math.hypot(gp0[0], gp0[1]) if gp0 else 0.0
    ck(r_host0 > 1050.0, f'host is >1050 m from origin before rescale ({r_host0:.1f} m)')
    ck(r_guest0 < 400.0, f'guest is near origin before rescale ({r_guest0:.1f} m)')
    s0 = hc0['scale'] or 1.0

    # 2. Trigger level change rescale on host (shared food advances guest too)
    fr = host.call('coop.pool food 120')['out'].strip()
    print(f'  host food 120 -> {fr}', flush=True)
    # Wait for both peers to advance scale and settle
    prev = [None, None]
    stable = [0]

    def settled():
        hc = cam_info(host)
        gc = cam_info(guest)
        hs, gs = hc['scale'], gc['scale']
        if hs is None or gs is None:
            return False
        if hs <= s0 + 0.05 or gs <= s0 + 0.05:
            return False
        if abs(hs - gs) > 0.02:
            return False
        if prev[0] is not None and abs(hs - prev[0]) < 0.002 and abs(gs - prev[1]) < 0.002:
            stable[0] += 1
        else:
            stable[0] = 0
        prev[0], prev[1] = hs, gs
        return stable[0] >= 3

    ck(wait(settled, 25, 0.5), 'both games finished rescaling to the higher level')
    time.sleep(0.5)

    hp1 = own_pos(host)
    gp1 = own_pos(guest)
    hc1 = cam_info(host)
    gc1 = cam_info(guest)
    print(f'  after:  host={hp1} {hc1["raw"]}', flush=True)
    print(f'  after:  guest={gp1} {gc1["raw"]}', flush=True)

    s1 = hc1['scale'] or s0
    # Invariant: without recentering shift C, world coordinate (pos * scale) changes only by normal swimming (~< 100 m)
    wx0, wy0 = hp0[0] * s0, hp0[1] * s0
    wx1, wy1 = hp1[0] * s1, hp1[1] * s1
    w_drift = math.hypot(wx1 - wx0, wy1 - wy0)
    sep0 = math.hypot(hp0[0] - gp0[0], hp0[1] - gp0[1]) * s0
    sep1 = math.hypot(hp1[0] - gp1[0], hp1[1] - gp1[1]) * s1
    print(f'  scale {s0:.3f} -> {s1:.3f}; host pos*scale drift={w_drift:.1f} m; sep*scale {sep0:.1f} -> {sep1:.1f} m', flush=True)

    ck(hc1['holds'] > 0, f'host held rescale origin at (0,0,0) (origin_holds={hc1["holds"]})')
    ck(w_drift < 150.0, f'host world coordinate pos*scale did not shift by recentering (drift={w_drift:.1f} m)')
    ck(abs(sep1 - sep0) < 200.0 and sep1 > 400.0,
       f'host/guest coordinate frames stayed aligned (sep*scale {sep0:.1f} -> {sep1:.1f} m)')
    ck(hc1['off'] is not None and hc1['off'] < 1.5,
       f'host camera stayed centred on its own cell after rescale (off={hc1["off"]} m)')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
