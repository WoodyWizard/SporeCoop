#!/usr/bin/env python3
"""A profile with a creation property list under key 0 can still name a new creation in an editor (BUG-1328,
docs/re/creations.md#creations-props-key0).

"Name Your Captain" (Space) writes the new captain's property list with the name the player gave; when the world's
species cannot be copied the captain key is all zero (BUG-1111) and the game writes that list under
{0, 0x00B1B104, 0x0000E500} into the profile's Pollination.package, where it stays. Every editor opened without a
creation key (the end of Tribe's City Hall and vehicle editors, a guest's own arrival editors of WP-X1) reads that
list when it opens: its name makes the name panel read-only, without the name field and without the dice, and the
editor never saves ("You must give your creation a name before you can save"). Lab lane 5's guest profile had such a
list (named "Breer", from space_captain_none): its own City Hall editor at its Civ arrival never ended there, 9 of 9
runs (line_stages_outfit, line_stages_editors, civ_own_arrival), while lane 6 named it every time.

tribe fixture, the route of civ_own_arrival (the guest's own City Hall and vehicle editors after the live end of Tribe,
both without a creation key; not civ_late_arrival's restarted guest: its City Hall result faults in the Tribe goal
listener, BUG-1329):
  1. the guest chooses its own creature and gets a list under key 0 written the way the captain dialog writes it
     (`coop.creationprops plant Breer`); the host ends Tribe the players' way (its vote, its City Hall and vehicle
     editors), both games go to Civ.
  2. after its arrival the guest's own City Hall editor opens; three parts are dragged onto the plot (real input),
     "done" without a name: the name panel opens. PASS: the panel shows its name field (0xC7CEB1BD) and the dice
     (0x0552C901) (the game's own windows, `coop.ui where`); key frame of the panel. Then the dice, "done": the editor
     saves and leaves. The vehicle editor the same way; both results reach the guest's line.
  3. the list is removed again (`coop.creationprops remove`: Pollination.package as before).
SPORE_FIX_OFF=game.creation_props_key0 (the twin, creation_props_key0_off): step 2 FAILs (no name field, no dice, the
editor stays; it is left with its red cross).

  SPORE_LANE=6 python3 tools/tests/coop_creation_props_key0.py
"""
import re
import time

import twoinst
import uifind
from coop_campaign import EDITOR_CANCEL, EDITOR_DICE, EDITOR_DONE, EDITOR_QUIT, TRIBE_EDITOR_PARTS, drag_slow, editor_click
from coop_civ_own_arrival import arrival, tribe_to_civ
from coop_tribe_civ import CAPTION_OK, busy
from coop_ui import Ui
from play_civ import answer_specialty
from harness import mode, waiter
from twoinst import Checks, Console, keyframe, shot, start
import lane

SWITCH = 'game.creation_props_key0'
NAME_FIELD, DICE = 0xC7CEB1BD, 0x0552C901  # the name panel's text field and random-name button (UI::EditorNamePanel)
CITY_HALL = 0x99E92F05
wait = waiter(step=1.0, final=True)


def props(con):
    out = con.call('coop.creationprops')['out'] or ''
    m = re.search(r'creationprops: (.*)', out)
    return dict(re.findall(r'(\w+)=(\S+)', m.group(1))) if m else {}


def in_editor(con):
    return 'Editor' in mode(con)


def name_panel(con, who):
    """three parts, done without a name: (name field shown, dice shown) of the name panel the game opened (shown: through
    visible parents, `coop.ui where`; its coordinates are the layout's before the panel moved up, not the screen's)"""
    for (sx, sy), (tx, ty) in TRIBE_EDITOR_PARTS:
        drag_slow(con, sx, sy, tx, ty, 1.0)
    editor_click(con, *EDITOR_DONE)
    time.sleep(2)
    shown = (None, None)
    for _ in range(6):
        shown = (uifind.where(con, NAME_FIELD), uifind.where(con, DICE))
        if all(shown) or not in_editor(con):
            break
        time.sleep(1)
    print(f'  {who}: name panel: name field {shown[0]}, dice {shown[1]}; still in the editor {in_editor(con)}', flush=True)
    return shown


