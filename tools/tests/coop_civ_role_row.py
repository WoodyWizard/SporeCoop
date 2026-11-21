#!/usr/bin/env python3
"""Civ: a guest that plays the host's species and has own creatures picks a civilization role in the co-op window and
keeps the host's species (BUG-1403; the owner's decision: two rows).

Since BUG-1289 (creations.late_species) such a guest sees "My creature: the host's species (in My Creations: N) < >" in
Civ and the arrows pick a creature; the "My role: ... < >" row was gone, so it could pick no role without first giving
up the host's species. With `core.ui_civ_two_rows` the window shows both rows at once: the creature row one line up
(creationup, its arrows cupprev / cupnext), the role row under it in its old place (creation, cprev / cnext).

civ_npc fixture (the guest has own creatures and plays the host's species, as coop_late_species.py --civ):
  1. the guest's window (F9 state, `coop.uistate 1`): "My creature: ..." with < > and "My role: ..." with < > at once;
     the two rows and their arrows do not overlap; key frame;
  2. the role row's ">" (real clicks, 1 to 3): within 15 s the host's game has that role for the guest (`coop.role` on
     the host) and the guest's own; the guest's creature is unchanged (it still plays the host's species);
  3. the creature row's ">" (a real click): the guest's creature changes (`coop.creations` mine), its role stays the
     one picked in 2 on both games.
SPORE_FIX_OFF=core.ui_civ_two_rows (suite civ_role_row_off, the twin): no role row under the creature row: step 1
FAILs (rel/0.7.16 before the fix).

  SPORE_LANE=4 python3 tools/tests/coop_civ_role_row.py
"""
import re
import time

import lane
from coop_late_species import mine, own_list
from coop_ui import Ui
from harness import field
from twoinst import Checks, Console, keyframe, start


def role_of(con, pid=1):
    return field(con.call('coop.role')['out'], rf'(?m)^role {pid} .* (\w+)$') or '?'


def overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def rows(s):
    """(creature row text, role row text) of a uistate, '' for a row that does not show with its arrows"""
    up = s.get('creationup')
    crow = up['text'] if isinstance(up, dict) and up['vis'] and s['cupnext']['vis'] and s['cupprev']['vis'] else ''
    rrow = s.get('creation', '') if s['cnext']['vis'] and s['cprev']['vis'] else ''
    return crow, rrow


def wait_role(cons, want, secs):
    t0 = time.time()
    got = {}
    while time.time() - t0 < secs:
        got = {r: role_of(c) for r, c in cons.items()}
        if all(v == want for v in got.values()):
            return got, True
        time.sleep(0.5)
    return got, False


def test(host, guest, ck):
    cons = {'host': host, 'guest': guest}
    time.sleep(4)
    own = own_list(guest)
    m0 = mine(guest)
    if not ck(len(own) >= 2 and m0 not in own, f'the guest has creatures of its own ({own[:4]}) and plays the host\'s '
                                               f'species (mine {m0})'):
        return
    if role_of(guest) != 'none':  # a role from an earlier test of the session: the guest starts without one
        guest.call('coop.role 0')
        wait_role(cons, 'none', 10)
    role0 = role_of(host)
    print(f'  guest: mine {m0}, role on the host {role0}, on the guest {role_of(guest)}', flush=True)

    # 1. both rows at once
    gu = Ui(guest, 'guest')
    guest.call('coop.uistate 1')
    try:
        st = gu.wait(lambda s: s['open'] == '1' and rows(s)[0].startswith('My creature') and
                     rows(s)[1].startswith('My role'), 30, 'the creature row and the role row')
        if st is None:
            st = gu.state()
            crow, rrow = rows(st)
            ck(False, f'the guest\'s window shows "My creature: ... < >" and under it "My role: ... < >" (creature row '
                      f'{crow!r}, role row {rrow!r}, row {st.get("creation")!r})')
            return
        crow, rrow = rows(st)
        print(f'  guest window: {crow!r} / {rrow!r}', flush=True)
        ck(True, f'the guest\'s window shows both rows ({crow!r}, {rrow!r})')
        ups = [st[n]['rect'] for n in ('creationup', 'cupprev', 'cupnext')]
        downs = [st[n]['rect'] for n in ('cprev', 'cnext')]
        ck(all(u[3] <= d[1] for u in ups for d in downs) and not any(overlap(u, d) for u in ups for d in downs) and
           not overlap(st['cupprev']['rect'], st['cupnext']['rect']),
           f'the creature row is above the role row and no control overlaps another ({ups}, {downs})')
        keyframe('civ_two_rows', 'Civ, the guest\'s co-op window: "My creature: the host\'s species (in My Creations: N) '
                                 '< >" and under it "My role: none - pick one < >", both rows whole, the chat above '
                                 'with 3 lines; the host\'s screen plays on', settle=0)

        # 2. the role row's arrows pick a role
        n = 1 + (int(time.time()) % 3)
        for _ in range(n):
            gu.click('cnext')
        time.sleep(1)
        st = gu.state()
        shown = re.search(r'My role: (\w+)', st.get('creation', ''))
        want = shown.group(1) if shown else '?'
        got, ok = wait_role(cons, want, 15)
        print(f'  after {n} x role ">": window {st.get("creation")!r}, roles {got}, mine {mine(guest)}', flush=True)
        ck(want not in ('?', 'none', role0), f'the role row\'s ">" picked a role ({want}, before {role0})')
        ck(ok, f'the host\'s game has the guest\'s role {want} (and the guest\'s own: {got})')
        ck(mine(guest) == m0, f'the role row\'s arrows leave the guest\'s creature alone (mine {mine(guest)}, was {m0})')
        ck(rows(st)[0].startswith('My creature'), f'the creature row still shows ({rows(st)[0]!r})')

        # 3. the creature row's arrows pick a creature, not a role
        gu.click('cupnext')
        t0 = time.time()
        while time.time() - t0 < 10 and mine(guest) == m0:
            time.sleep(0.5)
        m1 = mine(guest)
        time.sleep(2)
        got2 = {r: role_of(c) for r, c in cons.items()}
        print(f'  after the creature row\'s ">": mine {m1}, roles {got2}', flush=True)
        ck(m1 != m0 and m1 in own, f'the creature row\'s ">" picked an own creature (mine {m0} -> {m1})')
        ck(all(v == want for v in got2.values()), f'the creature row\'s arrows leave the role alone ({want}: {got2})')
        keyframe('civ_two_rows_picked', f'Civ, the guest\'s co-op window after both picks: "My creature: <an own '
                                        f'creature>" and under it "My role: {want}"', settle=0)
    finally:
        guest.call('coop.uistate 0')


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    try:
        test(host, guest, ck)
    finally:
        try:  # the guest's lane profile as the fixture leaves it (no choice, no record of this world)
            g = Console(lane.GUEST)
            print('  guest:', g.call('coop.creations unchoose creature')['out'].strip(), '|',
                  g.call('coop.creations worlds set creature 0')['out'].strip().splitlines()[0], flush=True)
        except OSError:
            pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
