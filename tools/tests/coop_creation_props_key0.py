#!/usr/bin/env python3
"""A profile with a creation property list under key 0 can still name the game's own end-of-Tribe editors (BUG-1328,
docs/re/creations.md#creations-props-key0). The 0.7.15 line's form of rel/0.7.16's test of the same name (there: a
guest's own arrival editors in Civ, which 0.7.15 does not have).

"Name Your Captain" (Space) writes the new captain's property list with the name the player gave; when the world's
species cannot be copied the captain key is all zero (BUG-1111, GitHub #202) and the game writes that list under
{0, 0x00B1B104, 0x0000E500} into the profile's Pollination.package, where it stays. Every editor opened without a
creation key reads that list when it opens: the game's own City Hall and vehicle editors at the end of Tribe. Its name
makes the name panel read-only, without the name field and without the dice, and the editor never saves ("You must give
your creation a name before you can save"); the end of Tribe's City Hall editor has no red cross: such a player cannot
leave the Tribe stage.

tribe fixture, the route of coop_tribe_civ.py (the players' way):
  1. the host's game gets a list under key 0 written the way the captain dialog writes it (`coop.creationprops plant
     Breer`); its goal bar is filled (`coop.tribe advance`), the advance button, the stage card, the caption, the
     guest's "I'm ready".
  2. the host's City Hall editor (the game's own, no creation key): three parts dragged onto the plot (real input),
     "done" without a name: the name panel opens. PASS: the panel shows its name field (0xC7CEB1BD) and the dice
     (0x0552C901) (the game's own windows, `coop.ui where`); key frame of the panel. Then the dice, "done": the editor
     saves and leaves. The vehicle editor (after the second vote) the same way; the host's game reaches Civ.
  3. the list is removed again (`coop.creationprops remove`: the lane profile's Pollination.package as before).
SPORE_FIX_OFF=game.creation_props_key0 (the twin, creation_props_key0_off): step 2 FAILs at the City Hall editor (no
name field, no dice; the editor stays, the game's end of Tribe has no way out of it).

  SPORE_LANE=9 python3 tools/tests/coop_creation_props_key0.py
"""
import re
import time

import twoinst
import uifind
from coop_campaign import EDITOR_DICE, EDITOR_DONE, TRIBE_EDITOR_PARTS, drag_slow, editor_click
from coop_tribe_civ import ADVANCE, CAPTION_OK, CARD_OK, busy, vote_active, where
from coop_ui import Ui
from harness import mode, waiter
from twoinst import Checks, Console, keyframe, shot, start
import lane

SWITCH = 'game.creation_props_key0'
NAME_FIELD, DICE = 0xC7CEB1BD, 0x0552C901  # the name panel's text field and random-name button (UI::EditorNamePanel)
wait = waiter(step=1.0, final=True)


def props(con):
    out = con.call('coop.creationprops')['out'] or ''
    m = re.search(r'creationprops: (.*)', out)
    return dict(re.findall(r'(\w+)=(\S+)', m.group(1))) if m else {}


def in_editor(con):
    return 'Editor' in mode(con)


def name_panel(con, what):
    """three parts, done without a name: (name field shown, dice shown) of the name panel the game opened (shown: through
    visible parents, `coop.ui where`)"""
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
    print(f'  host {what}: name panel: name field {shown[0]}, dice {shown[1]}; still in the editor {in_editor(con)}',
          flush=True)
    return shown


