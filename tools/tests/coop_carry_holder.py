#!/usr/bin/env python3
"""BUG-0814: a stick or rock a partner carried is let go in the other game when the partner's creature goes.

The partner's game shows the carried copy in the hold slot of the carrier's puppet (BUG-0458: the game's
Creature_HoldObject: held +0x71 = 1, the physics component attached, +0x1F0 = 3). Nothing let go of it when that
puppet was despawned (the partner died, hid, left, made a new creature): the copy stayed held by a destroyed
creature (in the air where the puppet had been), and a later release named the new puppet that never held it.

Two games on the creature fixture. The guest's creature goes to the save's rock and holds it (the game's
HoldObject, as the carry node calls it: `coop.rules hold`); the host's game must show the guest's puppet holding
its copy. Then the host's game takes the guest's puppet away as on the guest's death (`coop.puppet respawn 4`:
destroyed now, made again 4 s later). 1.5 s later, in the host's game, the copy must not be held any more (the
game's own fields: `coop.rules carry`, `last copy ... game_held=0`; the game's destroy clears it by itself), and 5 s
later it must not hang frozen where the puppet's hand was (the game's destroy leaves it there: lab 20261112-011434).
SPORE_FIX_OFF=creature.carry_holder must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_carry_holder.py"""
import math
import re
import time

from coop_creature_carry import ROCK, carry, rock_pos
from twoinst import Checks, start


def last_copy(con):
    out = con.call('coop.rules carry')['out']
    m = re.search(r'last copy of player \d+ obj=\S+ listed=(\d) game_held=(-?\d+) phys=(-?\d+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), tuple(map(float, m.groups()[3:]))) if m else None


def agl(con, p):
    """height of p over the ground of con's game (the ground radius in its direction)"""
    if not p:
        return None
    m = re.search(r'ground ([-\d.]+)', con.call('coop.abilities ground %.2f %.2f %.2f' % p)['out'])
    return math.sqrt(sum(x * x for x in p)) - float(m.group(1)) if m and float(m.group(1)) > 0 else None


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    rp = rock_pos(guest)
    ck(bool(rp), 'the save\'s rock is in the guest\'s game')
    if not rp:
        return ck.result()
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (rp[0] + 3, rp[1], rp[2]))
    time.sleep(2.5)
    print('  guest: ' + guest.call(f'coop.rules hold {ROCK} 10')['out'].strip(), flush=True)
    time.sleep(2.5)
    h, out = carry(host)
    held = [x for x in h if x[0].startswith('puppet') and x[1].lstrip('0') == ROCK]
    print('  host: ' + out.strip().replace('\n', ' | '), flush=True)
    ck(bool(held), 'the host\'s game shows the guest\'s puppet holding its copy of the rock')
    before = last_copy(host)
    print(f'  host: the copy before: {before}', flush=True)
    print('  host: ' + host.call('coop.puppet respawn 4')['out'].strip(), flush=True)
    time.sleep(1.5)
    after = last_copy(host)
    print(f'  host: the copy 1.5 s after the puppet went: {after}', flush=True)
    ck(after and after[0] == 1, 'the copy is still an object of the host\'s game')
    ck(after and after[1] == 0, f'the copy is not held any more in the host\'s game (game_held={after and after[1]})')
    time.sleep(3.5)
    late = last_copy(host)
    h1 = agl(host, late and late[3])
    print(f'  host: the copy 5 s after: {late}, {h1 and round(h1, 2)} m over the ground', flush=True)
    moved = math.dist(late[3], before[3]) if late and before else -1
    # (frozen in the hand's place: it hangs there; let go: the game's physics throws or rolls it, or it lies on the
    # ground already; the ground radius under a rock on the fixture's slope reads ~4.4 m below it either way)
    ck(late and (moved > 0.3 or (h1 is not None and h1 < 0.6)),
       f'the copy is not frozen where the puppet held it (moved {moved:.2f} m, {h1 and round(h1, 2)} m over the ground)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
