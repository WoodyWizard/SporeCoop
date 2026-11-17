#!/usr/bin/env python3
"""BUG-0882: a fault inside a game call that a detour lends state to cannot leave that state lent (Creature).

The game's own handler catches faults inside a call (BUG-0110): a detour that changes game state before the call and
puts it back after it stays changed when a fault unwinds past it. The swap ledger (BUG-0110) undoes such leftovers
once a frame; two newer detours were not in it:
* the relevance pass with partners (view_centres.cpp, BUG-0280) lends the game's sphere vector (manager +0x20) our
  static buffer: left there, every later pass "restores" it to our buffer, and the game's vector code may grow or
  free it (heap corruption, a crash far away);
* the own creature's scale call (creature_hp.cpp, BUG-0732) sets the player species override: left set, the game's
  PlayerSpecies answers the guest's own species for good (relations, sizes, the herd rules of the whole world).

Two games on the creature fixture, the guest playing its own creation (another species than the host's world; chosen
by the test since BUG-1176).
Guest: `coop.hp scaleunwind` runs the own avatar's scale call (vt 0x80, as the game does) and leaves it like a
fault; a second later the game's PlayerSpecies must answer the world's species again (`coop.hp species`: same=0).
Host: `coop.views unwind` makes the next relevance pass with the guest leave like a fault; after the next passes the
game's vector must be on its own memory again (`coop.views`: on_our_buffer=0).
SPORE_FIX_OFF=game.lend_ledger must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_lend_ledger.py"""
import re
import time

import creations_state
from twoinst import Checks, start


def views(con):
    out = con.call('coop.views')['out']
    m = re.search(r'passes_with_partners=(\d+)', out)
    o = re.search(r'on_our_buffer=(\d) unwound=(\d+) undone=(\d+)', out)
    return int(m.group(1)) if m else -1, (int(o.group(1)), int(o.group(2)), int(o.group(3))) if o else None


def main():
    host, guest = start('creature')
    # BUG-1176: the cold start puts the guest's worlds.ini back to the fixture state (no record: the guest plays the
    # world's species); the test needs the guest's own creation, so it chooses it (the newest of creations-guest) and
    # puts the profile back at the end
    st0 = creations_state.save(guest)
    print('  guest profile before:', st0, '| chose', creations_state.choose_own(guest), flush=True)
    try:
        return run(host, guest)
    finally:
        creations_state.restore(guest, st0)


def run(host, guest):
    ck = Checks()
    t0 = time.time()  # the own creature is applied a few seconds after the choice (the swap waits for a calm stage)
    while time.time() - t0 < 30 and 'same=0' not in guest.call('coop.hp species')['out']:
        time.sleep(1)
    time.sleep(5)
    before = guest.call('coop.hp species')['out'].strip()
    print('  guest before: ' + before, flush=True)
    other = 'same=0' in before
    ck(other, 'the guest plays a creature of another species than the world\'s player species')
    if other:
        print('  guest: ' + guest.call('coop.hp scaleunwind')['out'].strip(), flush=True)
        time.sleep(1.0)
        after = guest.call('coop.hp species')['out'].strip()
        print('  guest 1 s later: ' + after, flush=True)
        ck('same=0' in after, 'the game\'s PlayerSpecies answers the world\'s species again (not the guest\'s own)')
    n0, _ = views(host)
    print('  host: ' + host.call('coop.views unwind')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 25 and views(host)[0] < n0 + 2:
        time.sleep(1)
    n1, st = views(host)
    print(f'  host: relevance passes with partners {n0} -> {n1}, on_our_buffer/unwound/undone {st}', flush=True)
    ck(st and st[1] >= 1, 'a pass left like a fault')
    ck(st and st[0] == 0, 'the game\'s sphere vector is on its own memory again (not the mod\'s buffer)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
