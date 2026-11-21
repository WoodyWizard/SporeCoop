#!/usr/bin/env python3
"""BUG-9267: a guest that plays its own creature joins a planet where it has no nest yet: it loads the host's world ONCE
(the world waits for its own nest, BUG-9262), not once without the nest and again with it.

Session 1 (the fixture): the guest chooses its own creature (its record for this world, worlds.ini), the host makes
no nest yet (`coop.fix creature.own_nests off`, a session switch: gone with the restart). A new evening
(twoinst.session_restart): both games start cold, the guest joins from the galaxy menu. Checks:
  1. the host makes the guest's own nest (seat 1, the guest's species)
  2. the guest's game lives in it
  3. the guest's game loaded the host's world once since it joined (its log: `world: loading <planet> from`)

Fails without the fix: SPORE_FIX_OFF=creations.join_world_record (the guest then reports the world's species first, the
host's share hold lets the world go before the nest, the guest loads it twice). Manual runs on Windows (lane A,
2026-11-20): off 2 loads, on 1 (BUG-9267).
"""
import os
import re
import time

import creations_state
import lane
import twoinst
from twoinst import Checks, start, wait_guest_in


def out(con, line):
    return con.call(line).get('out', '') or ''


def wait(f, secs, step=2.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except (OSError, ValueError, AttributeError):
            pass
        time.sleep(step)
    return False


def log_size():
    try:
        return os.path.getsize(lane.game_log(lane.GUEST))
    except OSError:
        return 0


def loads_since(offset):
    try:
        with open(lane.game_log(lane.GUEST), 'rb') as f:
            f.seek(offset)
            text = f.read().decode('utf-8', 'replace')
    except OSError:
        return -1
    return len(re.findall(r'world: loading \S+ from ', text))


def main():
    host, guest = start('creature')
    ck = Checks()
    out(host, 'coop.fix creature.own_nests off')
    st0 = creations_state.save(guest)
    own = creations_state.choose_own(guest)
    ck(own is not None, f'the guest has a creature of its own ({own})')
    ck(wait(lambda: re.search(r'player 1 creature: 7C', out(host, 'coop.creations')), 90),
       'session 1: the host has the guest\'s own creature (its record for this world is written)')
    mark = {}

    def before_join(_guest):
        mark['log'] = log_size()

    host, guest, _info = twoinst.session_restart(host, guest, before_join=before_join)
    ck(wait_guest_in(host, guest), 'session 2: the guest joined from the galaxy menu')
    ck(wait(lambda: re.search(r'seat 1: species 7C[0-9A-F]{6} nest .* living [1-9]', out(host, 'coop.homenests')), 240, 4),
       f'host: the guest\'s own nest ({out(host, "coop.homenests").strip()[:200]})')
    ck(wait(lambda: re.search(r'herd_species_key=7C[0-9A-F]{6} avatar_key=7C[0-9A-F]{6}', out(guest, 'coop.nest home'))
            and 'dist=0.0' in out(guest, 'coop.nest home'), 240, 4),
       f'guest: it lives in its own nest ({out(guest, "coop.nest home").strip()})')
    time.sleep(20)  # a second load would come now
    n = loads_since(mark.get('log', 0))
    ck(n == 1, f'guest: the host\'s world loaded once since the join ({n})')
    creations_state.restore(guest, st0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
