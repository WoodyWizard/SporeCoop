#!/usr/bin/env python3
"""BUG-9261 / BUG-9262 (the Lineages port, phases 3 and 4): a partner's own creature lives in the host's save and every
player with their own species has their own nest.

The guest plays its own creature (its newest My Creations creature, creations_state.choose_own). Checks, each on the
game that shows it:
  1. host: seat 1 (the guest) gets a nest of the guest's species 40-100 m from the host's home nest, with living
     members, best friends with the party (the host's relation table: 6), the creature archived next to the planet
  2. guest: its game's home nest is that nest, a herd of its own species (`coop.nest home`), and it plays its own
     creature there
  3. guest: put next to the host's nest it takes a lethal hit; after the death caption it respawns at its own nest
  4. host: a player's hit on the guest's family is dropped (party species)
  5. a new evening (twoinst.session_restart): both games end, the host starts cold and loads the planet before the
     guest joins: the archived creature is registered before the load and the guest's nest is there with its family
     (living members of the guest's species) while the guest is offline; after the guest joins no second nest is made,
     and the guest lives in its own nest again

A new feature: on main `coop.homenests` is an unknown command and the test fails; `coop.fix creature.own_nests off`
on the host fails check 1. The manual run on Windows (lane A, 2026-11-20) is in the tickets.
"""
import math
import re
import time

import creations_state
import lane
import twoinst
from sporectl import Console
from twoinst import Checks, shot, start, wait_guest_in


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


def pos(con):
    m = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', out(con, 'coop.avatar'))
    return tuple(map(float, m.groups())) if m else None


def dist(a, b):
    return math.dist(a, b) if a and b else -1.0


def seat_nest(host):
    """(species, nest position, metres from the host's home, living members) of seat 1, or None"""
    m = re.search(r'seat 1: species ([0-9A-F]{8}) nest \((-?\d+) (-?\d+) (-?\d+)\) (-?\d+) m from the host\'s, '
                  r'members (-?\d+) living (-?\d+)', out(host, 'coop.homenests'))
    if not m:
        return None
    g = m.groups()
    return g[0], tuple(map(float, g[1:4])), float(g[4]), int(g[6])


def guest_home(guest):
    m = re.search(r'home nest at \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) herd=\S+ herd_species_key=([0-9A-F]{8}) '
                  r'avatar_key=([0-9A-F]{8})', out(guest, 'coop.nest home'))
    return (tuple(map(float, m.groups()[:3])), m.group(4), m.group(5)) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    st0 = creations_state.save(guest)
    own = creations_state.choose_own(guest)
    ck(own is not None, f'the guest has a creature of its own ({own})')
    key = None
    t0 = time.time()
    while time.time() - t0 < 60:
        key = (re.search(r'player 1 creature: (7C[0-9A-F]{6})', out(host, 'coop.creations')) or [None, None])[1]
        if key:
            break
        time.sleep(2)
    ck(key is not None, f'the host has the guest\'s creature under its one key ({key})')
    # 1. the host makes seat 1's nest
    ck(wait(lambda: (seat_nest(host) or ('', None, 0, 0))[3] > 0, 180, 4), 'host: seat 1 has its own nest with living members')
    sn = seat_nest(host)
    hn = out(host, 'coop.homenests')
    print(' ', hn.strip().replace('\n', ' | ')[:400], flush=True)
    ck(sn and sn[0] == key, f'host: the nest is of the guest\'s species ({sn and sn[0]})')
    ck(sn and 40 <= sn[2] <= 100, f'host: 40-100 m from the host\'s home nest ({sn and sn[2]} m)')
    ck('relation 6' in hn and 'best_friends 1' in hn, 'host: the guest\'s species is the party\'s best friend (relation 6)')
    ck('archived=1' in out(host, 'coop.creations archive') or 'last=' + str(key) in out(host, 'coop.creations archive'),
       f'host: the guest\'s creature is archived with the planet ({out(host, "coop.creations archive").strip()})')
    nest = sn[1] if sn else None
    # 2. the guest's game: its own nest is its home (the world went out again with the nest)
    wait_guest_in(host, guest)
    ck(wait(lambda: guest_home(guest) and guest_home(guest)[1] == key and dist(guest_home(guest)[0], nest) < 15, 240, 4),
       f'guest: its home nest is its own nest, a herd of its own species ({out(guest, "coop.nest home").strip()})')
    ck(wait(lambda: guest_home(guest)[2] == key, 60), 'guest: it plays its own creature')
    shot(guest, 'home_nests_guest.png')
    # 3. a death away from the nest: the respawn is at its own nest
    hp = pos(host)
    out(guest, 'coop.abilities put %.1f %.1f %.1f' % hp)
    time.sleep(3)
    ck(dist(pos(guest), nest) > 30, f'guest: put next to the host\'s nest ({dist(pos(guest), nest):.0f} m from its own)')
    ck('lethal' in out(guest, 'coop.team die'), 'guest: a lethal hit')
    time.sleep(8)

    def back():
        if 'fl=0x' in out(guest, 'coop.ui find "You were killed"'):
            guest.call('key enter')
        m = re.search(r'hp=([\d.]+)/', out(guest, 'coop.avatar'))
        return m and float(m.group(1)) > 0 and dist(pos(guest), nest) < 20
    ck(wait(back, 150), f'guest: respawned at its own nest ({dist(pos(guest), nest):.0f} m from it)')
    # 4. the party's species are not hunted
    herd = (re.search(r'seat 1: .*\(herd (\S+) ', out(host, 'coop.homenests')) or [None, None])[1]
    before = (seat_nest(host) or ('', None, 0, 0))[3]
    out(host, f'coop.nest kill {herd} 1')
    time.sleep(3)
    after = (seat_nest(host) or ('', None, 0, 0))[3]
    ck(before > 0 and after == before, f'host: a player\'s hit on the guest\'s family is dropped ({before} -> {after} living)')
    # 5. a new evening: the host loads the planet while the guest is offline
    seen = {}

    def offline_check(_guest):  # session_restart: the host plays the planet again, the guest is not in yet
        h = Console(lane.HOST)
        seen['archive'] = out(h, 'coop.creations archive')
        wait(lambda: (seat_nest(h) or ('', None, 0, 0))[3] > 0, 60)
        seen['nest'] = seat_nest(h)
    host, guest, _info = twoinst.session_restart(host, guest, before_join=offline_check)
    ck('restored=1' in seen.get('archive', ''), f'host (cold start): the archived creature is registered before the load ({seen.get("archive", "").strip()})')
    sn2 = seen.get('nest')
    ck(sn2 and sn2[0] == key and sn2[3] > 0 and dist(sn2[1], nest) < 6,
       f'host, the guest offline: its nest loaded with its family ({sn2})')
    ck(wait_guest_in(host, guest), 'the guest joined again')
    ck(wait(lambda: guest_home(guest) and guest_home(guest)[1] == key and dist(guest_home(guest)[0], nest) < 15, 240, 4),
       'guest: back in its own nest')
    ck('made=0' in out(host, 'coop.homenests'), 'host: no second nest for the returning player')
    creations_state.restore(guest, st0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
