#!/usr/bin/env python3
"""Cell: a guest without cells of its own picks a premade diet cell in "My cell" (BUG-1476, #210 by IltonBJSilva).

A guest whose My Creations has no cell saw "My cell: the host's species" with no arrows. Now its row offers the game's
three premade player cells (herbivore E219E5D2, carnivore 252A132F, omnivore 0C176D89: docs/re/cell.md
#cell-diet-premades). The test profile of the lab has cells, so the guest's own cells are hidden
(`coop.creations hideown cell on`: its My Creations read as empty, as an empty profile).

  1. the guest's co-op window: the creation row says "game cells: 3" and has arrows (coop.uistate, `cnext` shown)
  2. the guest clicks ">" until its own cell is the carnivore premade 252A132F (at most 4 clicks: the world's own first)
  3. the guest's game plays it (its avatar's model, `coop.creations` "avatar model") and the HOST's game draws the
     guest with it (the guest's puppet, `coop.pool kin` "kin player <id> ... drawn=")
  4. key frame: both games, the guest's cell is the carnivore premade (a red-brown cell with a jaw) on both screens
SPORE_FIX_OFF=cell.diet_premades must FAIL (no choice: the row has no arrows, the guest keeps the host's cell).

  SPORE_LANE=2 python3 tools/tests/coop_cell_diet_premade.py
"""
import re
import time

from coop_ui import Ui
from harness import field, waiter
from twoinst import Checks, keyframe, start

CARNIVORE = '252A132F'
wait = waiter(step=0.5)


def avatar_model(con):
    return field(con.call('coop.creations')['out'], r'avatar model ([0-9A-F]{8})')


def mine_cell(con):
    return field(con.call('coop.creations')['out'], r"mine cell: ([0-9A-F]{8})")


def drawn(host, gid):
    """the model the host's game draws the guest's puppet with"""
    return field(host.call('coop.pool kin')['out'], rf'kin player {gid} cell=\S+ drawn=([0-9A-F]{{8}})')


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(6)
    gid = (re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1]
    host_cell = avatar_model(host)
    print('  guest:', guest.call('coop.creations hideown cell on')['out'].strip(), flush=True)
    try:
        gu = Ui(guest, 'guest')
        guest.call('coop.uistate 1')
        st = gu.wait(lambda s: s['open'] == '1' and s.get('creation'), 15, 'the creation row')
        row = (st or {}).get('creation', '')
        print(f'  guest window: {row!r}; arrows {(st or {}).get("cnext")}', flush=True)
        ck('game cells' in row or 'клетки игры' in row, f'the guest\'s row offers the game cells ({row!r})')
        ck(bool(st and (st.get('cnext') or {}).get('vis')), 'the guest\'s row has the arrows')
        picked = None
        for i in range(4):
            if mine_cell(guest) == CARNIVORE:
                picked = CARNIVORE
                break
            gu.click('cnext')
            time.sleep(1.5)
            print(f'  > {i + 1}: mine cell {mine_cell(guest)}', flush=True)
        picked = picked or (CARNIVORE if mine_cell(guest) == CARNIVORE else None)
        ck(picked, f'the guest picks the carnivore premade {CARNIVORE} with ">" (mine {mine_cell(guest)})')
        played = wait(lambda: avatar_model(guest) == CARNIVORE, 30)
        shown = wait(lambda: drawn(host, gid) == CARNIVORE, 30)
        print(f'  guest avatar {avatar_model(guest)}, host draws the guest as {drawn(host, gid)} (host cell {host_cell})',
              flush=True)
        ck(played, f'the guest\'s game plays the carnivore premade ({avatar_model(guest)})')
        ck(shown, f'the host\'s game draws the guest with the carnivore premade ({drawn(host, gid)})')
        guest.call('coop.pool tp 3 0')  # (next to the host: both screens show both cells)
        time.sleep(2)
        keyframe('premade', 'the guest\'s cell is the carnivore premade (a jaw) on both screens; the host keeps its own cell')
        guest.call('coop.uistate 0')
    finally:
        guest.call('coop.creations unchoose cell')
        guest.call('coop.creations hideown cell off')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
