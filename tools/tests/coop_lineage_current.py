#!/usr/bin/env python3
"""BUG-9285: Lineages cells are only the players' CURRENT cells, at the player species' level (no giants).

The owner's test of 2026-11-20 ("many giants in the Cell stage"): the host's eight My Creations cells took the look of
one in six spawns of every native species, the big species of the levels above too. Now only spawns of the player
species (6-8 at the avatar's level) take a partner's current cell.

What each game shows is read from the game: `coop.lineage` lists the live creature cells by the model key the game
draws them with (cell +0xFC) and, for each cell of a player model, its level (+0x358).
  1. the guest plays its own cell (another model than the host's); the host's candidates are exactly the guest's
     current cell (from=guest:<id>), none of the host's My Creations;
  2. while both swim about (90 s), the host's pool holds cells of the guest's current model; EVERY live cell of a
     player model on the host is of that model and of the host's own food level (the player species' level: never a
     bigger species), and the guest's game draws them from its own key of its own cell;
  3. no live creature cell of any other player model (the host's My Creations cells) on the host;
  4. no faults.
--old (`coop.fix cell.lineage_current off` on both, the twin `lineage_current_off`): the My Creations cells come back on
every species with `coop.lineage share 100`: step 1 or 3 fails.

  SPORE_LANE=N python3 tools/tests/coop_lineage_current.py [--old]
"""
import re
import sys
import time

from twoinst import Checks, shot, start
from coop_lineage_cells import lineage, out, own_cell, roam, wait


def own_level(con):
    m = re.search(r'own (\w{8}) food_level=(-?\d+)', out(con, 'coop.lineage'))
    return (m.group(1), int(m.group(2))) if m else (None, -1)


def main():
    old = '--old' in sys.argv
    host, guest = start('cell', own_progress=True)
    ck = Checks()
    try:
        if old:
            for con in (host, guest):
                con.call('coop.fix cell.lineage_current off')
            host.call('coop.lineage share 100')
        mine = own_cell(guest)
        gmodel, _ = own_level(guest)
        hmodel, hlevel = own_level(host)
        print(f'  host model {hmodel} level {hlevel}, guest model {gmodel} (chose {mine})', flush=True)
        ck(wait(lambda: lineage(host)['pool'] > 0, 90, 3), 'the host has candidates for its pool')
        time.sleep(20)  # the next build (15 s) knows the guest's current cell
        h = lineage(host)
        print('  candidates: ' + ', '.join(f"{c['key']} {c['diet']} {c['from']}" for c in h['cands']), flush=True)
        ck(h['cands'] and all(c['from'].startswith('guest:') for c in h['cands']) and len(h['cands']) == 1,
           f"1: the only candidate is the guest's current cell ({[c['from'] for c in h['cands']]})")
        cand = {c['key'] for c in h['cands']}
        seen, levels, others = set(), set(), set()
        t0 = time.time()
        k = 0
        while time.time() - t0 < 90:
            h = lineage(host)
            _, hlevel = own_level(host)
            for p in h['pcells']:
                seen.add(p['model'])
            for ln in h['raw'].splitlines():
                m = re.match(r'pcell \d+ model=(\w+) .* level=(-?\d+)', ln)
                if m:
                    levels.add((m.group(1), int(m.group(2)), hlevel))
            others |= {m for m in h['live'] if m not in cand and m != hmodel and m.startswith(('29', '7B', '7C', '7D'))}
            roam(host, guest, k)
            k += 1
            time.sleep(6)
        print(f'  player-model cells (model, level, host level): {sorted(levels)}', flush=True)
        ck(seen and seen <= cand, f"2: the host's pool holds cells of the guest's current model only ({sorted(seen)})")
        ck(levels and all(lv == hl for _, lv, hl in levels),
           '2: every player-model cell is of the player species\' level (no bigger species: no giant)')
        g = lineage(guest)
        gl = {m for m in g['live']}
        ck(any(g['remap'].get(c) in gl or c in gl for c in cand),
           f"2: the guest's game draws them (live {g['live']}, remap {g['remap']})")
        ck(not others, f"3: no cell of the host's My Creations models in the host's pool ({sorted(others)})")
        shot(guest, 'lineage_current_guest')
        shot(host, 'lineage_current_host')
    finally:
        for con in (host, guest):
            try:
                con.call('coop.fix cell.lineage_current on')
            except OSError:
                pass
        try:
            host.call('coop.lineage share 17')
        except OSError:
            pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