def leave_named(con, tries=4):
    """the dice's name, done; again (with three more parts) when the editor is still up"""
    for n in range(tries):
        if not in_editor(con):
            return True
        if n and n % 2 == 0:
            for (sx, sy), (tx, ty) in TRIBE_EDITOR_PARTS:
                drag_slow(con, sx, sy, tx, ty, 1.0 + n // 2)
        editor_click(con, *EDITOR_DICE)  # (the panel's own place: `coop.ui where` gives the unmoved layout's)
        editor_click(con, *EDITOR_DONE, settle=0)
        wait(lambda: not in_editor(con), 8)
    return not in_editor(con)


def leave_cancelled(con):
    for _ in range(3):
        if not in_editor(con):
            return
        editor_click(con, *EDITOR_CANCEL)
        editor_click(con, *EDITOR_QUIT)
        wait(lambda: not in_editor(con), 20)


def wait_editor(guest, host, secs):
    """until the guest's game is in an editor (answering what the Civ start shows on both games meanwhile)"""
    t0 = time.time()
    n = 0
    while time.time() - t0 < secs:
        if in_editor(guest):
            return True
        n += 1
        if n % 5 == 0:
            for con in (guest, host):
                if in_editor(con):
                    continue
                answer_specialty(con)
                b = busy(con)
                if 'paused' in b and 'cinematic' in b:
                    con.call('click %d %d' % CAPTION_OK)
        time.sleep(1)
    return in_editor(guest)


def main():
    host, guest = start('tribe')
    ck = Checks()
    off_twin = SWITCH in twoinst.fix_off_names()
    rec0 = (guest.call('coop.creations worlds').get('out') or '').strip()
    m = re.search(r'creature=(\w+)', rec0)
    rec_creature = m.group(1) if m else '00000000'
    try:
        run(host, guest, ck, off_twin)
    finally:
        try:  # the guest's lane profile as it was
            g = Console(lane.GUEST)
            print('  guest:', g.call('coop.creationprops remove')['out'].strip().replace('\n', ' | '), '|',
                  g.call('coop.creations unchoose creature')['out'].strip(), '|',
                  g.call(f'coop.creations worlds set creature {rec_creature}')['out'].strip(), flush=True)
        except OSError:
            pass
    ck.faults(host=host, guest=guest)
    return ck.result()


def run(host, guest, ck, off_twin):
    time.sleep(4)
    ck('Tribe' in mode(host) and 'Tribe' in mode(guest), 'both games in Tribe')
    mine = re.findall(r'^creature (\w{8}) ', guest.call('coop.creations list')['out'], re.M)
    if not ck(mine, f'the guest has creatures of its own ({len(mine)})'):
        return
    print('  guest:', guest.call(f'coop.creations choose creature {mine[0]}')['out'].strip(), mine[0], flush=True)

    # 1. a list under key 0 in the guest's game, written as the captain dialog writes it; the live end of Tribe
    print('  guest:', guest.call('coop.creationprops plant Breer')['out'].strip().replace('\n', ' | '), flush=True)
    p = props(guest)
    if not ck(p.get('key0_list') == '1', f'the guest\'s game has a creation property list under key 0 ({p})'):
        return
    if not tribe_to_civ(ck, host, guest, Ui(guest, 'guest')):
        return
    if not ck(wait(lambda: 'Civ' in mode(guest), 400), f'the guest\'s game arrives in the host\'s Civ world ({mode(guest).strip()})'):
        return
    t_arr = time.time()

    # 2. its own City Hall and vehicle editors (no creation key): the name panel can name them
    for step, what, kind in ((0, 'City Hall', 'cityhall'), (1, 'vehicle', 'vehicle')):
        opened = wait_editor(guest, host, 300)
        st, _, out = arrival(guest)
        if not ck(opened, f'the guest\'s own {what} editor opened ({time.time() - t_arr:.0f} s after its arrival; phase '
                          f'{st.get("phase")} why {st.get("why")})'):
            print('  guest:', out.strip(), flush=True)
            return
        if step == 0:
            ck(st.get('editors', '').startswith(f'{CITY_HALL:08X}'), f'the first is the City Hall editor ({st.get("editors")})')
        time.sleep(6)
        field, dice = name_panel(guest, what)
        keyframe(f'{kind}_name_panel', f'Civ: the guest in its own {what} editor (three parts on the plot) after "done" '
                                       'without a name: the name panel is open with an empty name field and the dice '
                                       'left of it (the twin: no field, no dice, "You must give your creation a name")',
                 settle=0)
        ck(field is not None and dice is not None,
           f'{what} editor: the name panel shows its name field and the dice although a list under key 0 exists '
           f'({props(guest)})')
        if not (field and dice):
            shot(guest, f'creation_props_{kind}_no_dice.png')
            leave_cancelled(guest)
            return
        left = leave_named(guest)
        ck(left, f'the guest named its {what} with the dice and left the editor with a creation ({mode(guest).strip()})')
        if not left:
            shot(guest, f'creation_props_{kind}_stuck.png')
            leave_cancelled(guest)
            return
        time.sleep(2)
    st, res, out = arrival(guest)
    print('  guest:', out.strip(), flush=True)
    ck(len(res) == 2 and all(r.get('seen') and not r.get('cancelled') for r in res.values()),
       f'both editors gave a design ({res})')
    p = props(guest)
    ck(off_twin or int(p.get('refused', '0')) > 0, f'the guard answered the editors\' read of key 0 ({p})')


if __name__ == '__main__':
    raise SystemExit(main())
