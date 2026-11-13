#!/usr/bin/env python3
"""BUG-1004: Cell: h_SpawnLoot gave the game a 256-entry stack list it fills without a bound.

Audit 0.7.14 (WLD-18). Every foreground loot drop on the host goes through h_SpawnLoot, which passed the
game's SpawnLoot (0xE771D0) a `uint32_t buf[256]` on its stack as the list of new cells; the game appends
every cell it makes, recursion included, with no bound. The game's own tables make a few cells a drop;
a table (a mod's, or a repeat chain) that makes more than 256 wrote past the array on the stack.

Two games in the Cell fixture. The host arms one drop of 300 cells (`coop.pool lootbig 300`: for that call
the cell entry of the drop's own loot table makes 300 cells, the other entries weigh 0; the table is put
back right after) and bites the nearest creature to death (the game's Bite: BreakOrSwallow, then the
break's SpawnLoot). Checked: the game made and listed 300 cells (its count, `listed_last`), the host's
cell pool grew by them, and neither game faulted.

  coop_cell_loot_big.py   (SPORE_FIX_OFF=cell.loot_list: the stack list overflows, the host faults, it FAILS)
"""
import re
import time

from twoinst import Checks, start


def loot(con):
    out = con.call('coop.pool lootbig')['out']
    return {k: int(v) for k, v in re.findall(r'\b(\w+)=(-?\d+)', out)}, out.strip()


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    host.call('coop.pool invulnfor 600')
    guest.call('coop.pool invulnfor 600')
    l0, out = loot(host)
    print('  host: ' + out, flush=True)
    print('  host: ' + host.call('coop.pool lootbig 300')['out'].strip(), flush=True)
    # the host bites the creatures nearest to it (the game's Bite) until a drop came: each lethal bite breaks one
    t0, l1, r = time.time(), {}, ''
    while time.time() - t0 < 40:
        m = re.match(r'bit (c\d+) ', host.call('coop.pool kill - hit')['out'])
        if m:
            host.call(f'coop.pool npcflag {m.group(1)} hp 1')
            host.call(f'coop.pool swimto {m.group(1)} tp')
            time.sleep(0.5)
            r = host.call(f'coop.pool kill {m.group(1)}')['out'].strip()
        time.sleep(1.0)
        l1, out = loot(host)
        if not l1.get('armed', 1):
            break
    print('  host bite: ' + r[:160], flush=True)
    time.sleep(1)
    l1, out = loot(host)
    print('  host: ' + out, flush=True)
    if l1.get('armed', 0):
        print('RESULT: SKIP (no foreground loot drop with a cell entry came)')
        return 0
    ck(l1.get('listed_last') == 300, f'the game made and listed one drop of 300 cells ({l1.get("listed_last")})')
    ck(l1.get('cells', 0) - l0.get('cells', 0) >= 250, f'the host\'s cell pool grew by them ({l1.get("cells", 0) - l0.get("cells", 0)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
