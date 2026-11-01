#!/usr/bin/env python3
"""Editors in the Cell and the Creature stage, the players' way (report 2026-10-24, item 4: "the
guest could not call a mate himself, had to close the game (or it crashed) and rejoin to see the
host's change; in Creature the guest kept seeing the host's creature from before its first
editor").

The guest starts without own creations (a player who never used the Creature Creator: it plays the
host world's species). Real input throughout: the mating-call button, swimming to the mate, the
vote's "I'm ready", the editors (a part moved / legs added, "done", cancel), the stage switch.
Only the mating-call button is unlocked by the console (`coop.pool mate button`, as collecting a
part does).

  1. Cell, guest: the mating call brings a mate that stays (the guest's sync removed it at once),
     reaching it opens the guest's own editor without a vote; cancel.
  2. Cell, host: the mating call, the vote, the guest's "I'm ready": the host's editor opens and no
     second vote starts (the game asked again right after the vote).
  3. Host edits and saves the cell: the guest gets the world and the host's new cell with its records
     (the bundle went out without them and the guest's game faulted building the puppet).
  4. Cell, guest: the mating call works again after the host's evolution.
  5. Creature: the host finishes the Cell stage, enters the Creature stage, the first editor (legs)
     through the vote; the guest sees the host's evolved creature, also after a rejoin.

  coop_editor_desync.py [--cell-only] [--old-wait]

--old-wait: the waits before BUG-0411 (part 4 pressed the heart 5 s after the guest showed the host's
new cell, a caption the only thing waited for): the presses fell into the guest's reload of the
host's world (galaxy menu, the hatch with the game's input lock) and part 4 failed about every
second run.
"""
import re
import sys
import time

import lane
from twoinst import Checks, shot, start

HEART = (721, 1018)    # the mating-call button of the cell HUD (shown once S+0x69 is set)
READY = (478, 747)     # "I'm ready" in the co-op window
DONE = (1416, 1058)    # the editors' green check (1440x1080 test window)
CANCEL = (1383, 1062)  # the editors' red cross
STAGE_BTN = (1396, 1063)  # the cell HUD button under the portrait: History, then the next stage
HISTORY_OK = (1415, 1055)
CAPTION_OK = (1410, 1048)
OLD_WAIT = '--old-wait' in sys.argv


def mode(con):
    return con.call('game.mode')['out']


def field(text, pat):
    m = re.search(pat, text, re.S)
    return m.group(1) if m else None


def mate(con):
    return con.call('coop.pool mate')['out']


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def hud_ready(con):
    """the cell HUD takes a click: in the Cell stage, no caption (cine=0) and the game's input lock off
    (after a load the cell hatches: the lock is on for up to ~10 s and a click on the HUD is lost)"""
    if 'Cell' not in mode(con) or 'cine=0' not in con.call('coop.pool')['out']:
        return False
    m = re.search(r'input_lock=(-?\d+)', con.call('coop.uistate')['out'])
    return bool(m) and int(m.group(1)) <= 0


def wait_reload(con, secs=40):
    """BUG-0411: the host shares its world again a few seconds after its editor (the save waits until
    the stage settled), the guest leaves the Cell stage and loads it. Wait for that load to end, so
    the next step does not run in the galaxy menu or the hatch. True: the guest left and came back."""
    t0 = time.time()
    left = False
    while time.time() - t0 < secs:
        m = mode(con)
        if 'Cell' not in m:
            left = True
        elif left:
            return True
        time.sleep(0.5)
    return left and 'Cell' in mode(con)


