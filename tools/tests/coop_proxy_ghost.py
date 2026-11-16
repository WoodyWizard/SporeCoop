#!/usr/bin/env python3
"""BUG-0931: a guest's NPC proxy whose creature left the guest's world is not driven, not for one frame (Creature, guest).

Lab 20261111-071518 long_creature: when the guest's baby_game event started (the end of its evolution) its game
removed and freed two proxies; the mod's check against the creature list ran once a second, the proxies are driven in
every frame: 29 caught faults in game::teleport (coop.dll, a null vtable in the freed block) over 0.45 s.

Two games on the creature fixture. On the guest a spawned proxy loses its creature behind the mod's back and its
pointer names memory that is no creature (`coop.npc ghost <id>`: a zeroed block, as the freed one), three times.
Checks: the guest has no caught fault, its log says each time that the proxy's creature is no creature of this world
any more (BUG-0931's check, or BUG-0842's identity check that runs before it in every frame since 0.7.13), and the
proxy is made again (a new creature of the list under the same id).
SPORE_FIX_OFF=npc.proxy_identity,npc.proxy_listed_now must FAIL (faults in the mod's code until the next
once-a-second check); either switch alone is covered by the other (BUG-1195).

  SPORE_LANE=5 python3 tools/tests/coop_proxy_ghost.py
"""
import re
import time

from coop_creature_kills import proxies
from twoinst import Checks, start
import lane


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'] or '')
    return int(m.group(1)) if m else -1


def main():
    host, guest = start()
    ck = Checks()
    path = lane.game_log(lane.GUEST)
    size0 = len(open(path, errors='replace').read())
    t0 = time.time()
    while time.time() - t0 < 30 and len(proxies(guest)) < 3:
        time.sleep(1)
    prox = proxies(guest)
    ids = [i for _, i in sorted((p[3], i) for i, p in prox.items())][:3]
    ck(len(ids) == 3, f'the guest has spawned proxies of the host\'s NPCs ({len(prox)})')
    remade = 0
    for i in ids:
        old = proxies(guest).get(i)
        out = guest.call(f'coop.npc ghost {i}')['out'].strip()
        m = re.search(r'now names ([0-9A-Fa-f]+)', out)
        ghost = int(m.group(1), 16) if m else 0
        time.sleep(2.5)
        new = proxies(guest).get(i)
        ok = bool(ghost and new and new[0] not in (ghost, 0))
        remade += ok
        print(f'  proxy #{i}: {out[:90]}; after 2.5 s: {new and hex(new[0])} (was {old and hex(old[0])}), '
              f'guest faults {faults(guest)}', flush=True)
    # BUG-1195: the identity check of BUG-0842 (0.7.13, every frame before this one) finds the ghost first and says
    # so in its own words; either line names the proxy and says it is made again
    text = open(path, errors='replace').read()[size0:]
    said = sum(1 for i in ids if re.search(rf'npc {i}: the proxy\'s creature [0-9A-Fa-f]+ (is no creature of this world '
                                           rf'any more|is not the one it made any more)', text))
    ck(said >= len(ids), f'the guest\'s log names every proxy whose creature was gone ({said} of {len(ids)})')
    ck(remade == len(ids), f'every such proxy was made again as a creature of the world ({remade} of {len(ids)})')
    n = faults(guest)
    ck(n == 0, f'the guest drove no proxy that was no creature: no caught fault ({n})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
