#!/usr/bin/env python3
"""BUG-0790: a hit on a creature by something that is not a creature names that object as the attacker.

The mod's TakeDamage hook (game.cpp h_TakeDamage) took the attacker's object as `attacker combatant - 0x5A8`, the
offset of a creature's cCombatant; a vehicle's and a UFO's combatant is at +0x508, a turret's at +0x588, a hut's at
+0x120, so the damage filter got a pointer into another object (and read creature fields there).

1. Two games on the creature fixture, the guest next to the host.
2. The host's game makes a UFO (the Drama Manager's UFO visit, `coop.drama start 2`, the game's own event).
3. The UFO's combatant hits the guest's puppet in the host's game through the game's TakeDamage path (`coop.hp ufohit`,
   as a UFO weapon calls it).
PASS: the hook's attacker (the damage log, `coop.hp dmglog`) is the UFO object of the game's list. FAIL
(SPORE_FIX_OFF=creature.attacker_owner): another address (the UFO - 0xA0).

  SPORE_LANE=7 python3 tools/tests/coop_attacker_owner.py
"""
import re
import time

from twoinst import Checks, start


def main():
    host, guest = start('creature')
    ck = Checks()
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.avatar')['out'])
    if m:
        guest.call('coop.npc gopos %.2f %.2f %.2f' % (float(m.group(1)) + 4, float(m.group(2)), float(m.group(3))))
    time.sleep(3)
    ufo = None
    for _ in range(4):
        print('  host:', host.call('coop.drama start 2')['out'].strip().replace('\n', ' | ')[:200], flush=True)
        t0 = time.time()
        while time.time() - t0 < 8:
            host.call('coop.hp dmglog clear')
            r = host.call('coop.hp ufohit 1 0.1')['out'].strip()
            mm = re.search(r'ufo ([0-9A-Fa-fx]+)', r)
            if mm:
                ufo = int(mm.group(1), 16)
                print('  host:', r, flush=True)
                break
            time.sleep(1)
        if ufo:
            break
    ck(ufo is not None, "the host's game has a UFO (its Drama Manager's visit)")
    if ufo:
        log = host.call('coop.hp dmglog')['out']
        print('  host damage log:', log.strip()[:400], flush=True)
        atts = [int(a, 16) for a in re.findall(r'attacker=([0-9A-Fa-fx]+) amount=0\.10 ', log)]
        ck(atts, 'the hit went through the TakeDamage hook')
        ck(atts and all(a == ufo for a in atts), f'the attacker of the hit is the UFO object ({ufo:#x}): {[hex(a) for a in atts]}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
