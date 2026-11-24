#!/usr/bin/env python3
"""Cell: the players' old cells as wild species (BUG-1477; idea and RE of PR #211 by IltonBJSilva).

The session setting cell.lineage_cells (off by default) lets some native spawns of the host's population take the look of
a player's old cell (its My Creations cells, not the one it plays); a guest's only with its consent (the co-op window's
box). The test profiles of the lab have cells in My Creations.

  1. the setting on (`coop.settings force cell.lineage_cells on`), the swap share 100 % for a short run
     (`coop.lineage share 100`); the guest ticks its box (`coop.lineage consent on`; the window's button is checked
     with --ui: `lineage` shown, its caption)
  2. the host's pool gets old cells of both players (`coop.lineage`: `pool ... owner=0` and `owner=<guest>`), the guest
     registered every one of them (its `registered=`)
  3. the host's game repopulates the level (`coop.pool rebuild`, the game's RebuildLevel): wild creatures of the old
     cells' models live in the HOST's pool and the GUEST's game shows them too (`coop.pool kin 0`: `kin lineage live=`,
     on each game, >= 3); key frame
  4. the guest unticks its box: the host's pool keeps no cell of the guest
  5. the host's save (a new slot, `coop.save`) holds no key of an old cell (0x7B......): the population is not saved
SPORE_FIX_OFF=cell.lineage_cells must FAIL (no pool, no wild old cell).
An older guest (creations 1.1, 0.7.16.1) is never sent a kind 12 and the host swaps nothing while it is here: the rule
is unit-tested (tests/cell_tests.cpp lineage(), host_swaps); the lab has no 0.7.16.1 build on a lane for it.

  SPORE_LANE=2 python3 tools/tests/coop_lineage_cells.py [--ui]
"""
import argparse
import os
import re
import time

import twoinst
from twoinst import Checks, keyframe, start  # (first: it puts tools/ on the path)
import lane
import settings_lib as sl
from coop_cell_kin_species import keys_in_save
from coop_cell_puppet_scale import playing, wait
from coop_nest_species import fixtures_dir, read_pkg, write_pkg
from harness import field

CLL_TYPE, CLL_GROUP = 0x3D97A8E4, 0x40616200
HOST_CELLS = (0x0625A974, 0x0625A976)
GUEST_CELLS = (0x0625A978, 0x0625A97A)
_ORIG_RESTORE = twoinst.restore_creations


def _spore_game_pkg():
    cands = []
    if os.environ.get('SPORE_GAME'):
        cands.append(os.path.join(os.environ['SPORE_GAME'], 'Data', 'Spore_Game.package'))
    # Walk up from worktree/repo root to find SPORE-lab/laneN or SPORE/Data.
    base = lane.ROOT
    for _ in range(4):
        cands.append(os.path.join(base, 'SPORE-lab', f'lane{lane.LANE}', 'Data', 'Spore_Game.package'))
        cands.append(os.path.join(base, 'SPORE-lab', 'lane2', 'Data', 'Spore_Game.package'))
        cands.append(os.path.join(base, 'Data', 'Spore_Game.package'))
        base = os.path.dirname(base)
    for p in cands:
        if os.path.exists(p):
            return p
    raise RuntimeError(f'Spore_Game.package not found (checked {cands[:4]})')


def _seed_cells():
    """Extract real .cll cell creations from Spore_Game.package into host and guest EditorSaves.package."""
    game_pkg = _spore_game_pkg()
    want = set(HOST_CELLS) | set(GUEST_CELLS)
    by_inst = {r[2]: r for r in read_pkg(game_pkg) if r[0] == CLL_TYPE and r[1] == CLL_GROUP and r[2] in want}
    if len(by_inst) < len(want):
        raise RuntimeError(f'missing .cll creations in {game_pkg}: got {sorted(by_inst)}')

    def restore_with_cells(guest=True):
        _ORIG_RESTORE(guest)
        targets = [(lane.HOST_DIR, 'creations-host.package', HOST_CELLS)]
        if guest:
            targets.append((lane.GUEST_DIR, 'creations-guest.package', GUEST_CELLS))
        for d, base_name, insts in targets:
            p = os.path.join(d, 'EditorSaves.package')
            base = os.path.join(fixtures_dir(), base_name)
            recs = [r for r in read_pkg(p if os.path.exists(p) else base) if not (r[0] == CLL_TYPE and r[2] in want)]
            for inst in insts:
                recs.append(by_inst[inst])
            write_pkg(p, recs)

    twoinst.restore_creations = restore_with_cells


