#!/usr/bin/env python3
"""Space: a guest's own badges and captain rank survive its rejoin, the host's never replace them (BUG-0786).

Player report (Discord 2026-11-09): the badges a guest earned in a session are back to the host's after a rejoin.
Every player flies in a copy of the host's save; the badge manager (cSimulatorSpaceGame +0x70: the badge states,
the rank they give, the counters) is saved with that world, so a guest that loads the host's share save again gets
the host's badges (docs/re/space.md#space-badges). The host keeps each guest's own (space_badges.cpp, K_BADGES).

1. Both play the `space` fixture. The guest earns the rank 1 badge (0x58B78701) and another available badge by the
   game's own GiveBadge (`coop.space badges give`), and +7 on counter 2 (stars explored, AddToBadgeProgress): its
   rank is 1; the host's game stays at its own badges.
2. The guest leaves, joins again and loads the host's world.
3. In the rejoined guest's game (`coop.space badges`: the game's badge map, GetRank) the badges, the counter and the
   rank are the ones it had before it left.
4. The host's game still has its own (rank 0, the other badge not earned).
PASS: 1 to 4. SPORE_FIX_OFF=space.keep_badges: the rejoined guest has the host's badges and rank 0 (FAIL).

  SPORE_LANE=5 python3 tools/tests/coop_space_badges_rejoin.py
"""
import os
import re
import time

import lane
from coop_space_map import no_movie, sp, wait
from sporectl import Console
from twoinst import Checks, start, wait_guest_in

RANK1 = '58B78701'
WITH_RANK = os.environ.get('BADGES_RANK', '0') != '0'
R1 = 1 if WITH_RANK else 0
EVENTS = os.environ.get('BADGES_EVENT', '1') != '0'
RESTART = os.environ.get('BADGES_RESTART', '1') != '0'
GIVE = os.environ.get('BADGES_GIVE', '0') != '0'  # 1: GiveBadge from the console (BUG-0956: a rejoin from the same game faults at its load)


def badges(con):
    """(rank, {badge id: state}, {event: count}) of the game's badge manager"""
    out = sp(con, 'badges')
    m = re.search(r'rank=(-?\d+)', out)
    b = {x.group(1): int(x.group(2)) for x in re.finditer(r'^badge ([0-9A-F]+) (-?\d+)', out, re.M)}
    e = {int(x.group(1)): int(x.group(2)) for x in re.finditer(r'^event (-?\d+) (-?\d+)', out, re.M)}
    return (int(m.group(1)) if m else None), b, e


def restart_guest():
    try:
        Console(lane.GUEST).call('quit')
    except OSError:
        pass
    for _ in range(60):
        try:
            Console(lane.GUEST).call('ping')
        except OSError:
            break
        time.sleep(1)
    time.sleep(3)
    lane.run_guest()
    t0 = time.time()
    while time.time() - t0 < 120:
        try:
            c = Console(lane.GUEST)
            if 'GalaxyGameEntry' in c.call('game.mode')['out']:
                c.call('wait.frames 300 30000')
                return c
        except OSError:
            pass
        time.sleep(1)
    return None


def earned(b):
    """the badges the game counts as earned (state < 2: 1 earned, 0 earned and followed by the next of its chain;
    BadgeManager_GetRank, docs/re/space.md#space-badges)"""
    return {k for k, v in b.items() if v < 2}


