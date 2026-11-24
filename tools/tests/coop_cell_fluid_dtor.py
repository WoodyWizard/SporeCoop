#!/usr/bin/env python3
"""Cell: lingering Swarm fluid-field callbacks after leaving Cell do not dereference the freed pool (BUG-1536).

A player report (0.7.16.1, Cell -> Creature transition) crashed the host at SporeApp.exe+0xA4E454 (VA 0x00E4E454)
inside the Cell fluid-field lookup called by Swarm's particle simulator through the static fluid-field object
(0x015A839C, vtable 0x014852B8). cCellGame's destructor (0x00E63F90) frees the fluid/particle pool without
clearing [0x016B3C04] or unregistering the static fluid field from Swarm.

1. Start on the Cell fixture: coop.cellfluid reports hooked=1 and alive=1, and `coop.cellfluid probe` runs
   vtable+0x18 (CellFluid_Contains) -> vtable+0x24 (CellFluid_Sample) cleanly (contains=1, res=1).
2. Leave Cell for the galaxy menu (`coop.menu`): cCellGame_dtor runs and marks alive=0.
3. Run `coop.cellfluid probe` from the galaxy menu (exercising the exact Swarm fluid-field call sequence after
   Cell shutdown):
   PASS: contains=0, vel=(0.000 0.000 0.000), res=1, guarded >= 2, no fault and game stays alive.
   SPORE_FIX_OFF=game.cell_fluid_dtor (cell_fluid_dtor_off): the unguarded call enters the freed Cell fluid
   field (contains=1 and/or access violation in 0x00E4E454): must FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_cell_fluid_dtor.py
"""
import re
import time

from harness import mode, waiter
from twoinst import Checks, start

wait = waiter(step=0.5)


def parse_fluid(out):
    d = {}
    for k in ('hooked', 'alive', 'fix', 'contains', 'res', 'guarded'):
        m = re.search(rf'\b{k}=(-?\d+)', out)
        if m:
            d[k] = int(m.group(1))
    m = re.search(r'vel=\(([^)]+)\)', out)
    if m:
        d['vel'] = m.group(1).strip()
    return d


def main():
    host, guest = start('cell')
    ck = Checks()

    # 1. Active Cell stage: hooks installed, fluid field alive, probe enters game code
    st0 = host.call('coop.cellfluid')['out'].strip()
    print('  in cell:', st0, flush=True)
    f0 = parse_fluid(st0)
    ck(f0.get('hooked') == 1 and f0.get('alive') == 1,
       f'Cell fluid-field hooks active in Cell ({st0})')

    p0_out = host.call('coop.cellfluid probe')['out'].strip()
    print('  probe in cell:', p0_out, flush=True)
    p0 = parse_fluid(p0_out)
    ck(p0.get('alive') == 1 and p0.get('contains') == 1 and p0.get('res') == 1,
       f'fluid-field probe runs live inside Cell ({p0_out})')

    # 2. Leave Cell to the galaxy menu (runs cCellGame_dtor)
    guest.call('coop.leave')
    time.sleep(2)
    print('  host menu:', host.call('coop.menu')['out'].strip(), flush=True)
    left = wait(lambda: 'GalaxyGameEntry' in mode(host), 60)
    ck(left, f'host left Cell to the galaxy menu ({mode(host).strip()})')
    if not left:
        return ck.result()

    st1 = host.call('coop.cellfluid')['out'].strip()
    print('  in menu:', st1, flush=True)
    f1 = parse_fluid(st1)
    ck(f1.get('alive') == 0, f'cCellGame_dtor marked the fluid field dead ({st1})')
    g_before = f1.get('guarded', 0)

    # 3. Probe the static fluid field after Cell shutdown
    try:
        p1_out = host.call('coop.cellfluid probe')['out'].strip()
    except (OSError, TimeoutError) as e:
        ck(False, f'fluid-field probe after Cell shutdown crashed or hung the game ({e})')
        return ck.result()

    print('  probe in menu:', p1_out, flush=True)
    p1 = parse_fluid(p1_out)
    ck(p1.get('contains') == 0 and p1.get('vel') == '0.000 0.000 0.000' and p1.get('res') == 1,
       f'fluid-field probe after Cell shutdown returned safe empty result ({p1_out})')
    ck(p1.get('guarded', 0) >= g_before + 2,  # counter: confirms both Contains and Sample hit the lifetime guard; contains/vel checked above
       f'lifetime guard intercepted both fluid-field callbacks ({g_before} -> {p1.get("guarded")})')

    # Confirm the host stays responsive in the galaxy menu
    time.sleep(2)
    m_after = mode(host).strip()
    ck('GalaxyGameEntry' in m_after, f'host stays alive in the galaxy menu after probe ({m_after})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
