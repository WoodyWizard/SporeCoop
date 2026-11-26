#!/usr/bin/env python3
"""BUG-1568: Cell Rivals wscale divergence local-pool solo safety and 0x00E4E384 fluid viewbox overflow guard.

In 4-player campaign_rivals (out/lab/20261125-182926), when host Alice hit food=1000 (wscale surging 0.5 -> 4.722 ->
6793 -> 15000) while rival guest Bob stayed at wscale=0.5:
  1. BUG-1552 switched Bob to local pool (g_scale_solo=1), but modes::cell_drive kept scaling Alice's puppet by
     host_scale / own_scale (up to 999 m wide). Because Alice and Bob were at War, HandleCollisionPair collided Bob
     with Alice's giant puppet and launched Bob at 658 m/s across the map to (8187.9, 10249.0).
  2. At |cam.x| >= 8182.4 (or |cam.y| >= 4088.7), float32 rounding in ViewBoxes (0x016B3C88) made
     (cam.x + 10.0f) - (cam.x - 10.0f) = 20.000488 > 20.0, making 0x00E4E210 return 15 (0x0F) fluid grid cells into
     0x00E4E320's 9-int stack buffer and causing an 801+322 fault storm at 0x00E4E384 (data 0x00000112).

Scenario (Cell fixture, two players):
  1. Enable Cell Rivals, fast timers, and War (0-1=War). Place guest next to host.
  2. Advance host to food=1000 (`coop.pool food 1000`): host wscale surges while guest stays at wscale=0.5 and
     switches to local pool (`solo=1`).
  3. Hold 10 s while host wscale surges:
     - Guest stays near its start position (not launched away by an over-scaled War rival puppet),
       `puppets_clamped > 0`, and 0 faults on both games.
  4. Teleport guest to the exact campaign_rivals crash coordinates (8187.9, 10249.0) and hold 2.5 s:
     - Fix ON: `h_FluidStep` clamps ViewBoxes float32 overflow (`fluid_clamps > 0`), guest stays responsive with
       0 caught faults.
     - Fix OFF (`SPORE_FIX_OFF=cell.rivals_wscale_solo_safe`): `puppets_clamped == 0`, `fluid_clamps == 0`, and
       0x00E4E384 faults storm on guest: must FAIL.

  SPORE_LANE=5 python3 tools/tests/coop_cell_rivals_wscale_fault.py
"""
import re
import time

from harness import cell_pos, dist, faults, num, pool, wait_until
import twoinst
from twoinst import Checks, start


def rivals_line(con):
    return con.call('coop.pool rivals').get('out') or ''


def rivals_num(out, key):
    m = re.search(rf'\b{key}=(-?\d+)', out or '')
    return int(m.group(1)) if m else None


def main():
    twoinst.forget_settings()
    host, guest = start('cell')
    ck = Checks()
    try:
        ck(bool(wait_until(lambda: (num(pool(guest), 'mirrors') or 0) > 5, 30.0, 0.5, 'guest mirrors')),
           'guest mirrors host Cell pool')

        # 1. Enable Cell Rivals + fast timers + War 0-1
        host.call('coop.rivals preset all_together')
        host.call('coop.rivals mode cell rivals')
        host.call('coop.rivals fasttimers 1')
        host.call('coop.rivals war 1')

        ready = wait_until(
            lambda: rivals_num(rivals_line(host), 'active') == 1
            and rivals_num(rivals_line(guest), 'active') == 1
            and '0-1=War' in (guest.call('coop.rivals').get('out') or ''),
            12.0,
            0.25,
            'Cell Rivals + War',
        )
        ck(bool(ready), 'Cell Rivals active and 0-1=War synced')
        if not ready:
            return ck.result()

        # Place guest right next to host (reproducing close proximity when host surges to wscale=15000)
        guest.call('coop.pool swimto partner tp')
        time.sleep(1.0)
        p0 = cell_pos(guest)
        ck(p0 is not None, f'guest Cell start position readable ({p0})')
        if p0 is None:
            return ck.result()

        # 2. Trigger host wscale surge (exact campaign_rivals command: food 1000)
        host.call('coop.pool food 1000')
        solo_on = wait_until(
            lambda: num(pool(guest), 'solo') == 1,
            15.0,
            0.25,
            'guest wscale local pool (solo=1)',
        )
        ck(bool(solo_on), 'guest entered local pool (solo=1) when host wscale diverged')

        # 3. Monitor guest stability for 10 s while host wscale surges past 100..15000
        max_drift = 0.0
        t0 = time.time()
        while time.time() - t0 < 10.0:
            p = cell_pos(guest)
            if p is not None:
                d = dist(p, p0)
                if d > max_drift:
                    max_drift = d
            time.sleep(0.5)

        rl1 = rivals_line(guest)
        clamped = rivals_num(rl1, 'puppets_clamped') or 0
        skips = rivals_num(rl1, 'puppet_skips') or 0
        f_mid = faults(guest)
        print(f'  after wscale surge: max_drift={max_drift:.1f} m puppets_clamped={clamped} '
              f'puppet_skips={skips} guest_faults={f_mid}', flush=True)
        ck(max_drift < 150.0,
           f'guest stayed near start during War rival wscale surge (max_drift={max_drift:.1f} m < 150 m)')
        ck(clamped > 0,  # counter: confirms over-scaled host puppet was clamped during wscale surge (max_drift checked above)
           f'over-scaled rival puppet clamped on guest (puppets_clamped={clamped}, puppet_skips={skips})')

        # 4. Exercise exact campaign_rivals 0x00E4E384 overflow coordinates (8187.9, 10249.0)
        fluid_before = rivals_num(rl1, 'fluid_clamps') or 0
        tp_out = guest.call('coop.pool rivals tpabs 8187.9 10249.0').get('out') or ''
        print(f'  tpabs: {tp_out.strip()}', flush=True)
        time.sleep(2.5)

        rl2 = rivals_line(guest)
        fluid_clamps = rivals_num(rl2, 'fluid_clamps') or 0
        p_far = cell_pos(guest)
        f_end = faults(guest)
        print(f'  at (8187.9, 10249.0): pos={p_far} fluid_clamps={fluid_before}->{fluid_clamps} guest_faults={f_end}',
              flush=True)
        ck(p_far is not None, f'guest stays responsive at (8187.9, 10249.0) (pos={p_far})')
        ck(fluid_clamps > fluid_before,  # counter: confirms h_FluidStep clamped ViewBoxes float32 overflow (0 faults checked below)
           f'fluid grid ViewBoxes float32 overflow clamped (fluid_clamps={fluid_before}->{fluid_clamps})')
        ck.faults(host=host, guest=guest)
        return ck.result()
    finally:
        twoinst.forget_settings()


if __name__ == '__main__':
    raise SystemExit(main())
