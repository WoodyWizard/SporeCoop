#!/usr/bin/env python3
"""BUG-1279 (WP-K1 of docs/plan-0.7.17.md): a guest in the host's Space world has the host captain's property list,
so its captain screen shows the captain's items (docs/re/creations.md#creations-captain-0717).

A guest plays the host's empire; its captain (cEmpire +0xC4) is the host's creation, whose property list {captain,
0x00B1B104, group | 0xE500} (the item config 0xC339D5C7, the unlocked items) the guest's game never had: its captain
screen showed no item limits (BUG-0304's guard counts each null item list: `coop.captain` "items: none N").

  1. the host's captain gets a captain's property list (`coop.captain plant`: the game's CaptainProps_Copy of the
     galaxy menu's captain template into the captain's database, as a new captain gets one) and the host saves;
  2. the host's mod sends it (MSG_CREATION kind 10): the guest's game finds the list for the empire's captain
     (`coop.captain` props=1: the game's own CaptainProps_Get);
  3. the guest opens the captain panel the reporter's way (the adventure mission's Accept, coop_space_captain's
     accept_route): the game's item palette got a list (BUG-0304's guard saw no null list: "items: none" unchanged);
     key frame of the Equipment panel.
The guest's own captain (BUG-1475) is off here: the guest keeps the host's captain.
  SPORE_FIX_OFF=creations.captain_props must FAIL (props=0 on the guest, "items: none" +1).

  SPORE_LANE=2 python3 tools/tests/coop_space_captain_items.py
"""
import re
import time

import coop_space_finale as fz
import play_space
from coop_space_captain import BADGE, accept_route
from twoinst import Checks, keyframe, save_taken, start


def captain(con):
    out = con.call('coop.captain')['out'] or ''
    p = re.search(r'props=(\d)', out)
    k = re.search(r'captain=([0-9A-F]{8})', out)
    n = re.search(r'items: none (\d+)', out)
    return (p and p.group(1) == '1', k.group(1) if k else None, int(n.group(1)) if n else -1, out.strip())


def lists(con):
    out = con.call('coop.creations status')['out'] or ''
    return [ln.strip() for ln in out.splitlines() if 'captain list' in ln]


def main():
    host, guest = start('space', planet=fz.fixture_planet())
    ck = Checks()
    for c in (host, guest):
        c.call('coop.fix space.own_captain off')  # (BUG-1475: the guest keeps the host's captain here)
    time.sleep(3)
    fz.get_drive(guest, 'guest', ck)
    h0 = captain(host)
    print('  host:', h0[3], flush=True)
    # 1. the host's captain gets a captain's list
    print('  host:', host.call('coop.captain plant')['out'].strip(), flush=True)
    h1 = captain(host)
    ck(h1[0], f"host: its captain {h1[1]} has a captain's property list (the game's CaptainProps_Get): {h1[3][:90]}")
    said, _, _ = save_taken(host, 30)
    print('  host:', said[:80], flush=True)
    time.sleep(3)
    # 2. the guest's game has it
    t0, got = time.time(), None
    while time.time() - t0 < 60:
        g = captain(guest)
        if g[0]:
            got = g
            break
        time.sleep(2)
    for who, c in (('host', host), ('guest', guest)):
        print(f'  {who}:', ' | '.join(lists(c)), flush=True)
    g = captain(guest)
    print('  guest:', g[3], flush=True)
    ck(g[1] == h1[1], f"the guest's empire has the host's captain ({g[1]} / {h1[1]})")
    ck(got is not None, "the guest's game has the host captain's property list (CaptainProps_Get finds it)")
    # 3. the captain panel
    n0 = g[2]
    accept_route(guest, ck)
    g2 = captain(guest)
    print('  guest after Accept:', g2[3], flush=True)
    ck(n0 >= 0 and g2[2] == n0, f"the guest's captain screen (Accept) got the captain's item list (no null list: items "
                                f"none {n0} -> {g2[2]})")
    # the rank badge (a real click), the panel's picture, closed again
    guest.call(f'move {BADGE[0]} {BADGE[1]}')
    time.sleep(0.5)
    guest.call(f'click {BADGE[0]} {BADGE[1]}')
    if ck(fz.wait(lambda: play_space.captain_open(guest), 10), 'guest: the rank badge opened the captain screen'):
        time.sleep(2)
        keyframe('captain_items', "the guest's captain screen (rank badge): the Equipment panel shows the host captain's "
                 "item limits (locked items greyed), not every item available", {'guest': guest})
        play_space.close_captain(guest)
    g3 = captain(guest)
    ck(g3[2] == n0, f"the guest's captain screen (rank badge) got the captain's item list (items none {n0} -> {g3[2]})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
