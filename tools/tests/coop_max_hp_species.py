#!/usr/bin/env python3
"""BUG-1292 (found by the Lineages fork, PR #196, its BUG-9157): the game's base max health (cCreatureAnimal_
BaseMaxHitPoints 0xC04170, creature vt 0x7C) reads species +0x56C with no null check; a creature without a species,
without herd health and without a tuning faults at 0xC04252 (the fork saw it twice on a guest that joined dead and
confirmed its death caption, from a herd update). The natural trigger is not reproducible on demand, so the test puts
the guest's own avatar into that state for one call.

Two games on the creature fixture. Guest: `coop.hp nullspecies` clears the avatar's species (+0xB20), herd (+0x1674)
and tuning (+0xE84), asks vt 0x7C through the game's vtable once and puts the three back. With the fix the call answers
the avatar's max health (guarded +1) and nothing faults; afterwards the avatar has its species again and lives.
SPORE_FIX_OFF=creature.max_hp_species must FAIL (the fault at 0xC04252: the `exc` count, or the game dies).

  SPORE_LANE=9 python3 tools/tests/coop_max_hp_species.py"""
import math
import re
import time

from twoinst import Checks, start


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    before = guest.call('coop.hp species')['out'].strip()
    print('  guest before: ' + before, flush=True)
    m0 = re.search(r'avatar_species=(\S+)', before)
    ck(m0 and m0.group(1) not in ('00000000', '(nil)', '0'), 'the guest has an avatar with a species')
    out = guest.call('coop.hp nullspecies')['out'].strip()
    print('  guest: ' + out, flush=True)
    m = re.search(r'base=(\S+) max=(\S+) guarded=\d+ \(\+(\d+)\)', out)
    base = float(m.group(1)) if m else float('nan')
    ck(m and int(m.group(3)) == 1, 'the base max health of a creature without a species was answered by the guard')
    ck(m and math.isfinite(base) and base >= 1, 'the answer is a usable max health (finite, at least 1)')
    time.sleep(2)
    after = guest.call('coop.hp species')['out'].strip()
    print('  guest after: ' + after, flush=True)
    m1 = re.search(r'avatar_species=(\S+)', after)
    ck(m0 and m1 and m1.group(1) == m0.group(1), 'the avatar has its own species again')
    st = guest.call('coop.hp')['out']
    hp = re.search(r'own: hp=([\d.]+)/([\d.]+)', st)
    print('  guest: ' + (hp.group(0) if hp else st.splitlines()[0] if st else '(no avatar)'), flush=True)
    ck(hp and float(hp.group(1)) > 0 and float(hp.group(2)) > 0, 'the guest\'s avatar lives with its max health')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
