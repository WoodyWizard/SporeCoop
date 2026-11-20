#!/usr/bin/env python3
"""BUG-9268 (the Lineages port, phase 5): every player's own tribe.

The guest plays its own creature (its newest My Creations creature, creations_state.choose_own) in the host's Tribe
world. Checks, each on the game that shows it:
  1. host: the guest's party seat gets a village of the guest's species next to the host's (`coop.tribe villages`:
     seat 1 with a village, members, its own food), allied with the host's (the relation over the allied threshold)
  2. guest: its game has that village with the guest's species and owns its members (the same ids the host gives the
     guest); none of the host's village's members is the guest's
  3. guest: it selects its own members (not the host's) and walks them; the host's game moves them (what the other
     player sees); an attack order of the guest's members on the host's village is refused by the host
  4. both games show the same food in the guest's village (its own store, not the host's)
  5. a rejoin: the guest leaves (its village stays, nobody's members while it is away) and joins again: the same village
     and members are its own, no second village
  6. a new evening (twoinst.session_restart): the host starts cold and loads the planet before the guest joins: the
     village of the guest's archived species is there while the guest is offline; after the join it is the guest's
     again, none founded again
The move to Civilization together is the existing vote (coop_tribe_civ.py); the NPC tribe controller's counts that leave
the players' villages out are counted here (`count_views`).

A new feature: on main `coop.tribe villages` is unknown and check 1 fails; SPORE_FIX_OFF=tribe.own_villages (on the
host) fails check 1. The manual run on Windows (lane D, 2026-11-20) is in the ticket.
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
        except (OSError, ValueError, AttributeError, TypeError):
            pass
        time.sleep(step)
    return False


SEAT = re.compile(r'seat 1: player=(-?\d+) species=([0-9A-F]{8}) faction=([0-9A-F]{8}) village=([0-9A-F]{8}) '
                  r'members=(\d+) food=([-\d.]+) relation=(\S+)')


def seat1(host):
    """(player, species, faction, village, members, food, relation) of seat 1 on the host, or None"""
    m = SEAT.search(out(host, 'coop.tribe villages'))
    if not m:
        return None
    g = m.groups()
    rel = float(g[6]) if g[6] != 'nan' else float('nan')
    return int(g[0]), g[1], g[2], g[3], int(g[4]), float(g[5]), rel


def has_village(s):
    return s is not None and s[3] != '00000000' and s[4] > 0


def counter(host, name):
    m = re.search(name + r'=(\d+)', out(host, 'coop.tribe villages'))
    return int(m.group(1)) if m else -1


def members(con):
    """{id: (owner, species of its tribe, player tribe?, position)} of `coop.tribe`"""
    res, species, player = {}, None, False
    for line in out(con, 'coop.tribe').splitlines():
        t = re.match(r'tribe \d+( \(player\))? at .* species=([0-9A-F]{8})', line)
        if t:
            player, species = bool(t.group(1)), t.group(2)
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) .*?pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', line)
        if m and m.group(1) != '0':
            res[int(m.group(1))] = (int(m.group(2)), species, player, tuple(float(m.group(i)) for i in (3, 4, 5)))
    return res


def own_village(guest):
    """(faction, members, food) of the guest's own village on its game, or None"""
    m = re.search(r'own village \S+ faction=([0-9A-F]{8}) members=(\d+) food=([-\d.]+)', out(guest, 'coop.tribe villages'))
    return (m.group(1), int(m.group(2)), float(m.group(3))) if m else None


def owned_same(host, guest, gid):
    h, g = members(host), members(guest)
    hm = sorted(i for i, v in h.items() if v[0] == gid)
    gm = sorted(i for i, v in g.items() if v[0] == gid)
    return hm, gm


