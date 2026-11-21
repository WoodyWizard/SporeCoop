#!/usr/bin/env python3
"""BUG-9278 (the Lineages port: its founder rescue and party wipe): a player's downed last villager.

The guest plays its own creature in the host's Tribe world and gets its own village (BUG-9268, coop_tribe_seats.py).
Checks, each on the game that shows it:
  1. host: the guest's village is cut down to one living member (the host's test blows), then a lethal blow on that one
     downs it: alive on the host's game at 1 health (`coop.tribe` hp, the game's dead flag 0), a window of 60 s
     (`coop.tribe rescue`: seat 1 open)
  2. guest: its game shows the same villager down (the window's line for its own player, the mirror alive) and no world
     save runs on the host meanwhile
  3. a host villager walked next to it (the game's own walk order) revives it within a few seconds: 40 % health on the
     host's game, the window closed on both games, the guest's mirror alive
  4. downed again, the guest gives up (as holding G: CMD_GIVE_UP): the party wipe on the host's game (no win yet in the
     fixture: every village loses 10 % of its food; a win: the goal bar drops by one step), the villager up at its
     village; the guest's game shows the same food (wsync) and the villager alive, and loads the refreshed world
A new feature: on main `coop.tribe rescue` is unknown and check 1 fails (the villager dies); with
SPORE_FIX_OFF=tribe.founder_rescue (on the host) check 1 fails too (the game's death).
"""
import math
import re
import time

import creations_state
from twoinst import Checks, shot, start, wait_guest_in


def out(con, line):
    return con.call(line).get('out', '') or ''


def wait(f, secs, step=1.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except (OSError, ValueError, AttributeError, TypeError, KeyError, IndexError):
            pass
        time.sleep(step)
    return False


ROW = re.compile(r'\s+#(\d+) \S+ owner=(-?\d+) .*?hp=(-?[\d.]+).*?pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)')


def members(con):
    """{id: (owner, species, player tribe?, hp, position, dead)} of `coop.tribe`"""
    res, species, player = {}, None, False
    for line in out(con, 'coop.tribe').splitlines():
        t = re.match(r'tribe \d+( \(player\))? at .* species=([0-9A-F]{8})', line)
        if t:
            player, species = bool(t.group(1)), t.group(2)
            continue
        m = ROW.match(line)
        if m and m.group(1) != '0':
            dead = re.search(r'dead=(\d)', line)
            res[int(m.group(1))] = (int(m.group(2)), species, player, float(m.group(3)),
                                    tuple(float(m.group(i)) for i in (4, 5, 6)), int(dead.group(1)) if dead else 0)
    return res


def seat_open(con, seat):
    return re.search(r'seat %d(:| player)' % seat, out(con, 'coop.tribe rescue')) is not None


def counter(con, name):
    m = re.search(name + r'=(\d+)', out(con, 'coop.tribe rescue'))
    return int(m.group(1)) if m else -1


def seat1_food(host):
    m = re.search(r'seat 1: player=\S+ species=\S+ faction=\S+ village=\S+ members=\d+ food=([-\d.]+)', out(host, 'coop.tribe villages'))
    return float(m.group(1)) if m else None


def main():
    host, guest = start('tribe')
    ck = Checks()
    creations_state.choose_own(guest)
    gid = int((re.search(r'client id=(\d+)', out(guest, 'coop.status')) or [None, '1'])[1])
    ck(wait(lambda: seat1_food(host) is not None and 'village=00000000' not in out(host, 'coop.tribe villages'), 240, 3),
       'host: the guest has its own village')
    wait_guest_in(host, guest)
    h = members(host)
    mine = [i for i, v in h.items() if v[0] == gid and not v[2] and v[3] > 0]
    hosts = [i for i, v in h.items() if v[2] and v[3] > 0]
    ck(mine and hosts, f'host: the guest\'s villagers {mine}, the host\'s {hosts}')
    victim, killer = mine[0], hosts[0]
    # 1. the others fall (the game's death: not the last ones), the last one goes down
    for i in mine[1:]:
        out(host, f'coop.tribe pincer {i} {killer} 1000')
    ck(wait(lambda: all(members(host).get(i, (0, 0, 0, 0))[3] <= 0 for i in mine[1:]), 20),
       'host: the guest\'s other villagers fell')
    out(host, f'coop.tribe pincer {victim} {killer} 1000')
    ck(wait(lambda: seat_open(host, 1), 5), 'host: the last villager is down, its window open')
    v = members(host).get(victim)
    ck(v and v[3] == 1.0 and not v[5], f'host: the downed villager is alive at 1 health ({v})')
    # 2. the guest's game
    ck(wait(lambda: seat_open(guest, 1), 5), 'guest: its game shows its villager down (the window\'s line)')
    gv = members(guest).get(victim)
    ck(gv and gv[3] > 0, f'guest: the mirror is alive ({gv})')
    shot(host, 'tribe_rescue_down_host.png')
    shot(guest, 'tribe_rescue_down_guest.png')
    # 3. a host villager walks to it: the revive
    p = members(host)[victim][4]
    out(host, 'coop.tribe order %d 4 %.2f %.2f %.2f' % (killer, *p))
    ck(wait(lambda: not seat_open(host, 1), 40), 'host: a partner villager next to it revived it')
    ck(counter(host, 'revives') >= 1, f'host: revives={counter(host, "revives")}')
    v = members(host).get(victim)
    ck(v and v[3] > 1, f'host: up with its health back ({v})')
    ck(wait(lambda: not seat_open(guest, 1), 5), 'guest: the window closed')
    # 4. down again, far from the host's villagers, and the guest gives up: the party wipe
    hv = members(host)
    far = all(math.dist(hv[i][4], hv[victim][4]) > 20 for i in hosts if i in hv)
    if not far:
        out(host, 'coop.tribe order %d 4 %.2f %.2f %.2f' % (killer, *hv[hosts[-1]][4]))
        wait(lambda: math.dist(members(host)[killer][4], members(host)[victim][4]) > 20, 20)
    time.sleep(2.5)  # (the grace after the revive)
    f0 = seat1_food(host)
    out(host, f'coop.tribe pincer {victim} {killer} 1000')
    ck(wait(lambda: seat_open(host, 1) and seat_open(guest, 1), 5), 'down again on both games')
    out(guest, 'coop.tribe rescue giveup')
    ck(wait(lambda: counter(host, 'wipes') >= 1, 10), f'host: the party wipe ran (wipes={counter(host, "wipes")})')
    v = members(host).get(victim)
    ck(v and v[3] > 1 and not v[5], f'host: the villager is up at its village ({v})')
    f1 = seat1_food(host)
    ck(f0 is not None and f1 is not None and f1 < f0, f'host: the guest\'s village lost food ({f0} -> {f1})')
    shot(host, 'tribe_rescue_wipe_host.png')
    shot(guest, 'tribe_rescue_wipe_guest.png')
    ck(wait(lambda: wait_guest_in(host, guest), 180, 5), 'guest: back in the refreshed world')
    gv = members(guest).get(victim)
    ck(gv and gv[3] > 1, f'guest: its villager alive after the wipe ({gv})')
    ck.done()


if __name__ == '__main__':
    main()
