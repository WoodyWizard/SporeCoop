#!/usr/bin/env python3
"""BUG-0278: a new guest (no own creations) in the first Creature editor (Discord report
2026-10-29: "I spawn with the phase 1 cell without legs, the host plays normally, can't change my
character").

The players' way from the Cell stage: the host fills its food bar, History and its check start the
Creature stage, the first creature editor waits for the vote, the guest clicks "I'm ready", the
host adds legs and saves. The vote opened the guest's own editor on the creature it plays: the
host's creature from before the editor (legless). The guest then does what a new player does:

  --guest done    "done" without changing anything (default)
  --guest cancel  the red cross
  --guest new       adds legs, "done", and "Save New" in the game's save dialog
  --guest replace   adds legs, "done", and "Replace" (the creation it edited)
  --guest dontsave  adds legs, "done", and "Don't Save"
  (the own editor asks "Save 'X' as a new creation or replace 'X' before exiting?" after an edit)

Checked on the guest's game (its avatar's species key) and on the host's (the species of the
guest's puppet), after the editors, after the guest's rejoin and after the host's next editor:
  * done / cancel: the guest plays the host's evolved (legged) species, not the pre-editor one;
  * new / replace: the guest plays its own new creature (it designed it);
  * dontsave: as cancel;
  * every case: the guest can still change its creature in its own next editor (it opens on the
    creature the guest plays).

  coop_legless_guest.py [--guest done|cancel|new|replace|dontsave]
"""
import re
import sys
import time

import lane
from twoinst import Checks, shot, start

READY = (478, 747)     # "I'm ready" in the co-op window
DONE = (1416, 1058)    # the editors' green check (1440x1080 test window)
CANCEL = (1383, 1062)  # the editors' red cross
STAGE_BTN = (1396, 1063)  # the cell HUD button under the portrait: History, then the next stage
HISTORY_OK = (1415, 1055)
CAPTION_OK = (1410, 1048)
# the editor's exit dialog after an edit ("Save 'X' as a new creation or replace 'X' before exiting?")
SAVE_DIALOG = {'dontsave': (510, 604), 'cancel': (612, 604), 'replace': (830, 604), 'new': (926, 604)}
QUIT = (944, 604)  # "Exiting will cause your changes to be lost": Quit
OWN = ('new', 'replace')  # the guest keeps a creature it designed


def mode(con):
    return con.call('game.mode')['out']


def field(text, pat):
    m = re.search(pat, text or '', re.S)
    return m.group(1) if m else None


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def editor_ui(con):
    tree = con.call('coop.ui tree 4')['out']
    return tree.count('Window') > 15


def add_legs(con):
    """drag the first leg of the palette onto the creature (the editor mirrors it)"""
    con.call('move 275 300')
    time.sleep(0.3)
    con.call('button 275 300 down')
    time.sleep(0.3)
    for x in (400, 600, 800, 950, 1000):
        con.call(f'move {x} 560')
        time.sleep(0.2)
    time.sleep(0.5)
    con.call('button 1000 570 up')
    time.sleep(1)


def add_part(con):
    """drag the first part of the palette (a mouth in the later editors) onto the creature's body"""
    con.call('move 86 170')
    time.sleep(0.3)
    con.call('button 86 170 down')
    time.sleep(0.3)
    for x, y in ((300, 250), (600, 400), (850, 500), (930, 560)):
        con.call(f'move {x} {y}')
        time.sleep(0.2)
    time.sleep(0.5)
    con.call('button 930 560 up')
    time.sleep(1)