def epoch(con):
    m = re.search(r'world=(\d+) open=1', sp(con, 'world'))
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    no_movie(host)
    hr0, hb0, he0 = badges(host)
    gr0, gb0, ge0 = badges(guest)
    print(f'  start: host rank {hr0}, {len(hb0)} badges; guest rank {gr0}, {len(gb0)} badges', flush=True)
    ck(hr0 == 0 and gr0 == 0, f'both start at rank 0 (host {hr0}, guest {gr0})')
    other = next((k for k, s in sorted(gb0.items()) if s == 2 and k != RANK1 and hb0.get(k) == 2), None)
    ck(other is not None, f'an available badge to earn ({other})')
    if other is None:
        return ck.result()

    # 1. the guest earns badges
    if WITH_RANK:
        print('  guest: ' + sp(guest, 'badges give', RANK1), flush=True)
        time.sleep(2)
        no_movie(guest)  # the rank badge's cinematic
    if GIVE:
        print('  guest: ' + sp(guest, 'badges give', other), flush=True)
    if EVENTS:
        print('  guest: ' + sp(guest, 'badges event 2 60'), flush=True)
    time.sleep(1)
    if not GIVE:  # the game's own Update gives the badges whose counters are met
        def got_new():
            no_movie(guest, 4)
            _, b, _ = badges(guest)
            new = sorted(k for k, st in b.items() if st == 1 and gb0.get(k) != 1)
            return new or None
        new = wait(got_new, 40, 2)
        print(f'  guest earned by the game: {new}', flush=True)
        ck(new is not None, 'the guest\'s game gives badges for its counter by itself')
        if not new:
            return ck.result()
        other = new[0]
    no_movie(guest)
    gr1, gb1, ge1 = badges(guest)
    print(f'  guest before leaving: rank {gr1}, {RANK1}={gb1.get(RANK1)}, {other}={gb1.get(other)}, event 2={ge1.get(2)}',
          flush=True)
    ck(gr1 == R1 and (gb1.get(RANK1) == 1 or not WITH_RANK) and gb1.get(other) == 1 and (ge1.get(2, 0) >= 60 or not EVENTS),
       f'the guest\'s game: rank {R1}, the badge earned, counter 2 >= 60')
    hr1, hb1, _ = badges(host)
    ck(hr1 == 0 and hb1.get(other) != 1, f'the host\'s game keeps its own (rank {hr1}, {other}={hb1.get(other)})')
    time.sleep(4)  # the guest's badges reach the host
    # the game goes on with a badge chain by itself for a few seconds (056 earned, then 055 earned and 056 state 0):
    # what the guest has when it leaves is what must come back
    no_movie(guest, 4)
    gr1, gb1, ge1 = badges(guest)
    print(f'  guest when it leaves: rank {gr1}, earned {sorted(k for k, v in gb1.items() if v == 1)}', flush=True)
    print('  host: ' + sp(host, 'badges kept').replace('\n', ' | '), flush=True)

    # 2. leave and join again. RESTART=1 (the default): the guest's game is closed and started again in between (a
    # rejoin after a crash or a restart); RESTART=0: it joins from the same game, whose load of the host's world
    # then faults after a badge was earned (BUG-0956, with space.keep_badges on and off)
    guest.call('coop.leave')
    time.sleep(4)
    if RESTART:
        guest = restart_guest()
        ck(guest is not None, 'the guest\'s game started again')
        if guest is None:
            return ck.result()
        e0 = 0
    else:
        e0 = epoch(guest)
    guest.call(lane.JOIN)
    loaded = wait(lambda: epoch(guest) > e0 and 'Space' in guest.call('game.mode')['out'], 180, 2)
    ck(loaded is not None, f'the guest loaded the host\'s world again (Space world {e0} -> {epoch(guest)})')
    ck(wait_guest_in(host, guest, timeout=120), 'the guest is back in the host\'s world')
    no_movie(guest)

    # 3. its own badges again
    def own():
        r, b, e = badges(guest)
        return (r, b, e) if r == R1 and (b.get(RANK1) == 1 or not WITH_RANK) and earned(gb1) <= earned(b) and e.get(2, 0) >= ge1.get(2, 0) else None
    r = wait(own, 45, 2)
    gr2, gb2, ge2 = badges(guest)
    print(f'  rejoined guest: rank {gr2}, {RANK1}={gb2.get(RANK1)}, {other}={gb2.get(other)}, event 2={ge2.get(2)}; '
          + sp(guest, 'badges').splitlines()[0], flush=True)
    ck(r is not None, f'the rejoined guest\'s game has its own badges and rank again (rank {gr2})')
    ck(earned(gb1) <= earned(gb2), f'every badge the guest had earned is earned again (missing {sorted(earned(gb1) - earned(gb2))}; '
                                   f'states that differ, the game goes on with a chain by itself: '
                   f'differ: {[(k, gb1.get(k), gb2.get(k)) for k in sorted(set(gb1) | set(gb2)) if gb1.get(k) != gb2.get(k)][:6]})')

    # 4. the host keeps its own
    hr2, hb2, _ = badges(host)
    ck(hr2 == 0 and hb2.get(other) != 1, f'the host\'s badges are still its own (rank {hr2}, {other}={hb2.get(other)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
