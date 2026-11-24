#!/usr/bin/env python3
"""A guest's DNA spend owed while the host had no creature is taken off in that world only (CORE-14 of the external
audit, BUG-1506).

With one shared DNA pool (all.shared_dna on) a guest's editor spend that reaches the host while the host has no
creature (an editor, a load, the menu) is kept as owed (BUG-0466) and taken off the host's DNA when its creature is
back. It was taken off in any Creature world with an avatar: a host that loaded another Creature world meanwhile lost
the guest's spend there. Now the owed spend carries its world (as the owed gains, BUG-0233) and is dropped elsewhere.

Creature fixture (Scalinus), two players, pace off:
  1. all.shared_dna on (`coop.settings force`); the other Creature world (the creature_cadian fixture, Cadian) goes into
     another slot of the host's profile; the host loads it once (`coop.menu`, `coop.load`) and its DNA there is noted
     (the save's own value), then loads Scalinus again (the guest follows).
  2. The host goes to the galaxy menu (`coop.menu`); the guest spends 30 (`coop.dna spend 30`, as its editor's
     result): the host keeps it as owed (its status `dna spend: ... owed=-30.0`).
  3. The host loads Cadian: PASS: its DNA there is the save's (the noted value), not 30 less; key frame of the host's
     HUD. SPORE_FIX_OFF=creature.spend_owed_world: the 30 come off the Cadian DNA: FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_dna_spend_owed_world.py
"""
import re
import time

import fixtures
import lane
import settings_lib as sl
import twoinst
from harness import waiter
from lane import sh
from twoinst import Checks, keyframe, played_slot, start

wait = waiter(step=1.0)
SLOT = 22  # the host profile's slot for the other Creature world


def dna(con):
    m = re.search(r'dna=(-?[\d.]+)', con.call('coop.dna bar')['out'] or '')
    return float(m.group(1)) if m else None


def owed(con):
    m = re.search(r'dna spend: .*owed=(-?[\d.]+)', con.call('coop.status')['out'] or '')
    return float(m.group(1)) if m else None


def to_menu(host):
    print('  host menu:', host.call('coop.menu')['out'].strip()[:80], flush=True)
    return wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 90)


def load(host, slot, planet):
    host.call('wait.frames 240 90000')
    print(f'  host load {slot} {planet}:', host.call(f'coop.load {slot} {planet}')['out'].strip()[:80], flush=True)
    return wait(lambda: 'Creature' in host.call('game.mode')['out'] and host.call('coop.avatar')['ok'], 180, 2)


def main():
    twoinst.forget_settings()
    host, guest = start('creature')
    ck = Checks()
    home_slot, home = played_slot()
    other = fixtures.planet_of('creature_cadian').split('/')[1]
    try:
        for con in (host, guest):
            con.call('coop.dna pace 0')
        ck(sl.force(host, [guest], 'all.shared_dna', 'on'), 'the guest plays with all.shared_dna on (one pool)')
        sh('fixture', 'slot', 'creature_cadian', lane.HOST_PROFILE, str(SLOT))
        # 1. the other world's own DNA
        if not ck(to_menu(host) and load(host, f'Game{SLOT}', other), f'the host plays the other Creature world ({other})'):
            return ck.result()
        time.sleep(4)
        base = dna(host)
        print(f'  the DNA of {other}\'s save: {base}', flush=True)
        if not ck(to_menu(host) and load(host, home_slot, home), f'the host plays {home} again'):
            return ck.result()
        ck(twoinst.wait_guest_in(host, guest, 200), "the guest is in the host's world again")
        # Loading the other world reset all.shared_dna to default: re-apply shared DNA and ensure enough DNA to spend 30.
        ck(sl.force(host, [guest], 'all.shared_dna', 'on'), 'both play with all.shared_dna on again')
        if (dna(host) or 0) < 50:
            host.call('coop.dna add 50')
        wait(lambda: (dna(guest) or 0) >= 40, 15)
        time.sleep(2)
        # 2. the owed spend: send the guest's spend while the host is in the galaxy menu (no avatar) so it arrives
        # as an in-flight spend and lands in the host's owed spend buffer (BUG-1506).
        if not ck(to_menu(host), 'the host is in the galaxy menu'):
            return ck.result()
        time.sleep(2)
        print('  guest:', guest.call('coop.dna sendspend 30')['out'].strip()[:100], flush=True)
        ck(wait(lambda: (owed(host) or 0) <= -29, 20), f"the host keeps the guest's spend as owed ({owed(host)})")
        # 3. another Creature world
        if not ck(load(host, f'Game{SLOT}', other), f'the host loads {other}'):
            return ck.result()
        time.sleep(6)
        now = dna(host)
        print(f'  {other}: host DNA {now} (the save: {base}); owed {owed(host)}', flush=True)
        print(' ', next((ln for ln in host.call('coop.status')['out'].splitlines() if ln.startswith('away dna:')), '-'),
              flush=True)
        ck(base is not None and now is not None and abs(now - base) < 1.01,
           f"the guest's spend owed in {home} is not taken off the DNA of {other} ({base} -> {now})")
        keyframe('other_world', f"the host's HUD in {other}: about {base or 0:.0f} DNA (the save's), not 30 less",
                 players={'host': host})
    finally:
        sl.reset([host, guest], ('all.shared_dna',))
        twoinst.forget_settings()
        for con in (host, guest):
            try:
                con.call('coop.dna pace 1')
            except OSError:
                pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
