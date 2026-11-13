#!/usr/bin/env python3
"""Tribe: a click on another player's member tells the guest whose member it is (BUG-1087).

The player tribe is split between the players (docs/09-coop-design.md, Tribe): a guest selects and leads only its own
members. A click, a box or a roster card on the host's member selected nothing and said nothing (h_SetSelected): a
Discord player said "the guest cannot control its units".

Two games on the tribe fixture, 2 more members (so both players own some).
1. On the guest's screen (`coop.targets objs`) a member the host owns (`coop.tribe` owner=0): a real left click on it.
   The member stays unselected on the guest's game, and the guest's notification panels (the game's own windows,
   `coop.status` toast lines) show "That member is Alice's ...". A key frame of the guest.
2. A real box over an own member selects it (the guest's own members still answer), with no further note.
Without the fix (SPORE_FIX_OFF=tribe.foreign_note) the click is silent: step 1 FAILS.

  SPORE_LANE=7 python3 tools/tests/coop_tribe_foreign_click.py
"""
import re
import time

from play_tribe import screen, tribe_state
from twoinst import Checks, game_runs, keyframe, start

W, H = 1440, 1080


def toasts(con):
    return re.findall(r"^toast \S+ \([^)]*\) '([^']*)'", con.call('coop.status')['out'], re.M)


def visible(objs, ids):
    return [o for o in objs if o[0].startswith('member') and int(o[0][6:]) in ids and 60 < o[1] < W - 80
            and 120 < o[2] < H - 200]


def find_on_screen(guest, ids, st, secs=30):
    """a member of `ids` drawn on the guest's screen (the fixture's view is on the village; members walk about)"""
    t0 = time.time()
    objs = visible(screen(guest), ids)
    while not objs and time.time() - t0 < secs:
        time.sleep(1)
        objs = visible(screen(guest), ids)
    return objs


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip()[:80], flush=True)
    time.sleep(8)
    st = tribe_state(guest)
    hosts = [i for i, v in st.items() if v['tribe'] == 'p' and v['owner'] == 0 and v['hp'] > 0]
    mine = [i for i, v in st.items() if v['tribe'] == 'p' and v['owner'] == 1 and v['hp'] > 0]
    print(f'  guest\'s view of the player tribe: host\'s {hosts}, own {mine}', flush=True)
    if not ck(hosts and mine, 'both players own members of the player tribe'):
        ck.faults(host=host, guest=guest)
        return ck.result()

    # 1. a click on the host's member
    objs = find_on_screen(guest, hosts, st)
    if not ck(objs, f'a member of the host on the guest\'s screen ({len(objs)})'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    k, x, y, _ = objs[0]
    guest.call(f'move {x} {y}')
    time.sleep(0.4)
    guest.call(f'click {x} {y}')
    t0, note = time.time(), []
    while time.time() - t0 < 4 and not note:
        note = [t for t in toasts(guest) if 'you lead only your own' in t]
        time.sleep(0.3)
    st2 = tribe_state(guest)
    sel = [i for i, v in st2.items() if v['sel'] and v['tribe'] == 'p' and v['owner'] != 1]
    print(f'  click on #{k[6:]} at ({x}, {y}): selected others {sel}, panels {toasts(guest)}', flush=True)
    ck(not sel, f'the host\'s member stays unselected on the guest\'s game ({sel})')
    ck(note and "Alice's" in note[0], f'the guest\'s screen says whose member it is ({note})')
    keyframe('foreign_click', 'the guest\'s Tribe with the panel "That member is Alice\'s: you lead only your own '
                              'members"', players={'guest': guest}, settle=0)

    # 2. an own member still answers a box
    time.sleep(1)
    own = visible(screen(guest), mine) or find_on_screen(guest, mine, st)
    got = []
    if own:
        _, x, y, _ = own[0]
        guest.call(f'drag {max(5, x - 35)} {max(65, y - 55)} {min(W - 65, x + 35)} {min(H - 130, y + 25)}')
        time.sleep(1.5)
        got = [i for i, v in tribe_state(guest).items() if v['sel'] and v['tribe'] == 'p' and v['owner'] == 1]
    print(f'  box over own #{own[0][0][6:] if own else None}: selected {got}', flush=True)
    ck(got, f'a box over an own member selects it ({got})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
