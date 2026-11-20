#!/usr/bin/env python3
"""BUG-9276: on a Civilization planet with no free city site a partner's nation is an NPC nation taken over.

civ_npc (the host's capital, NPC nations, free sites): the host holds the own nations (`coop.fix civ.own_nations off`)
and founds NPC cities on every free site (`coop.civ nations spawn`, sites left 0), then lets them go. The guest plays
its own creature. Checks, each on the game that shows it:
  1. host: the guest's seat gets a nation of the guest's species that was an NPC nation before (its political id was
     an NPC city's), the one with the fewest cities; it is the guest's, allied with the host's, its money not over the
     host nation's
  2. guest: its game has that nation as its own, of its species (the world shared again); the city's citizens are of
     the guest's species on both games
  3. a rejoin: the same nation again, none taken over twice

SPORE_FIX_OFF=civ.nation_adopt (on the host) fails check 1 ("<name>'s capital waits: no free site"). The manual run
on Windows (lane C, 2026-11-20, the Kealeneon save with 10 of 10 sites) is in the ticket.
"""
import re
import time

import creations_state
import lane
from twoinst import Checks, shot, start, wait_guest_in

from coop_civ_seats import cities, city_pos, counter, look_at, out, own_nation, seats, wait


def sites_left(con):
    m = re.search(r'sites (\d+) left (\d+)', out(con, 'coop.civ nations'))
    return int(m.group(2)) if m else -1


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    out(host, 'coop.fix civ.own_nations off')  # (before the guest's own creature is known: no capital yet)
    for _ in range(12):
        if sites_left(host) == 0:
            break
        n0 = len(cities(host))
        out(host, 'coop.civ nations spawn')
        wait(lambda: len(cities(host)) > n0, 30, 2)
    ck(sites_left(host) == 0, f'host: every city site founded ({out(host, "coop.civ nations").strip()[-80:]})')
    before = cities(host)
    counts = {}
    for _, pol, owner in before:
        if owner in (-1, 254, 255):
            counts[pol] = counts.get(pol, 0) + 1
    st0 = creations_state.save(guest)
    own = creations_state.choose_own(guest)
    ck(own is not None, f'the guest has a creature of its own ({own})')
    gid = int((re.search(r'client id=(\d+)', out(guest, 'coop.status')) or [None, '1'])[1])
    key = None
    t0 = time.time()
    while time.time() - t0 < 60 and not key:
        key = (re.search(r'player %d creature: (7C[0-9A-F]{6})' % gid, out(host, 'coop.creations')) or [None, None])[1]
        time.sleep(2)
    ck(key is not None, f'the host has the guest\'s creature ({key})')
    out(host, 'coop.fix civ.own_nations on')

    # 1. the guest's nation: an NPC nation taken over
    def partner():
        return next(((s, v) for s, v in seats(host).items() if s >= 1 and v[1] != '00000000'), None)
    ck(wait(lambda: partner() and partner()[1][0] == gid, 120, 3), f'host: the guest\'s seat has a nation it commands ({out(host, "coop.civ nations").strip()[-160:]})')
    seat, (owner, faction, species, ncities, nmoney, level) = partner() or (0, (0, '', '', 0, 0, 0))
    print(' ', out(host, 'coop.civ nations').strip().replace('\n', ' | ')[:700], flush=True)
    ck(species == key, f'host: the nation is of the guest\'s species ({species}, {key})')
    ck(faction in counts, f'host: it was an NPC nation ({faction}, {counts})')
    ck(counts and counts.get(faction) == min(counts.values()), f'host: the one with the fewest cities ({counts.get(faction)} of {counts})')
    ck(seats(host).get(0) and nmoney <= seats(host)[0][4], f'host: its money {nmoney} not over the host nation\'s')
    rel = {}

    def allied():
        m = re.search(r"relation to the host (-?[\d.]+), the host's to it (-?[\d.]+) \(allied (-?[\d.]+)\)",
                      out(host, 'coop.civ nations relation'))
        rel['v'] = m.groups() if m else None
        return m and float(m.group(1)) >= float(m.group(3)) and float(m.group(2)) >= float(m.group(3))
    ck(wait(allied, 30), f'host: the two nations are allies ({rel.get("v")})')
    founded = counter(host, 'founded')

    # 2. the guest's own nation, its species' citizens
    wait_guest_in(host, guest)
    ck(wait(lambda: own_nation(guest) == (faction, key, ncities), 240, 4), f'guest: its own nation {faction} of its species ({own_nation(guest)})')
    gc = [c for c in cities(host) if c[1] == faction]
    ck(bool(gc) and all(c[2] == gid for c in gc), f'host: its cities are the guest\'s ({gc})')
    p = city_pos(host, gc[0][0]) if gc else None
    if p:
        look_at(host, p)
        look_at(guest, p)
        time.sleep(8)
    want = f'city_pol={faction} species={key}'
    ck(wait(lambda: want in out(host, 'coop.civ nations citizens'), 30), 'host: the city\'s citizens are of the guest\'s species')
    ck(wait(lambda: want in out(guest, 'coop.civ nations citizens'), 30), 'guest: the city\'s citizens are of its species')
    shot(guest, 'civ_nosite_guest_city.png')
    out(host, 'coop.view off')
    out(guest, 'coop.view off')

    # 3. a rejoin
    out(guest, 'coop.leave')
    ck(wait(lambda: seats(host).get(seat, (0,))[0] == -1, 30), f'host: the nation stays while its player is away ({seats(host).get(seat)})')
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest joined again')
    gid = int((re.search(r'client id=(\d+)', out(guest, 'coop.status')) or [None, str(gid)])[1])
    ck(wait(lambda: seats(host).get(seat, (0,))[0] == gid, 60), 'host: the guest commands its nation again')
    ck(wait(lambda: (own_nation(guest) or ('',))[0] == faction, 240, 4), 'guest: its own nation again')
    ck(counter(host, 'founded') == founded, 'host: none taken over twice')
    creations_state.restore(guest, st0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