def leave_named(con, tries=4):
    """the dice's name, done; again (with three more parts) when the editor is still up"""
    for n in range(tries):
        if not in_editor(con):
            return True
        if n and n % 2 == 0:
            for (sx, sy), (tx, ty) in TRIBE_EDITOR_PARTS:
                drag_slow(con, sx, sy, tx, ty, 1.0 + n // 2)
        editor_click(con, *EDITOR_DICE)
        editor_click(con, *EDITOR_DONE, settle=0)
        wait(lambda: not in_editor(con), 8)
    return not in_editor(con)


def ready_after_vote(host, gu, ck, what):
    gu.wait(lambda s: s['ready']['vis'], 15, 'ready button')
    gu.click('ready')
    ok = wait(lambda: in_editor(host), 90)
    ck(ok, f'the host\'s game opens its {what} editor after the vote')
    return ok


def to_city_hall(host, guest, ck):
    """coop_tribe_civ's way into the end of Tribe: the advance button, the card, the caption, the vote"""
    print(' ', host.call('coop.tribe advance')['out'].strip(), flush=True)
    r = wait(lambda: where(host, ADVANCE), 15)
    if not ck(r, 'the HUD shows the advance button (the goal bar is full)'):
        return False
    host.call('click %d %d' % ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2))
    time.sleep(5)
    t0 = time.time()
    while time.time() - t0 < 120 and not vote_active(host) and not in_editor(host):
        b = busy(host)
        if 'cinematic' not in b:
            host.call('click %d %d' % CARD_OK)
        elif 'paused' in b:
            time.sleep(2)
            host.call('click %d %d' % CAPTION_OK)
        time.sleep(3)
    if not ck(vote_active(host) or in_editor(host), "the game's City Hall editor request starts the vote"):
        print(host.call('coop.vote')['out'], flush=True)
        return False
    return in_editor(host) or ready_after_vote(host, Ui(guest, 'guest'), ck, 'City Hall')


def main():
    host, guest = start('tribe')
    ck = Checks()
    off_twin = SWITCH in twoinst.fix_off_names()
    try:
        run(host, guest, ck, off_twin)
    finally:
        try:  # the host's lane profile as it was
            h = Console(lane.HOST)
            print('  host:', h.call('coop.creationprops remove')['out'].strip().replace('\n', ' | '), flush=True)
        except OSError:
            pass
    ck.faults(host=host, guest=guest)
    return ck.result()


def run(host, guest, ck, off_twin):
    time.sleep(4)
    ck('Tribe' in mode(host) and 'Tribe' in mode(guest), 'both games in Tribe')
    # 1. a list under key 0 in the host's game, written as the captain dialog writes it
    print('  host:', host.call('coop.creationprops plant Breer')['out'].strip().replace('\n', ' | '), flush=True)
    p = props(host)
    if not ck(p.get('key0_list') == '1', f'the host\'s game has a creation property list under key 0 ({p})'):
        return
    if not to_city_hall(host, guest, ck):
        shot(host, 'creation_props_no_editor.png')
        return

    # 2. the game's own City Hall and vehicle editors (no creation key): the name panel can name them
    gu = Ui(guest, 'guest')
    for step, what, kind in ((0, 'City Hall', 'cityhall'), (1, 'vehicle', 'vehicle')):
        if step == 1:  # the City Hall cinematic, then (TRG2CVG_ShowCityHall) the second vote
            t0 = time.time()
            while time.time() - t0 < 240 and not in_editor(host) and not vote_active(host) and 'Civ' not in mode(host):
                b = busy(host)
                if 'paused' in b and 'cinematic' in b:
                    host.call('click %d %d' % CAPTION_OK)
                time.sleep(3)
            if not in_editor(host):
                if not ck(vote_active(host), f'the second vote of the end of Tribe starts ({mode(host).strip()})'):
                    return
                if not ready_after_vote(host, gu, ck, what):
                    return
        time.sleep(6)
        field, dice = name_panel(host, what)
        keyframe(f'{kind}_name_panel', f'Tribe end: the host in the game\'s own {what} editor (three parts on the plot) '
                                       'after "done" without a name: the name panel is open with an empty name field '
                                       'and the dice left of it (the twin: no field, no dice, "You must give your '
                                       'creation a name")', settle=0)
        ck(field is not None and dice is not None,
           f'{what} editor: the name panel shows its name field and the dice although a list under key 0 exists '
           f'({props(host)})')
        if not (field and dice):
            shot(host, f'creation_props_{kind}_no_dice.png')
            return
        left = leave_named(host)
        ck(left, f'the host named its {what} with the dice and left the editor with a creation ({mode(host).strip()})')
        if not left:
            shot(host, f'creation_props_{kind}_stuck.png')
            return
        time.sleep(2)
    civ = wait(lambda: 'Civ' in mode(host), 300)
    ck(civ, f'the host\'s game leaves Tribe for the Civ stage ({mode(host).strip()})')
    p = props(host)
    ck(off_twin or int(p.get('refused', '0')) > 0, f'the guard answered the editors\' read of key 0 ({p})')


if __name__ == '__main__':
    raise SystemExit(main())
