#!/usr/bin/env python3
"""Cell: the copy of the guest's mate in the host's game cannot be killed there (CELL-3 of the external audit,
BUG-9317).

Since BUG-1246 the host keeps a copy of a guest's mate in its pool (the guest's mating call made it in the guest's
game only). It was protected by the invulnerable byte +0x111 alone, which the game's lethal bite (Bite ->
BreakOrSwallow), a mouth's chunks and a swallow do not ask: the host's cell (and its NPCs, and other guests' bites)
killed it, the game dropped its meat, and 0.2 s later the host made the copy again from the guest's next pose: free
meat again and again, and the guest's mate died on the host's screen only.

1. Cell fixture. The guest's mating call (the HUD's heart, a real click) brings its mate; the host's game has the copy
   (`coop.pool mate`: copy(player=1 idx=... alive=1)).
2. Three times the host's cell bites the copy with the game's Bite after its hp was set to 1 (`coop.pool cellhp`,
   `coop.pool kill c<idx>`: the lethal branch, BreakOrSwallow), then once a hit (`kill c<idx> anyhit`: DamageCell,
   KillCell when lethal).
3. PASS: on the host the copy is the same cell, alive and not dying (the game's dying / dead bytes), it was not made
   again, the game dropped no loot by it, and the guest's game still has its mate.
   SPORE_FIX_OFF=cell.mate_copy_safe: the copy breaks, drops meat and is made again: FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_cell_mate_copy.py
"""
import math
import re
import time

from harness import waiter
from twoinst import Checks, keyframe, start
from coop_editor_desync import HEART

wait = waiter(step=0.5)

COPY = re.compile(r'copy\(player=(\d+) idx=([0-9A-F]{8}) alive=(\d) gfx=\d at=\((-?[\d.]+) (-?[\d.]+)\) model=[0-9A-F]{8} '
                  r'hp=(-?\d+) dying=(\d)\)')


def copy_of(host):
    """(idx, alive, (x, y), hp, dying, made) of the host's copy of player 1's mate, or None"""
    out = host.call('coop.pool mate')['out']
    made = re.search(r'\bmade=(\d+)', out)
    for m in COPY.finditer(out):
        if m.group(1) == '1':
            return (int(m.group(2), 16), int(m.group(3)), (float(m.group(4)), float(m.group(5))), int(m.group(6)),
                    int(m.group(7)), int(made.group(1)) if made else -1)
    return None


def loot_near(host, at, rad=8.0, age=20.0):
    n = 0
    for m in re.finditer(r'^loot c\d+ (\S+) age=([\d.]+) pos=\((-?[\d.]+) (-?[\d.]+)\)', host.call('coop.pool loot')['out'],
                         re.MULTILINE):
        if float(m.group(2)) < age and math.dist(at, (float(m.group(3)), float(m.group(4)))) < rad:
            n += 1
    return n


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(4)
    for _ in range(4):  # the mating call (real click on the heart)
        guest.call('coop.pool mate button')
        time.sleep(1)
        guest.call('click %d %d' % HEART)
        time.sleep(3)
        if 'alive=1' in guest.call('coop.pool mate')['out']:
            break
    print('  guest mate: ' + guest.call('coop.pool mate')['out'].strip()[:200], flush=True)
    if not ck('alive=1' in guest.call('coop.pool mate')['out'], "the guest's mating call brought its mate"):
        return ck.result()
    cp = wait(lambda: (lambda c: c if c and c[1] else None)(copy_of(host)), 15)
    if not ck(cp, f"the host's game has the copy of the guest's mate ({host.call('coop.pool mate')['out'].strip()[-200:]})"):
        return ck.result()
    idx, _, at, hp0, _, made0 = cp
    print(f'  host copy c{idx} at {at} hp={hp0} made={made0}', flush=True)
    host.call('coop.pool tp 2 2')  # the host's cell next to the guest (and its mate), as a player swims there
    time.sleep(2)
    loot0 = loot_near(host, at)
    bitten = 0
    for k in range(4):
        print('  host: ' + host.call(f'coop.pool cellhp c{idx} 1')['out'].strip(), flush=True)
        how = 'anyhit' if k == 3 else ''
        out = host.call(f'coop.pool kill c{idx} {how}'.strip())['out'].strip()
        print(f'  host bite {k + 1}: ' + out[:200], flush=True)
        bitten += out.startswith(f'bit c{idx} ')
        time.sleep(1.2)
    # (the command finds the copy by its index; "gone" after a twin's kill: the copy was made again elsewhere)
    ck(bitten >= 1, f"the host's bites reached the copy ({bitten} of 4)")
    time.sleep(1)
    cp1 = copy_of(host)
    loot1 = loot_near(host, at)
    print(f'  host copy after the bites: {cp1}; loot near it {loot0} -> {loot1}; '
          f'{host.call("coop.pool mate")["out"].strip()[-160:]}', flush=True)
    ck(cp1 and cp1[0] == idx and cp1[1] == 1 and cp1[4] == 0,
       f"the host's copy of the guest's mate is the same cell, alive and not dying after the host's lethal bites ({cp1})")
    ck(cp1 and cp1[5] == made0, f'the copy was not made again ({made0} -> {cp1 and cp1[5]})')
    ck(loot1 <= loot0, f'the host\'s game dropped no meat by the copy ({loot0} -> {loot1})')
    ck('alive=1' in guest.call('coop.pool mate')['out'], "the guest's game still has its mate")
    keyframe('mate_copy', "Cell: the guest's mate next to both cells, alive on both screens after the host's bites")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