def mating_call(con, name, until):
    """press the mating-call button and swim to the mate (clicks at it) until `until()`;
    returns (mate seen alive 3 s after the call, reached)"""
    alive = False
    presses = 0
    for _ in range(3):  # a click right as the HUD comes back can be lost: a player clicks again
        if OLD_WAIT:  # before BUG-0411: a caption only (the hatch and a load were not waited for)
            wait(lambda: 'cine=0' in con.call('coop.pool')['out'], 30)
        else:
            wait(lambda: hud_ready(con), 40)
        con.call('coop.pool mate button')
        time.sleep(1)
        con.call('click %d %d' % HEART)
        presses += 1
        time.sleep(3)
        alive = 'alive=1' in mate(con)
        if alive:
            break
    print(f'  {name} mate 3 s after the call: {mate(con)} ({presses} presses)')
    gone = 0
    for _ in range(25):
        if until():
            return alive, True
        m = re.search(r'alive=1 .* at (-?\d+) (-?\d+)', mate(con))
        if not m:
            gone += 1
            # the mate went with a reload of the world (or never came): a player calls again
            if not OLD_WAIT and gone >= 3 and presses < 6 and hud_ready(con):
                con.call('coop.pool mate button')
                con.call('click %d %d' % HEART)
                presses += 1
                gone = 0
                time.sleep(2)
                print(f'  {name} called its mate again: {mate(con)}')
            time.sleep(1)
            continue
        gone = 0
        x, y = (min(max(int(v), 20), lim) for v, lim in ((m.group(1), 1420), (m.group(2), 1000)))
        con.call(f'click {x} {y}')
        time.sleep(2)
    return alive, bool(until())


def edit_cell(con, name):
    """move the second eye to the back (free), name the species if asked, done"""
    con.call('move 866 540')
    time.sleep(0.3)
    con.call('button 866 540 down')
    time.sleep(0.3)
    for x in (900, 940, 980):
        con.call(f'move {x} 552')
        time.sleep(0.2)
    con.call('button 980 552 up')
    time.sleep(1)
    con.call('click 720 1055')  # the species name field ("Name Your Species" on a first edit)
    time.sleep(1)
    con.call('click 720 810')
    con.call('text ' + name)
    time.sleep(0.5)
    con.call('click %d %d' % DONE)


def editor_ui(con):
    """the editor's interface is up (a stuck editor showed only the creature: 2 UI windows)"""
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


