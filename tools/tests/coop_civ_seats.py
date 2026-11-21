#!/usr/bin/env python3
"""BUG-9274 (the Lineages port, phase 5 for Civ): every player's own nation.

The guest plays its own creature (its newest My Creations creature, creations_state.choose_own) in the host's
Civilization world (civ_npc: the host's capital and NPC nations, free city sites left). Checks, each on the game that
shows it:
  1. host: the guest's party seat gets a capital of the guest's species at the next free site (`coop.civ nations`: a
     seat with a nation of the guest's species, owner the guest), allied with the host's nation (both relations over
     the allied threshold)
  2. guest: its game has that nation as its own (the political id of the cities the host says are its own); the
     citizens of that capital are of the guest's species on both games (`coop.civ nations citizens`); the HUD progress
     counts the team's cities
  3. guest: a vehicle bought in its own city is made in its nation on the host (the host's game: owner the guest, the
     nation's political id) and paid from its nation's money, which is the guest's HUD money; the guest's move order
     drives it on the host's game
  4. host: its nation cannot take the guest's city (`coop.civ capture`: refused, the owner stays the guest)
  5. host: an NPC city the guest's nation takes is the guest's, on both games
  6. a rejoin: the nation stays while the guest is away (nobody commands it), and is the guest's again after the join;
     none founded again
  7. a new evening (twoinst.session_restart): the host loads the planet alone: the nation of the guest's species is
     there; after the join it is the guest's again, none founded again
  8. the team victory: the remaining sites are founded and taken by the players' nations (`coop.civ nations spawn`,
     `capture`, as the campaign's shortcut): only the players' nations are left, the host's game shows the planet won
     (`won=1`, the end window 0x6453746); the Space vote and the spacecraft editor follow as before (manual run)

A new feature: on main `coop.civ nations` is unknown and check 1 fails; SPORE_FIX_OFF=civ.own_nations (on the host)
fails check 1. The manual run on Windows (lane C, 2026-11-20) is in the ticket.
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
        except (OSError, ValueError, AttributeError, TypeError, IndexError):
            pass
        time.sleep(step)
    return False


SEAT = re.compile(r'seat (\d+): owner=(-?\d+) nation=([0-9A-F]{8}) species=([0-9A-F]{8}) cities=(\d+) money=(-?\d+) level=(-?\d+)')


def seats(host):
    """{seat: (owner, nation, species, cities, money, level)} of `coop.civ nations` on the host"""
    return {int(m[0]): (int(m[1]), m[2], m[3], int(m[4]), int(m[5]), int(m[6])) for m in SEAT.findall(out(host, 'coop.civ nations'))}


def counter(con, name):
    m = re.search(name + r'=(\d+)', out(con, 'coop.civ nations'))
    return int(m.group(1)) if m else -1


def own_nation(guest):
    m = re.search(r'own nation ([0-9A-F]{8}) species=([0-9A-F]{8}) cities=(\d+)', out(guest, 'coop.civ nations'))
    return (m.group(1), m.group(2), int(m.group(3))) if m and m.group(1) != '00000000' else None


def cities(con):
    """[(host index, political id, owner)] of `coop.civ`"""
    return [(int(i), p, int(o)) for i, p, o in
            re.findall(r'^city (\d+) \S+ pol=([0-9A-F]{8})(?: \(player\))? owner=(-?\d+)', out(con, 'coop.civ'), re.M)]


def vehicles(con):
    """{id: (political id, owner, position)} of `coop.civ`"""
    return {int(m[0]): (m[1], int(m[2]), (float(m[3]), float(m[4]), float(m[5]))) for m in
            re.findall(r'veh #(\d+) \S+ pol=([0-9A-F]{8}) owner=(\d+) .*?pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', out(con, 'coop.civ'))}


def money(con):
    m = re.search(r'money=(-?\d+)', out(con, 'coop.wallet'))
    return int(m.group(1)) if m else None


def city_pos(con, ci):
    m = re.search(r'^city %d .*? pos=\((-?\d+) (-?\d+) (-?\d+)\)' % ci, out(con, 'coop.civ'), re.M)
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def look_at(con, p):
    e = tuple(x * 1.35 for x in p)
    out(con, 'coop.view set %.1f %.1f %.1f %.1f %.1f %.1f 50' % (e + p))


def main():
    host, guest = start('civ_npc')
    ck = Checks()
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

    # 1. the guest's capital (the next free site), its species, allied
    def partner():
        return next(((s, v) for s, v in seats(host).items() if s >= 1 and v[1] != '00000000'), None)
    ck(wait(lambda: partner() and partner()[1][0] == gid, 240, 3), f'host: the guest\'s seat has a nation it commands ({seats(host)})')
    seat, (owner, faction, species, ncities, nmoney, level) = partner() or (0, (0, '', '', 0, 0, 0))
    print(' ', out(host, 'coop.civ nations').strip().replace('\n', ' | ')[:700], flush=True)
    ck(species == key, f'host: the nation is of the guest\'s species ({species}, {key})')
    rel = {}

    def allied():
        m = re.search(r"relation to the host (-?[\d.]+), the host's to it (-?[\d.]+) \(allied (-?[\d.]+)\)",
                      out(host, 'coop.civ nations relation'))
        rel['v'] = m.groups() if m else None
        return m and float(m.group(1)) >= float(m.group(3)) and float(m.group(2)) >= float(m.group(3))
    ck(wait(allied, 30), f'host: the two nations are allies ({rel.get("v")})')
    founded = counter(host, 'founded')

    # 2. the guest's own nation, its species' citizens, the team's progress
    wait_guest_in(host, guest)
    ck(wait(lambda: (own_nation(guest) or ('',))[0] == faction, 240, 4), f'guest: its own nation is {faction} ({own_nation(guest)})')
    gc = [c for c in cities(host) if c[1] == faction]
    ck(bool(gc) and gc[0][2] == gid, f'host: the capital is the guest\'s ({gc})')
    ci = gc[0][0] if gc else 0
    p = city_pos(host, ci)
    if p:
        look_at(host, p)
        look_at(guest, p)
        time.sleep(8)
    want = f'city_pol={faction} species={key}'
    ck(wait(lambda: want in out(host, 'coop.civ nations citizens'), 30), f'host: the capital\'s citizens are of the guest\'s species')
    ck(wait(lambda: want in out(guest, 'coop.civ nations citizens'), 30), f'guest: the capital\'s citizens are of its species')
    shot(guest, 'civ_seats_guest_capital.png')
    hud = re.search(r'hud=\d+ \(own ([\d.]+) team ([\d.]+)\)', out(guest, 'coop.civ nations'))
    ck(hud and float(hud.group(2)) > float(hud.group(1)), f'guest: the HUD progress counts the team\'s cities ({hud and hud.groups()})')
    out(host, 'coop.view off')
    out(guest, 'coop.view off')

    # 3. a vehicle of its own nation, paid from its money; its move order on the host's game
    m0 = money(guest)
    before = set(vehicles(host))
    out(guest, f'coop.civ buy {ci} 0')
    ck(wait(lambda: any(v[0] == faction and v[1] == gid for i, v in vehicles(host).items() if i not in before), 30, 1),
       'host: the guest\'s purchase is a vehicle of its nation, the guest\'s')
    vid = next((i for i, v in vehicles(host).items() if i not in before and v[0] == faction), None)
    st = seats(host).get(seat)
    ck(wait(lambda: money(guest) is not None and m0 is not None and money(guest) < m0 and
            abs(money(guest) - seats(host)[seat][4]) <= 1, 20),
       f'guest: paid from its nation\'s money (HUD {m0} -> {money(guest)}, nation {st and st[4]})')
    if vid:
        p0 = vehicles(host)[vid][2]
        r = math.dist(p0, (0, 0, 0))
        q = (p0[0] + 25, p0[1] + 10, p0[2])
        k = r / math.dist(q, (0, 0, 0))
        q = tuple(x * k for x in q)
        out(guest, 'coop.civ move %d %.1f %.1f %.1f' % ((vid,) + q))
        ck(wait(lambda: math.dist(vehicles(host)[vid][2], p0) > 5, 30, 1), 'host: the guest\'s order drives its vehicle')

    # 4. allies: the host's nation does not take the guest's city
    out(host, f'coop.civ capture {ci}')
    time.sleep(3)
    ck([c[2] for c in cities(host) if c[0] == ci] == [gid], 'host: its capture of the guest\'s city is refused (allies)')
    ck(counter(host, 'friendly_fire') > 0, 'host: the refusal counted')

    # 5. a city the guest's nation takes is the guest's
    npc = [c for c in cities(host) if c[2] in (-1, 254, 255)]
    ck(bool(npc), f'an NPC city to take ({cities(host)})')
    if npc:
        out(host, f'coop.civ nations capture {npc[0][0]} {seat}')
        ck(wait(lambda: [c[2] for c in cities(host) if c[0] == npc[0][0]] == [gid], 20, 1), 'host: the taken city is the guest\'s')
        ck(wait(lambda: [c[2] for c in cities(guest) if c[0] == npc[0][0]] == [gid], 30, 2), 'guest: the taken city is its own')

    # 6. a rejoin
    out(guest, 'coop.leave')
    ck(wait(lambda: seats(host).get(seat, (0,))[0] == -1, 30), f'host: the nation stays while its player is away ({seats(host).get(seat)})')
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest joined again')
    gid = int((re.search(r'client id=(\d+)', out(guest, 'coop.status')) or [None, str(gid)])[1])
    ck(wait(lambda: seats(host).get(seat, (0,))[0] == gid, 60), 'host: the guest commands its nation again')
    ck(wait(lambda: (own_nation(guest) or ('',))[0] == faction, 240, 4), 'guest: its own nation again')
    ck(counter(host, 'founded') == founded, 'host: no second nation for the returning player')

    # 7. a new evening: the host loads the planet while the guest is offline
    seen = {}

    def offline_check(_guest):
        h = Console(lane.HOST)
        wait(lambda: 'Civ' in out(h, 'game.mode'), 120)
        wait(lambda: seats(h).get(seat), 30)
        seen['seat'] = seats(h).get(seat)
    host, guest, _info = twoinst.session_restart(host, guest, before_join=offline_check)
    ck(seen.get('seat') and seen['seat'][1] == faction and seen['seat'][2] == key and seen['seat'][0] == -1,
       f'host (cold start, the guest offline): the nation of the guest\'s species, nobody\'s ({seen.get("seat")})')
    ck(wait_guest_in(host, guest), 'the guest joined again')
    ck(wait(lambda: (own_nation(guest) or ('',))[0] == faction, 240, 4), 'guest: its own nation after the reload')
    ck(counter(host, 'founded') == 0, 'host: none founded again after the reload')

    # 8. the team victory: the remaining sites, taken by the players' nations
    turn = 0
    for _ in range(30):
        cs = cities(host)
        npc = [c for c in cs if c[2] in (-1, 254, 255)]
        if not npc:
            r = out(host, 'coop.civ nations spawn')
            if 'no city' in r:
                break
            n0 = len(cs)
            wait(lambda: len(cities(host)) > n0, 30, 2)
            time.sleep(2)
            continue
        out(host, f'coop.civ nations capture {npc[0][0]} {seat if turn % 2 else 0}')
        turn += 1
        time.sleep(3)
    ck(wait(lambda: 'won=1' in out(host, 'coop.civ nations'), 60, 3), f'host: only the players\' nations are left, the planet is won ({cities(host)})')
    ck(wait(lambda: 'none' not in out(host, 'coop.ui where 6453746'), 30), 'host: the game\'s planet-won window')
    shot(host, 'civ_seats_won.png')
    creations_state.restore(guest, st0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
