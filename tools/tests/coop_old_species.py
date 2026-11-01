#!/usr/bin/env python3
"""BUG-0370: an NPC of a player's replaced creature must still show on the guest.

Player logs (2026-11-01): after the guest's editor gave it a new creature, the host's world still held
a creature of the guest's previous one (7CA388E5, a puppet's leftover a world share brought back).
The host sent it as an NPC; the guest no longer had that species (every machine unregisters a
player's replaced creature), asked the host, and the host answered "not found here": the NPC was
missing on the guest until the next world share.

Here: the guest's creature A (registered on the host as player 1's), the guest switches to its next
own creation B (`coop.creations next creature`: the same replacement as an editor), then the host's
game spawns a creature of species A next to the host (`coop.npc spawn A`). Checked on the guest's
game (its census): an NPC proxy of species A is in its world.

  coop_old_species.py   (SPORE_FIX_OFF=creations.old_species must FAIL)"""
import re
import time

import census
from twoinst import Checks, start, lane_state


def player1_creature(host):
    m = re.search(r'player 1 creature: ([0-9A-F]{8})/', host.call('coop.creations')['out'])
    return m.group(1) if m else None


def main():
    host, guest = start()
    ck = Checks()
    t0 = time.time()
    a = None
    while time.time() - t0 < 30 and not a:
        a = player1_creature(host)
        time.sleep(1)
    print(f'  the guest\'s creature on the host: {a}', flush=True)
    print('  guest:', guest.call('coop.creations next creature')['out'].strip()[:200], flush=True)
    lane_state(**{**lane_state(), 'fixture': 'switched'})  # the guest plays another creation now: no warm reuse
    b, t0 = a, time.time()
    while time.time() - t0 < 40 and b == a:
        b = player1_creature(host)
        time.sleep(1)
    print(f'  after the switch: {b}', flush=True)
    time.sleep(3)
    # the spawned creature is in a herd like a puppet's (no archetype, no tuning): the host's stray
    # pass (npc.strays, BUG-0008) would remove it within a second, as the leftover puppet it looks like
    host.call('coop.fix npc.strays off')
    out = host.call(f'coop.npc spawn {a}')['out'].strip() if a else 'no creature'
    print('  host:', out, flush=True)
    # (the host's census shows no species key for it once the package is unregistered; the guest's
    # proxy has the key the host's NPC sync sends)
    found, t0 = None, time.time()
    while time.time() - t0 < 25 and a:
        rows = [r for r in census.dump(guest).rows if r['kind'] == 'c' and r['own'] == 'proxy' and r['key'] == a]
        if rows:
            found = rows[0]
            break
        time.sleep(1)
    print(f'  on the guest: {"npc %d at %s" % (found["id"], census.fmt(found["pos"])) if found else "none"} '
          f'of species {a}', flush=True)
    st = [w for w in host.call('coop.creations')['out'].split() if w.startswith(('missing=', 'old_sent='))]
    print('  host creations:', ' '.join(st), flush=True)
    ck(a is not None and b is not None and b != a, f'the guest\'s creature changed on the host ({a} -> {b})')
    ck(out.startswith('spawned'), 'the host\'s game made a creature of the guest\'s earlier species')
    ck(found is not None, f'the guest\'s game has that NPC (species {a}) in its world')
    host.call('coop.fix npc.strays on')
    print('  guest:', guest.call('coop.creations prev creature')['out'].strip()[:200], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
