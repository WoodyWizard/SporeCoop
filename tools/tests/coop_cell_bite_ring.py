#!/usr/bin/env python3
"""Cell stage: a guest's bite on a host creature shows the creature's health ring (BUG-0350).

The game shows a bitten cell's health ring from DamageCell (ShowHealthRollover 0xE62340 when the
avatar bites). On the guest a bite on a mirror of a host creature goes to the host (MSG_CELL_HIT)
and skipped DamageCell, so the guest never saw the hp of what it bit (player report 2026-10-31,
"the guest doesn't see enemies' HP").

Each bite waits until the creature is drawn on the guest's screen (its GFX built, +0x248): the game
shows no ring on a cell it does not draw (ShowHealthRollover returns without GFX), with or without the
fix. The ring is the game's own state: an entry for that cell in the cell UI's rollover pool
(`ring=` of `coop.pool kill` / `coop.pool cell`: its timer, -1 = none; docs/re/cell.md#cell-guest-bite-ring).

1. Control, the host's game: the host's own bite (the game's DamageCell) gives the creature a ring
   in the host's game (the reader sees what the game does).
2. Three guest bites on the same creature (weapon hits through DamageCell, then the game's Bite):
   each time the creature has a ring in the guest's game, read in the same frame and again 0.1 s later,
   and the host took the hp (the bite still goes to the host).

SPORE_FIX_OFF=cell.bite_ring: the run that must FAIL at 2.
"""
import re
import time

from twoinst import Checks, start


def num(out, key, cast=float):
    m = re.search(r'\b' + key + r'=(-?[\d.]+)', out)
    return cast(m.group(1)) if m else None


def main():
    host, guest = start('cell')
    ck = Checks()
    key = None
    r = ''
    time.sleep(3)
    for _ in range(15):  # the creature nearest the guest (this first bite counts as a try only)
        r = guest.call('coop.pool kill - hit')['out']
        m = re.match(r'bit (c\d+) ', r)
        if m and 'dying' not in r:
            key = m.group(1)
            break
        time.sleep(1)
    ck(key is not None, f'the guest found a host creature ({r.strip()})')
    if not key:
        return ck.result()
    print('  ' + host.call(f'coop.pool npcflag {key} hp 60')['out'].strip())  # room for the bites

    # 1. the host's own bite: the game's ring in the host's game
    host.call(f'coop.pool swimto {key} tp')
    time.sleep(2.5)
    r = host.call(f'coop.pool kill {key} hit')['out']
    print(f'  host bite: {r.strip()}')
    ck((num(r, 'ring') or -1) >= 0, f'control: the host\'s own bite gives the creature a ring in the host\'s game ({num(r, "ring")})')

    # 2. the guest's bites
    for i, how in enumerate(('hit', 'hit', '')):
        # the game shows a ring only on a cell it draws (ShowHealthRollover needs its GFX, +0x248):
        # the creature must be on the guest's screen with its body built
        drawn = None
        for _ in range(6):
            host.call(f'coop.pool swimto {key} tp')
            guest.call(f'coop.pool swimto {key} tp')
            time.sleep(2.5)  # also: the hurt lock (+0x17C) of the last bite is over
            st = guest.call(f'coop.pool cell {key}')['out']
            if num(st, 'gfx', int):
                drawn = st
                break
        ck(drawn is not None, f'bite {i + 1}: the creature has its body (GFX) in the guest\'s game ({st.strip()})')
        h0 = num(host.call(f'coop.pool cell {key}')['out'], 'hp', int)
        r = guest.call(f'coop.pool kill {key} {how}'.strip())['out']
        time.sleep(0.1)
        later = guest.call(f'coop.pool cell {key}')['out']
        time.sleep(1.5)
        h1 = num(host.call(f'coop.pool cell {key}')['out'], 'hp', int)
        ring, ring2 = num(r, 'ring'), num(later, 'ring')
        print('    guest cell: ' + later.strip())
        print('    guest me: ' + ' '.join(l for l in guest.call('coop.pool')['out'].splitlines() if l.startswith('avatar ')))
        label = how or "the game's Bite"
        print(f'  guest bite {i + 1} ({label}): {r.strip()} | 0.1 s later ring={ring2} | host hp {h0} -> {h1}')
        ck(ring is not None and ring >= 0, f'bite {i + 1}: the creature shows its health ring in the guest\'s game (ring={ring})')
        ck(ring2 is not None and ring2 >= 0, f'bite {i + 1}: the ring is still up 0.1 s later (ring={ring2})')
        ck(h0 is not None and h1 is not None and h1 < h0, f'bite {i + 1}: the host took the hp ({h0} -> {h1})')
    print('  guest:', ' '.join(l for l in guest.call('coop.pool')['out'].splitlines() if l.startswith('cell_guest_fx')))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