def avatar_key(con):
    return field(con.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')


def puppet_of_guest(host):
    """the species the host's game shows for the guest (its puppet)"""
    return field(host.call('coop.status')['out'], r"remote 1 'Bob'.*?species=([0-9A-F]{8})")


def host_key_on_guest(guest, hk):
    """the key the guest's game uses for a host creation (remapped for the host's own creations)"""
    return field(guest.call('coop.creations')['out'], rf'host species {hk} = ([0-9A-F]{{8}}) here') or hk


def guest_mine(guest):
    return field(guest.call('coop.creations')['out'], r"mine creature: ([0-9A-F]{8})")


def main():
    how = sys.argv[sys.argv.index('--guest') + 1] if '--guest' in sys.argv else 'done'
    host, guest = start('cell', guest_creations=False)
    ck = Checks()

    # the end of the Cell stage the players' way
    host.call('coop.pool food 1000')
    t0 = time.time()
    while time.time() - t0 < 150 and 'Creature' not in mode(host):
        if 'cine=1' in host.call('coop.pool')['out']:
            host.call('click %d %d' % CAPTION_OK)
            time.sleep(3)
            continue
        if time.time() - t0 > 25:  # the cinematic is over: History, then its check = next stage
            host.call('click %d %d' % STAGE_BTN)
            time.sleep(3)
            host.call('click %d %d' % HISTORY_OK)
        time.sleep(4)
    ck('Creature' in mode(host), f'the host entered the Creature stage ({mode(host).strip()})')
    ck(wait(lambda: 'vote active' in host.call('coop.vote')['out'], 30), 'its first editor waits for the vote')
    ck(wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 120), 'the guest follows')
    k0 = avatar_key(host)
    g0 = avatar_key(guest)
    print(f'  host pre-editor species {k0}, guest avatar {g0}, guest mine {guest_mine(guest)}')
    time.sleep(3)
    guest.call('click %d %d' % READY)
    ck(wait(lambda: 'Editor' in mode(host), 40), 'the host is in the creature editor')
    time.sleep(6)
    add_legs(host)
    shot(host, 'legless_host_legs.png')
    host.call('click %d %d' % DONE)

    # the guest's own editor (the vote opened it on the creature it plays)
    own = wait(lambda: 'Editor' in mode(guest), 120)
    ck(own, "the guest's own editor opens after the vote")
    if own:
        ui = wait(lambda: editor_ui(guest), 40)
        ck(ui, "the guest's own editor shows its interface")
        time.sleep(3)
        shot(guest, f'legless_guest_editor_{how}.png')
        if how in SAVE_DIALOG:
            add_legs(guest)
            guest.call('click %d %d' % DONE)
            time.sleep(3)
            shot(guest, f'legless_guest_dialog_{how}.png')
            guest.call('click %d %d' % SAVE_DIALOG[how])
        elif how == 'cancel':
            guest.call('click %d %d' % CANCEL)
            time.sleep(3)
            if 'Editor' in mode(guest):  # "your changes will be lost" (seen: the editor showed changes)
                shot(guest, 'legless_guest_cancel_dialog.png')
                guest.call('click %d %d' % QUIT)
        else:
            guest.call('click %d %d' % DONE)
        left = wait(lambda: 'Creature' in mode(guest), 30)
        if not left:  # "done" refused (an error shown): a player then closes it
            print('  the guest editor stayed open after the click: ' + mode(guest).strip())
            shot(guest, f'legless_guest_editor_stays_{how}.png')
            guest.call('click %d %d' % CANCEL)
            time.sleep(3)
            guest.call('click %d %d' % QUIT)  # the editor asks before it drops changes
            left = wait(lambda: 'Creature' in mode(guest), 30)
        ck(left, f'the guest leaves its editor ({how})')
    ck(wait(lambda: 'Creature' in mode(host), 60), 'the host is back in the Creature stage')
    k1 = wait(lambda: (lambda k: k if k and k != k0 else None)(avatar_key(host)), 60)
    print(f'  host species {k0} -> {k1}')
    ck(k1, 'the host plays its evolved creature')

    def state():
        gk = avatar_key(guest)
        pk = puppet_of_guest(host)
        evolved_here = host_key_on_guest(guest, k1) if k1 else None
        pre_here = host_key_on_guest(guest, k0) if k0 else None
        return dict(guest_avatar=gk, host_shows=pk, mine=guest_mine(guest), evolved_here=evolved_here,
                    pre_here=pre_here)

    def good():
        s = state()
        if not s['guest_avatar'] or not s['host_shows']:
            return None
        if how in OWN:  # its own new creature: neither of the host's
            ok = s['guest_avatar'] not in (k0, k1, s['evolved_here'], s['pre_here'], g0) and s['host_shows'] not in (k0, k1)
        else:  # the host's evolved species on both machines (the guest may use its key or a remapped one)
            ok = s['guest_avatar'] in (k1, s['evolved_here']) and s['host_shows'] == k1
        return s if ok else None

    time.sleep(10)
    r = wait(good, 120, 3)
    print('  after the editors: %s' % (r or state()))
    want = 'its own new creature' if how in OWN else "the host's evolved species, not the legless one"
    ck(r, f'guest ({how}): it plays {want}, on its own screen and on the host\'s')
    shot(guest, f'legless_guest_after_{how}.png')
    shot(host, f'legless_host_after_{how}.png')

    # a rejoin keeps it
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN)
    time.sleep(10)
    r = wait(lambda: 'Creature' in mode(guest) and good(), 150, 3)
    print('  after a rejoin: %s' % (r or state()))
    ck(r, f'after a rejoin the guest still plays {want}')

    # the guest can change its creature: its next own editor opens on the creature it plays
    # (evolution editor through the game's own entry, which the mod turns into a vote)
    before = avatar_key(guest)
    guest.call('coop.vote editor')
    opened = wait(lambda: 'Editor' in mode(guest), 60)
    ck(opened, "the guest's next own editor opens")
    if opened:
        wait(lambda: editor_ui(guest), 40)
        time.sleep(3)
        add_part(guest)
        shot(guest, f'legless_guest_next_editor_{how}.png')
        guest.call('click %d %d' % DONE)
        time.sleep(3)
        if 'Editor' in mode(guest):  # the save dialog after an edit
            guest.call('click %d %d' % SAVE_DIALOG['new'])
        ck(wait(lambda: 'Creature' in mode(guest), 40), 'the guest saves its edit')
        changed = wait(lambda: (lambda k: k if k and k != before else None)(avatar_key(guest)), 120, 2)
        seen = wait(lambda: (lambda p: p if changed and p and p not in (k0, k1, before) else None)(
            puppet_of_guest(host)), 90, 2)
        print(f'  next editor: guest {before} -> {changed}, host shows {seen}')
        ck(changed and seen, 'the guest changed its creature and the host shows the new one')

    # and back to the world's own (the host's creature) with the co-op window's arrows
    # (`coop.creations prev` is what the arrow runs)
    back = None
    for _ in range(4):
        out = guest.call('coop.creations prev creature')['out']
        if re.search(r"mine creature: [0-9A-F]{8} ''", guest.call('coop.creations')['out']):
            back = True
            break
    print('  choices: ' + (field(guest.call('coop.creations')['out'], r'(mine creature: [^\n]*)') or '?'))
    ck(back, "the co-op window offers the world's own creature")
    if back:
        def world_again():
            s = state()
            return s if s['guest_avatar'] in (k1, s['evolved_here']) and s['host_shows'] == k1 else None
        r = wait(world_again, 90, 3)
        print('  world\'s own: %s' % (r or state()))
        ck(r, "after choosing the world's own the guest plays the host's evolved creature on both screens")

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