def main():
    host, guest = start('tribe')
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
    # 1. the host founds the guest's village (15 s after the world, 10 s after the creature settled)
    ck(wait(lambda: has_village(seat1(host)), 180, 3), 'host: seat 1 has its own village with members')
    s = seat1(host)
    print(' ', out(host, 'coop.tribe villages').strip().replace('\n', ' | ')[:600], flush=True)
    ck(s and s[1] == key, f'host: the village is of the guest\'s species ({s and s[1]})')
    ck(s and s[6] == s[6] and s[6] > 5, f'host: allied with the host\'s village (relation {s and s[6]})')
    faction = s[2] if s else None
    founded = counter(host, 'founded')
    # 2. the guest's game (the world went out again with the village)
    wait_guest_in(host, guest)
    ck(wait(lambda: (own_village(guest) or ('', 0, 0))[0] == faction and own_village(guest)[1] > 0, 240, 4),
       f'guest: its own village (faction {faction}) with members ({own_village(guest)})')
    ck(wait(lambda: owned_same(host, guest, gid)[0] and owned_same(host, guest, gid)[0] == owned_same(host, guest, gid)[1], 60),
       f'guest: owns the members the host gives it ({owned_same(host, guest, gid)})')
    g = members(guest)
    mine = [i for i, v in g.items() if v[0] == gid]
    ck(mine and all(g[i][1] == key for i in mine) and not any(g[i][2] for i in mine),
       f'guest: its members are of its own village (species {key}), none in the host\'s')
    theirs = [i for i, v in g.items() if v[2] and v[0] != gid]
    ck(bool(theirs), f'guest: the host\'s village keeps the host\'s members ({theirs})')
    shot(guest, 'tribe_seats_guest.png')
    # 3. orders: its own members walk on the host's game; the host's member cannot be selected; no attack on an ally
    for i in mine:
        out(guest, f'coop.tribe select {i} 1')
    ck('selected=0' in out(guest, f'coop.tribe select {theirs[0]} 1') if theirs else False,
       'guest: a member of the host\'s village is not selectable')
    h0 = members(host)
    p = h0[mine[0]][3]
    r = math.dist(p, (0, 0, 0))
    q = (p[0] + 20, p[1] + 10, p[2])
    k = r / math.dist(q, (0, 0, 0))
    q = tuple(x * k for x in q)
    out(guest, 'coop.tribe walk %.1f %.1f %.1f' % q)
    moved = {}

    def walked():
        h1 = members(host)
        moved.update({i: math.dist(h0[i][3], h1[i][3]) for i in mine if i in h1})
        return max(moved.values(), default=0) > 5
    ck(wait(walked, 20, 1), f'host: the guest\'s members walk on the host\'s game ({moved})')
    if theirs:
        out(guest, f'coop.tribe act 7 {theirs[0]}')
        ck(wait(lambda: 'a player\'s village' in out(host, 'coop.tribe'), 10, 1), 'host: the attack on the host\'s village is refused')
    # 4. its own food on both games
    ck(wait(lambda: own_village(guest) and seat1(host) and abs(own_village(guest)[2] - seat1(host)[5]) <= 1, 20),
       f'its village\'s food is the same on both games (guest {own_village(guest)}, host {seat1(host)})')
    ck(counter(host, 'count_views') > 0, f'host: the NPC controller counted without the players\' villages (count_views={counter(host, "count_views")})')
    # 5. a rejoin
    out(guest, 'coop.leave')
    ck(wait(lambda: seat1(host) and seat1(host)[0] == -1 and has_village(seat1(host)), 30), f'host: the village stays while its player is away ({seat1(host)})')
    hm = members(host)
    ck(all(hm[i][0] == 254 for i in mine if i in hm), 'host: its members are nobody\'s while the guest is away')
    guest.call(lane.JOIN)
    wait(lambda: not guest.call('coop.avatar')['ok'], 40)
    ck(wait_guest_in(host, guest), 'the guest joined again')
    ck(wait(lambda: (own_village(guest) or ('', 0, 0))[0] == faction and own_village(guest)[1] > 0, 240, 4), 'guest: its own village again')
    ck(wait(lambda: owned_same(host, guest, gid)[0] and owned_same(host, guest, gid)[0] == owned_same(host, guest, gid)[1], 60),
       f'guest: its members again ({owned_same(host, guest, gid)})')
    ck(counter(host, 'founded') == founded, 'host: no second village for the returning player')
    # 6. a new evening: the host loads the planet while the guest is offline
    seen = {}

    def offline_check(_guest):  # session_restart: the host plays the planet again, the guest is not in yet
        h = Console(lane.HOST)
        wait(lambda: 'Tribe' in out(h, 'game.mode'), 120)
        seen['tribes'] = re.findall(r'^tribe \d+ at .* species=([0-9A-F]{8})', out(h, 'coop.tribe'), re.M)
    host, guest, _info = twoinst.session_restart(host, guest, before_join=offline_check)
    ck(key in seen.get('tribes', []), f'host (cold start, the guest offline): the village of the guest\'s archived species ({seen.get("tribes")})')
    ck(wait_guest_in(host, guest), 'the guest joined again')
    ck(wait(lambda: has_village(seat1(host)) and seat1(host)[2] == faction, 120), f'host: seat 1 is the same village ({seat1(host)})')
    ck(wait(lambda: (own_village(guest) or ('', 0, 0))[0] == faction, 240, 4), 'guest: back in its own village')
    ck(counter(host, 'founded') == 0, 'host: none founded again after the reload')
    creations_state.restore(guest, st0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