def main():
    cell_only = '--cell-only' in sys.argv
    host, guest = start('cell', guest_creations=False)
    ck = Checks()

    # 1. the guest's own mating call in the shared pool
    in_editor = lambda: 'Editor' in mode(guest)
    alive, reached = mating_call(guest, 'guest', in_editor)
    ck(alive, "the guest's mate stays in the pool (it was removed as a stray cell)")
    ck(reached, f"the guest reaches its mate and its own cell editor opens ({mode(guest)})")
    ck('vote none' in host.call('coop.vote')['out'], 'no vote for a guest evolving its own cell')
    if reached:
        time.sleep(3)
        guest.call('click %d %d' % CANCEL)
        ck(wait(lambda: 'Cell' in mode(guest), 30), 'the guest leaves its editor (cancel)')
        time.sleep(8)  # the cell hatches again

    # 2. the host's mating call: the vote, then one editor
    host_vote = lambda: 'vote active' in host.call('coop.vote')['out'] or 'Editor' in mode(host)
    alive, reached = mating_call(host, 'host', host_vote)
    ck(reached, 'the host reaches its mate: the evolution vote starts')
    st = wait(lambda: guest.call('coop.uistate')['out'], 10)
    ck(st and "ready 380 728 580 768 1 'I'm ready'" in st, "the guest gets the vote with an I'm ready button")
    guest.call('click %d %d' % READY)
    ck(wait(lambda: 'Editor' in mode(host), 30), 'the host editor opens after the vote')
    second = False
    for _ in range(8):  # the game asks again while the mate is near (a race: not every time)
        second |= 'vote active' in host.call('coop.vote')['out']
        time.sleep(1)
    print('  host:', host.call('coop.vote')['out'].replace('\n', ' | '))
    ck(not second, 'no second vote while the host is in the editor')
    shot(host, 'editor_desync_cell_editor.png')

    # 3. the host saves a new cell: the guest gets it with its records and keeps running
    edit_cell(host, 'Alicecell')
    ck(wait(lambda: 'Cell' in mode(host), 30), 'the host is back in the pool')
    got = wait(lambda: field(guest.call('coop.creations')['out'], r"player 0 cell: ([0-9A-F]{8})/\S+ 'Alicecell'"), 60)
    ck(got, f"the guest got the host's new cell with its records ({got})")
    t0 = time.time()
    seen = None
    while time.time() - t0 < 90:
        gs = guest.call('coop.status')['out']
        seen = field(gs, r"remote 0 'Alice'.*?species=([0-9A-F]{8})")
        if got and seen == got and 'Cell' in mode(guest):
            break
        time.sleep(2)
    print(f'  guest shows Alice as {seen}')
    ck(got and seen == got, "the guest shows the host's new cell")
    gc = guest.call('coop.creations')['out']
    ck('unusable=0' in gc, 'the guest got no creation it cannot build (' + gc.splitlines()[0] + ')')
    print('  host:', host.call('coop.creations')['out'].splitlines()[0])
    shot(guest, 'editor_desync_cell_guest.png')

    # 4. the guest can still call its mate (after the world the host shares after its editor loaded)
    if OLD_WAIT:
        time.sleep(5)
    else:
        print(f'  guest loaded the host\'s world after its evolution: {wait_reload(guest)} ({mode(guest)})')
    alive, reached = mating_call(guest, 'guest', in_editor)
    ck(reached, "after the host's evolution the guest's own editor still opens")
    if reached:
        time.sleep(3)
        guest.call('click %d %d' % CANCEL)
        wait(lambda: 'Cell' in mode(guest), 30)
        time.sleep(8)

    if not cell_only:
        # 5. the end of the Cell stage, the Creature stage and its first editor
        host.call('coop.pool food 1000')
        t0 = time.time()
        while time.time() - t0 < 120 and 'Creature' not in mode(host):
            pool = host.call('coop.pool')['out']
            if 'cine=1' in pool:
                host.call('click %d %d' % CAPTION_OK)
                time.sleep(3)
                continue
            if time.time() - t0 > 25:  # the cinematic is over: History, then its check = next stage
                host.call('click %d %d' % STAGE_BTN)
                time.sleep(3)
                host.call('click %d %d' % HISTORY_OK)
            time.sleep(4)
        ck('Creature' in mode(host), f'the host entered the Creature stage ({mode(host)})')
        ck(wait(lambda: 'vote active' in host.call('coop.vote')['out'], 30), 'its first editor waits for the vote')
        ck(wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 90), 'the guest follows')
        k0 = field(host.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')
        time.sleep(3)
        guest.call('click %d %d' % READY)
        ck(wait(lambda: 'Editor' in mode(host), 40), 'the host is in the creature editor')
        time.sleep(6)
        add_legs(host)
        shot(host, 'editor_desync_legs.png')
        host.call('click %d %d' % DONE)
        if wait(lambda: 'Editor' in mode(guest), 20):  # the guest's own editor (the vote opened it)
            ui = wait(lambda: editor_ui(guest), 40)
            shot(guest, 'editor_desync_guest_editor.png')
            ck(ui, "the guest's own editor shows its interface (parts, done, cancel)")
            time.sleep(2)
            guest.call('click %d %d' % CANCEL)
        ck(wait(lambda: 'Creature' in mode(host), 40), 'the host is back in the Creature stage')
        k1 = wait(lambda: (lambda k: k if k and k != k0 else None)(
            field(host.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')), 40)
        print(f'  host species {k0} -> {k1}')
        ck(k1, 'the host plays its evolved creature')

        def guest_sees_host():
            gc = guest.call('coop.creations')['out']
            want = field(gc, r"player 0 creature: ([0-9A-F]{8})")
            seen = field(guest.call('coop.status')['out'], r"remote 0 'Alice'.*?species=([0-9A-F]{8})")
            av = field(guest.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')
            return want, seen, av

        def evolved_seen():
            want, seen, av = guest_sees_host()
            return (want, seen, av) if want and seen == want and want != k0 and av == k1 else None

        r = wait(evolved_seen, 120, 2)
        print('  guest: host creature %s, puppet %s, own avatar %s' % (r or guest_sees_host()))
        ck(r, "the guest sees the host's evolved creature and plays the evolved species itself")
        shot(guest, 'editor_desync_creature_guest.png')
        # a rejoin keeps it
        guest.call('coop.leave')
        time.sleep(3)
        guest.call(lane.JOIN)
        time.sleep(10)
        r = wait(lambda: 'Creature' in mode(guest) and evolved_seen(), 120, 2)
        print('  after a rejoin: host creature %s, puppet %s, own avatar %s' % (r or guest_sees_host()))
        ck(r, "after a rejoin the guest still sees the host's evolved creature")
        shot(guest, 'editor_desync_rejoin_guest.png')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
