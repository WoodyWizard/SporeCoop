#!/usr/bin/env python3
"""BUG-9287: own progress in Cell: nobody grows a level alone (the pool is populated at the host's level).

With every player's own food (BUG-9265) the host grew levels ahead of the guest; the host's game populates the one pool
(also the guest's area) at the host's level, so the guest at a lower level found nothing it could eat on its screen and
its DNA did not rise (lane A, 2026-11-20: guest at food 24, host at 312, the guest's `coop.pool see` empty).

The game's own food total (cCellSerializableData +0x1C, `coop.progress` food=) on both games:
  1. both games start at the same level L (the fixture);
  2. the host eats an own meal (`coop.pool meal`, AvatarEat's AddFoodPoints path) that would take it 50 food over the
     next level line: its food stops at the line's last point (L*100 - 1), the rest is kept (bank > 0), the guest's food
     is untouched;
  3. the guest eats over the same line: within 20 s the host's game gets its kept food (its total = what it ate);
  4. no faults.
--old (`SPORE_FIX_OFF=cell.level_together`, twin level_together_off): step 2 fails (the host crosses the line alone).

  SPORE_LANE=N python3 tools/tests/coop_cell_level_together.py [--old]
"""
import re
import sys
import time

from twoinst import Checks, start


def prog(con):
    s = con.call('coop.progress')['out']
    m = re.search(r'\bfood=(-?\d+)', s)
    b = re.search(r'\bbank=(-?\d+)', s)
    return (int(m.group(1)) if m else -1), (int(b.group(1)) if b else 0), s


def wait(f, secs, step=1.0):
    t = time.time() + secs
    while time.time() < t:
        if f():
            return True
        time.sleep(step)
    return False


def main():
    host, guest = start('cell', own_progress=True)
    ck = Checks()
    if '--old' in sys.argv:
        host.call('coop.fix cell.level_together off')
        guest.call('coop.fix cell.level_together off')
    time.sleep(5)
    hf, _, _ = prog(host)
    gf, _, _ = prog(guest)
    lv = lambda f: 1 + f // 100
    print(f'  food: host {hf} guest {gf}', flush=True)
    if lv(gf) != lv(hf):  # the same level first: the lower one eats up to the other's level
        low, n = (guest, lv(hf) * 100 - 100 - gf) if lv(gf) < lv(hf) else (host, lv(gf) * 100 - 100 - hf)
        low.call(f'coop.pool meal {max(n, 1)}')
        time.sleep(3)
        hf, _, _ = prog(host)
        gf, _, _ = prog(guest)
    L = lv(min(hf, gf))
    ck(lv(hf) == lv(gf) and lv(hf) < 10, f'1: both games on level {L} (host {hf}, guest {gf})')
    line = L * 100
    meal = line + 50 - hf
    host.call(f'coop.pool meal {meal}')
    time.sleep(4)
    hf2, bank, raw = prog(host)
    gf2, _, _ = prog(guest)
    print(f'  host meal +{meal}: host food {hf} -> {hf2}, bank {bank}; guest {gf2}', flush=True)
    ck(hf2 == line - 1 and bank == hf + meal - (line - 1),
       f'2: the host stops at the level line ({line - 1}) and keeps the rest (food {hf2}, bank {bank})')
    ck(gf2 == gf, f"2: the guest's food is its own ({gf} -> {gf2})")
    guest.call(f'coop.pool meal {line + 10 - gf2}')
    ck(wait(lambda: prog(host)[0] == hf + meal, 20),
       f'3: the guest crossed the line: the host gets its kept food (host {prog(host)[0]}, wanted {hf + meal})')
    ck(prog(guest)[0] == line + 10, f'3: the guest on the next level too ({prog(guest)[0]})')
    for con in (host, guest):
        con.call('coop.fix cell.level_together on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
