#!/usr/bin/env python3
"""Creature: a joining guest's creature stands beside the host's, not inside it (BUG-0989).

Key frames of the 0.7.13 final check (play_creature start, find_layout, same_view): on the creature fixture both games
showed the host's and the guest's creatures at one point, drawn inside each other in other poses on each game, and
at the join the host's name tag hung over the guest's own creature for a moment. The guest loads the host's save, so
its creature starts where the save has the player's creature: where the host's creature stands.

1. Two games on the creature fixture; the guest joins the host's world again (as a player's rejoin: the switches a
   test turns off act from this join on; the first join is the fixture's).
2. For 20 s both games' own creature (`coop.avatar`) and their puppet of the other (`coop.status`: the game's
   creature as it draws it) twice a second.
Checked: the guest's creature started within 2.5 m of the host's (the case; else SKIP); 15-20 s after the join the two
are at least 3 m apart on both games (the guest's game: its creature and the host's puppet; the host's game: its
creature and the guest's puppet). Key frame: both games' own view after it. SPORE_FIX_OFF=core.join_apart: they stay
in one spot (FAIL).

  SPORE_LANE=6 python3 tools/tests/coop_creature_join_apart.py
"""
import math
import re
import time

from twoinst import Checks, keyframe, start, wait_guest_in  # (first: it puts tools/ on the path)
import lane


def own(con):
    m = re.search(r'pos=\(\s*(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\)', con.call('coop.avatar').get('out') or '')
    return tuple(float(v) for v in m.groups()) if m else None


def puppet(con, rid):
    """the puppet of remote rid ('0' the host; None: the first other player's)"""
    for m in re.finditer(r"remote (\d+) '[^']*'[^\n]*\n\s+puppet pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)", con.call('coop.status')['out']):
        if (rid is None and m.group(1) != '0') or m.group(1) == rid:
            return tuple(float(v) for v in m.groups()[1:])
    return None


def apart(con):
    m = re.search(r'moves=(\d+) .*from=([-\d.]+)', con.call('coop.npc joinapart')['out'])
    return (int(m.group(1)), float(m.group(2))) if m else (0, -1.0)


def main():
    host, guest = start('creature')
    ck = Checks()
    print('  guest: ' + guest.call('coop.npc joinapart')['out'].strip() + ' (the fixture\'s join)', flush=True)
    moves0 = apart(guest)[0]
    guest.call(lane.JOIN)
    t1 = time.time()
    while time.time() - t1 < 40 and guest.call('coop.avatar')['ok']:
        time.sleep(0.5)
    ck(wait_guest_in(host, guest), 'the guest joined the host\'s world again')
    t0 = time.time()
    first, last_g, last_h, rows = None, None, None, []
    while time.time() - t0 < 20:
        ga, gp = own(guest), puppet(guest, '0')
        ha, hp = own(host), puppet(host, None)
        dg = math.dist(ga, gp) if ga and gp else None
        dh = math.dist(ha, hp) if ha and hp else None
        if first is None and dg is not None:
            first = dg
        if time.time() - t0 > 15:
            last_g, last_h = dg if dg is not None else last_g, dh if dh is not None else last_h
        rows.append((round(time.time() - t0, 1), None if dg is None else round(dg, 2), None if dh is None else round(dh, 2)))
        time.sleep(0.5)
    print(f'  (s, guest: own to the host\'s puppet m, host: own to the guest\'s puppet m): {rows[:12]} ... {rows[-3:]}', flush=True)
    print('  guest: ' + guest.call('coop.npc joinapart')['out'].strip(), flush=True)
    keyframe('join', 'both games 20 s after the guest joined: the host\'s and the guest\'s creatures stand side by side, '
                     'not in one spot drawn inside each other; no name tag over a player\'s own creature', settle=1.0)
    moves, frm = apart(guest)
    if moves > moves0 and 0 <= frm < 2.5:  # (the fix put it aside before the first sample: where it started)
        first = frm
    if first is None or first >= 2.5:
        print(f'RESULT: SKIP the guest\'s creature started {first} m from the host\'s (not on it)', flush=True)
        return 0
    ck(True, f'the guest\'s creature started {first:.2f} m from the host\'s (on it)')
    ck(last_g is not None and last_g >= 3.0, f'the guest\'s game: its creature and the host\'s {last_g} m apart (>= 3 m)')
    ck(last_h is not None and last_h >= 3.0, f'the host\'s game: its creature and the guest\'s {last_h} m apart (>= 3 m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
