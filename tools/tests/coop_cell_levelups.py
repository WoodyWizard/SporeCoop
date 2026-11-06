#!/usr/bin/env python3
"""BUG-0307: many Cell level changes on two games while both cells swim among the cells: does the own cell
of either game stop being finite (the guest's level-up NaN of lab 20261105-142923, mask 0x104)?

Two games in the Cell fixture. The host adds food (`coop.pool food +50`: the shared total, both games level
up) and takes it back (`-50`: both level down) <cycles> times; between the changes both cells swim to a
random place nearby (`coop.pool goto`), so they keep touching cells. Each game's own cell state is the
game's: `own_nf=` counts the times a probe of the own cell (cell step start, around its collision pairs,
the contact solver, the soft-body step, our tick) saw a field of it not finite. `contacts_bad=` counts the
collision contacts whose impulse was not finite (ContactSolve 0xE58C90, docs/re/cell.md#cell-contact-nan).
Checks: 1. both games go through the level changes (the world scale moves); 2. neither own cell goes
non-finite (own_nf stays 0 on both).

  SPORE_LANE=N python3 tools/tests/coop_cell_levelups.py [--cycles 20] [--wait 4.5]
"""
import argparse
import random
import re
import time

from twoinst import Checks, start


def stats(con):
    out = con.call('coop.pool')['out']

    def num(key):
        m = re.search(r'\b' + key + r'=(-?[\d.]+)', out)
        return float(m.group(1)) if m else None
    return {k: num(k) for k in ('own_nf', 'contacts_bad', 'contacts_bad_own', 'contact_solves', 'nan_heals',
                                'world_scale', 'food', 'mirrors')}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cycles', type=int, default=20)
    ap.add_argument('--wait', type=float, default=4.5)
    a = ap.parse_args()
    ck = Checks()
    host, guest = start('cell')
    time.sleep(5)
    s0 = {'host': stats(host), 'guest': stats(guest)}
    print(f'  start: host {s0["host"]} | guest {s0["guest"]}', flush=True)
    rnd = random.Random(307)
    scales = {'host': set(), 'guest': set()}
    for i in range(a.cycles):
        for amount in (50, -50):
            host.call(f'coop.pool food {amount}')
            for _ in range(int(a.wait / 0.75)):
                for con in (host, guest):
                    con.call(f'coop.pool goto {rnd.uniform(-6, 6):.1f} {rnd.uniform(-6, 6):.1f}')
                time.sleep(0.75)
            for who, con in (('host', host), ('guest', guest)):
                s = stats(con)
                if s['world_scale'] is not None:
                    scales[who].add(round(s['world_scale'], 2))
        sh, sg = stats(host), stats(guest)
        print(f'  cycle {i + 1}: host own_nf={sh["own_nf"]} bad={sh["contacts_bad"]}/{sh["contact_solves"]} '
              f'scale={sh["world_scale"]} | guest own_nf={sg["own_nf"]} bad={sg["contacts_bad"]} '
              f'own={sg["contacts_bad_own"]} heals={sg["nan_heals"]} scale={sg["world_scale"]} mirrors={sg["mirrors"]}',
              flush=True)
    end = {'host': stats(host), 'guest': stats(guest)}
    for who in ('host', 'guest'):
        print(f'  {who}: {end[who]} scales seen {sorted(scales[who])}', flush=True)
        ck(len(scales[who]) >= 2, f'{who}: its world went through the level changes (scales {sorted(scales[who])})')
        nf = (end[who]['own_nf'] or 0) - (s0[who]['own_nf'] or 0)
        bad = (end[who]['contacts_bad'] or 0) - (s0[who]['contacts_bad'] or 0)
        ck(nf == 0, f'{who}: its own cell stayed finite through {2 * a.cycles} level changes (own_nf +{nf:.0f}, '
                    f'non-finite contacts {bad:.0f})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