def lineage(con):
    return con.call('coop.lineage')['out']


def live(con):
    v = field(con.call('coop.pool kin 0')['out'], r'kin lineage .*\blive=(\d+)')
    return int(v) if v is not None else 0


def owners(con):
    return [int(m.group(1)) for m in re.finditer(r'^pool [0-9A-F]{8} owner=(\d+)', lineage(con), re.M)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ui', action='store_true')
    a = ap.parse_args()
    _seed_cells()
    try:
        host, guest = start('cell')
    finally:
        twoinst.restore_creations = _ORIG_RESTORE
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    gid = int((re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1])
    ck(sl.force(host, [guest], 'cell.lineage_cells', 'on'), 'cell.lineage_cells on for both')
    host.call('coop.lineage share 100')
    if a.ui:
        from coop_ui import Ui
        gu = Ui(guest, 'guest')
        guest.call('coop.uistate 1')
        st = gu.wait(lambda s: s['open'] == '1' and (s.get('lineage') or {}).get('vis'), 15, 'the lineage box')
        print(f'  guest window: {(st or {}).get("lineage")}', flush=True)
        ck(st and '[ ]' in st['lineage']['text'], 'the guest\'s box is shown, not ticked')
        gu.click('lineage')
        st = gu.wait(lambda s: '[x]' in (s.get('lineage') or {}).get('text', ''), 10, 'the box ticked')
        ck(st, 'the guest ticked its box in the co-op window')
        guest.call('coop.uistate 0')
    else:
        print('  guest: ' + guest.call('coop.lineage consent on')['out'].splitlines()[0], flush=True)
    try:
        got = wait(lambda: (lambda o: 0 in o and gid in o)(owners(host)), 60)
        print('  host: ' + lineage(host).strip()[:600], flush=True)
        print('  guest: ' + lineage(guest).strip()[:300], flush=True)
        ck(got, f'the host\'s pool has old cells of both players (owners {sorted(set(owners(host)))})')
        reg = field(lineage(guest), r'registered=(\d+)')
        ck(reg and int(reg) >= len(owners(host)), f'the guest registered every pool cell ({reg} of {len(owners(host))})')
        time.sleep(8)  # (each cell's records reach every peer before its first use: lineage::kLead)
        print('  host: ' + host.call('coop.pool rebuild')['out'].strip(), flush=True)
        h_live = wait(lambda: (lambda n: n if n >= 3 else None)(live(host)), 30)
        g_live = wait(lambda: (lambda n: n if n >= 3 else None)(live(guest)), 30)
        print(f'  wild old cells: host {live(host)}, guest {live(guest)}; host '
              f'{field(host.call("coop.pool kin 0")["out"], r"(kin lineage .*)")}', flush=True)
        ck(h_live, f'wild creatures of the old cells live in the host\'s pool ({live(host)} >= 3)')
        ck(g_live, f'the guest\'s game shows them too ({live(guest)} >= 3)')
        keyframe('lineage', 'Cell: wild creatures that look like the players\' old cells swim among the native species on both screens')
        print('  guest: ' + guest.call('coop.lineage consent off')['out'].splitlines()[0], flush=True)
        gone = wait(lambda: gid not in owners(host), 15)
        ck(gone, f'the guest\'s consent off: no cell of the guest in the host\'s pool ({sorted(set(owners(host)))})')
        keys = [int(m.group(1), 16) for m in re.finditer(r'^pool ([0-9A-F]{8})', lineage(host), re.M)]
        out = host.call('coop.save')['out']
        print('  host:', out.strip(), flush=True)
        time.sleep(3)
        ck('saved' in out, f'the host saved ({out.strip()})')
        found = {('%08X' % k): keys_in_save(lane.HOST_DIR, k) for k in keys}
        found = {k: v for k, v in found.items() if v}
        ck(not found, f'the host\'s new save holds no key of an old cell ({found})')
    finally:
        guest.call('coop.lineage consent off')
        host.call('coop.lineage share 17')
        sl.force(host, [guest], 'cell.lineage_cells', 'off')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
