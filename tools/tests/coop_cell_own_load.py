#!/usr/bin/env python3
"""BUG-0342: a guest whose own cell is applied at the world load (the level rebuild on the first Cell
frames) was a "ghost" for minutes: it swam through everything and could not eat, bite or be hit.

The guest's choices.ini names a premade cell before it starts (the test profiles have no own cells),
so its own cell comes with the load of the host's world, as for the players. Then, in the guest's
own game: it plays that cell, and swimming into food of its diet makes its game eat it (AvatarEat:
own points up, the cell gone on both machines, coop_cell_new_game.py's probe); its bite through the
game's Bite takes hp from a host NPC in the host's game.

SPORE_FIX_OFF=cell.own_rebuild: the old timing (the run that must FAIL)."""
import re
import time

import twoinst
from coop_cell_new_game import probe
from play_cell import pool
from twoinst import Checks, start

OTHER_CELL = 'BAF1AD40'  # a premade player cell in Spore_Content (coop_creations_cell.py)


def field(out, pat):
    m = re.search(pat, out)
    return m.group(1) if m else None


def main():
    twoinst.PRESET_CHOICES = {'guest': {'cell': OTHER_CELL}}
    host, guest = start('cell')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 40:
        gc = guest.call('coop.creations')['out']
        if field(gc, r'avatar model ([0-9A-F]{8})') == OTHER_CELL:
            break
        time.sleep(2)
    print('  guest:', guest.call('coop.creations')['out'].replace('\n', ' | ')[-160:])
    ck(field(guest.call('coop.creations')['out'], r'avatar model ([0-9A-F]{8})') == OTHER_CELL,
       'the guest plays its own cell after the load')
    time.sleep(8)
    diet = field(guest.call('coop.pool')['out'], r'diet=(\w+)') or '?'
    kinds = {'herb': ['plant'], 'carn': ['meat'], 'omni': ['plant', 'meat']}.get(diet, ['plant', 'meat'])
    print(f'  guest diet {diet}')
    eaten = sum(probe('guest', guest, host, k, 4) for k in kinds)
    ck(eaten >= 2, f"the guest's own game eats food it swims into ({eaten} of {4 * len(kinds)})")
    # a bite through the game's Bite on the nearest host NPC: the host's game takes its hp
    out = guest.call('coop.pool realbite 40')['out'].strip()
    print('  realbite:', out[:160])
    key = field(out, r'\bc(\d+)')
    time.sleep(2)
    p = pool(guest)
    print(f"  guest hits_sent={p['hits_sent']} own_points={p['own_points']}")
    ck(key is not None and (p['hits_sent'] or 0) > 0, "the guest's bite reaches a host cell")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
