#!/usr/bin/env python3
"""Civilization: a vehicle gone from the host's game is gone from the guest's game too (BUG-0478).

The divergence detector found "a vehicle more on the guest" in 28% of the Civ comparisons, more the
longer a game ran: the guest removed a mirror the host no longer had with Die(v, 0, -1) alone, which
leaves the dead vehicle in the world (the noun list, the civilization's lists, driven on by its AI).

1. Two games on civ_npc.
2. The host's game removes two vehicles (`coop.civ remove`: the game's own removal, 0xCAA7B0).
3. 6 s later, every vehicle object in each game (the game's noun list, dead ones included:
   `coop.civ nouns 18C6DE8`): the guest has no vehicle the host does not have (within 15 m).
PASS: 3. SPORE_FIX_OFF=civ.mirror_remove: the guest keeps the two dead vehicles (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_civ_mirror_remove.py
"""
import math
import re
import time

from play_civ import civ_state
from twoinst import Checks, start


def objects(con):
    out = []
    for ln in con.call('coop.civ nouns 18C6DE8')['out'].splitlines():
        m = re.match(r'obj \S+ (-?\d+) (-?\d+) (-?\d+) \S+ \S+ pol=(\w+)( dead)?', ln)
        if m:
            out.append(((int(m.group(1)), int(m.group(2)), int(m.group(3))), m.group(4), bool(m.group(5))))
    return out


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    _, hv, _ = civ_state(host)
    _, gv, _ = civ_state(guest)
    ck(len(hv) >= 2 and set(hv) <= set(gv), f'the guest mirrors the host\'s vehicles ({sorted(hv)} / {sorted(gv)})')
    for i in sorted(hv)[:2]:
        print(f'  host removes vehicle {i}:', host.call(f'coop.civ remove {i}')['out'].strip(), flush=True)
    time.sleep(6)
    h, g = objects(host), objects(guest)
    extra = [x for x in g if not any(math.dist(x[0], y[0]) < 15 and x[1] == y[1] for y in h)]
    print(f'  vehicle objects: host {len(h)} {h}', flush=True)
    print(f'  vehicle objects: guest {len(g)} {g}', flush=True)
    ck(not extra, f'the guest has no vehicle the host does not have ({len(extra)}: {extra})')
    ck(not any(d for _, _, d in g), 'no dead vehicle stays in the guest\'s game')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
