#!/usr/bin/env python3
"""Every stage: while the host's game is paused (its pause menu) the guest's game waits too, with a line (BUG-1142).

C3 of 0.7.15, owner decision 1: the host's explicit pause (P, the pause menu, the Sporepedia, a tutorial box) makes the
guests wait in every stage with the line "<host> paused the game"; not in Rivals mode; a guest's pause stops nobody.
Before, only Tribe did it (BUG-1086): in the other stages the guest played on against a world that stood.

Two games on the stage's fixture (--stage cell|creature|tribe|civ|space).
1. Before: the guest's game runs (Cell: its own cell swims to a point 40 m away, `coop.pool goto`, and moves; the
   other stages: the game's time runs, `coop.status` busy without "paused").
2. The host opens its pause menu (Esc, as a player).
3. Within 6 s the guest's game stands: Cell: its cell, sent on to swim again, moves less than 0.5 m in 3 s (the game's
   own cell position; the Cell stage has no game-time pause, its cell step stands as under the pause menu); the other
   stages: the guest's game time stands (busy: paused, the game's cGameTimeManager). Its notification panels show
   "<host> paused the game" (`coop.status` toast lines). A key frame of both games.
4. The host resumes (Resume Game): within 10 s the guest's game runs again (Cell: its cell moves).
--rivals (Space): Rivals mode on (`coop.space rivalry mode rivals`): the guest's game does NOT wait and shows no line.
--guest-pause: the guest opens its pause menu instead: the host's game runs on (no "paused" in its busy, no line).
--host-save (Tribe, BUG-1217): no pause at all: the host's own saves (an autosave, `coop.autosave milestone`, then the
world share's save for the guest that leaves and joins again in its copy of the world). The guest's game must not
stop for them: sampled ~10 times a second its game time never stands (busy without "paused"), the host-busy wait of
coop.status never starts (waits= stays) and no "<host>'s game stands" / "goes on" card shows. Without the fix
(SPORE_FIX_OFF=core.host_busy_save) the save's stand was told as a caption: the guest stopped for ~0.1 s with both
cards after every save.
Without the fix (SPORE_FIX_OFF=<stage>.busy_model; Creature: npc.busy_model; Tribe also tribe.host_pause_wait,
its own 0.7.14 wait) step 3 FAILS: the guest plays on.

  SPORE_LANE=9 python3 tools/tests/coop_host_pause.py --stage creature
"""
import argparse
import math
import re
import time

from harness import busy_list as busy
import lane
import uifind
from twoinst import Checks, game_runs, keyframe, start

HOST = 'Alice'
FIXTURE = {'cell': 'cell', 'creature': 'creature', 'tribe': 'tribe', 'civ': 'civ', 'space': 'space'}


def toasts(con):
    return re.findall(r"^toast \S+ \([^)]*\) '(.*?)'(?= \||$)", con.call('coop.status')['out'], re.M)


def hb(con):
    m = re.search(r'^host busy: .*$', con.call('coop.status')['out'], re.M)
    return m.group(0) if m else '?'


def cell_pos(con):
    m = re.search(r'^avatar .*?pos=\((-?[\d.]+) (-?[\d.]+)\)', con.call('coop.pool')['out'], re.M)
    return (float(m.group(1)), float(m.group(2))) if m else None


def cell_moved(con, secs=3.0):
    """the own cell sent to swim 40 m on: how far it got in `secs` (None: no position)"""
    a = cell_pos(con)
    con.call('coop.pool goto 40 0')
    time.sleep(secs)
    b = cell_pos(con)
    return math.dist(a, b) if a and b else None


def held(con, stage):
    """the guest's game stands now (the game's own state)"""
    if stage == 'cell':
        d = cell_moved(con)
        return d is not None and d < 0.5, f'cell moved {d}'
    b = busy(con)
    return 'paused' in b, f'busy {b}'


def wait_held(con, stage, want, timeout):
    t0, info = time.time(), ''
    while time.time() - t0 < timeout:
        h, info = held(con, stage)
        if h == want:
            return True, time.time() - t0, info
        time.sleep(0.3)
    return False, time.time() - t0, info


def waits(con):
    m = re.search(r'waits=(\d+)', hb(con))
    return int(m.group(1)) if m else -1


STAND_CARDS = ("'s game stands", 'goes on: so does your game', 'paused the game')


def leaving(guest):
    """the guest gets the host's world to load it (its quit to the menu and the load stand its time: not the host's
    stand). The host sends it after the share's save was written and its box closed, so the save's stand is over."""
    return any(m in guest.call('game.mode')['out'] for m in ('GalaxyGameEntry', 'Load')) or \
        'receiving world' in guest.call('coop.status')['out']


def watch_guest(guest, secs, until=None, stop=None):
    """sample the guest ~10 times a second for `secs` (or until `until()` and 3 s more, or at once when `stop()`):
    the samples whose game time stood, the stand cards it showed"""
    t0, stood, cards, n, done_at, last_toasts = time.time(), [], set(), 0, None, 0.0
    while time.time() - t0 < secs:
        if stop and n % 3 == 0 and stop():
            print(f'  (the guest gets the host\'s world {time.time() - t0:.1f} s into the watch: watch ends)', flush=True)
            for t in toasts(guest):
                if any(c in t for c in STAND_CARDS):
                    cards.add(t)
            break
        try:
            b = busy(guest)
        except OSError:
            b = []
        n += 1
        if 'paused' in b:
            stood.append(round(time.time() - t0, 1))
        if time.time() - last_toasts > 0.5:
            last_toasts = time.time()
            for t in toasts(guest):
                if any(c in t for c in STAND_CARDS):
                    cards.add(t)
        if until and done_at is None and until():
            done_at = time.time()
        if done_at and time.time() - done_at > 3.0:
            break
        time.sleep(0.08)
    return stood, sorted(cards), n


def autosave_confirmed(host):
    m = re.search(r'confirmed=(\d+)', host.call('coop.autosave')['out'])
    return int(m.group(1)) if m else -1


def host_save(ck, host, guest):
    """BUG-1217: the host's own saves (an autosave; the share's save of a rejoin) do not stop the Tribe guest"""
    w0 = waits(guest)
    print(f'  before: guest {hb(guest)}', flush=True)
    # 1. the host's autosave (a milestone, as a captured village or a new stage)
    c0 = autosave_confirmed(host)
    print('  host:', host.call('coop.autosave milestone a test milestone')['out'].strip(), flush=True)
    stood, cards, n = watch_guest(guest, 45, until=lambda: autosave_confirmed(host) > c0)
    saved = autosave_confirmed(host) > c0
    print(f'  autosave confirmed={saved}; guest samples {n}, time stood at {stood}; cards {cards}; {hb(guest)}', flush=True)
    ck(saved, 'the host\'s game saved itself (coop.autosave: confirmed went up)')
    w1 = waits(guest)
    ck(not stood and not cards and w1 == w0,
       f'the host\'s autosave does not stop the guest\'s game (time stood at {stood} s, cards {cards}, waits {w0} -> {w1})')
    # 2. the guest leaves and joins again in its copy of the world: the host saves for the share
    guest.call('coop.leave')
    time.sleep(3)
    w1 = waits(guest)
    print('  rejoin:', (guest.call(lane.JOIN).get('out') or '').strip()[:60], flush=True)
    time.sleep(1)
    stood, cards, n = watch_guest(guest, 30, stop=lambda: leaving(guest))
    w2 = waits(guest)
    t0 = time.time()
    while time.time() - t0 < 120 and not ('Tribe' in guest.call('game.mode')['out'] and guest.call('coop.avatar')['ok']):
        time.sleep(1)
    mode = guest.call('game.mode')['out'].strip()
    print(f'  after the rejoin: guest {mode}; samples {n}, time stood at {stood}; cards {cards}; {hb(guest)}', flush=True)
    ck('Tribe' in mode, f'the guest plays Tribe again after its rejoin ({mode})')
    ck(not stood and not cards and w2 == w1,
       f'the share\'s save for the rejoin does not stop the guest\'s game (time stood at {stood} s, cards {cards}, '
       f'waits {w1} -> {w2})')
    keyframe('host_save_tribe', 'tribe: the guest after its rejoin and the host\'s autosave: no "Alice\'s game stands" '
                                '/ "goes on" card on the guest', settle=0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', required=True, choices=sorted(FIXTURE))
    ap.add_argument('--rivals', action='store_true', help='Space: Rivals mode, the guest must not wait')
    ap.add_argument('--guest-pause', action='store_true', help='the guest pauses: the host must not wait')
    ap.add_argument('--host-save', action='store_true', help='Tribe: the host\'s own saves must not stop the guest')
    a = ap.parse_args()
    host, guest = start(FIXTURE[a.stage])
    ck = Checks()
    game_runs(guest, 90)
    game_runs(host, 90)
    time.sleep(5)
    if a.stage == 'cell':
        host.call('coop.pool invulnfor 600')
        guest.call('coop.pool invulnfor 600')
    if a.rivals:
        print('  host:', host.call('coop.space rivalry mode rivals')['out'].strip()[:120], flush=True)
        t0 = time.time()
        while time.time() - t0 < 15 and 'rivals=1' not in guest.call('coop.space rivalry')['out']:
            time.sleep(0.5)
        ck('rivals=1' in guest.call('coop.space rivalry')['out'], 'Rivals mode on in the guest\'s game')
    if a.host_save:
        host_save(ck, host, guest)
        ck.faults(host=host, guest=guest)
        return ck.result()
    pauser, other = (guest, host) if a.guest_pause else (host, guest)
    h0, info = held(other, a.stage)
    print(f'  before: {info}; {hb(other)}', flush=True)
    ck(not h0, f'the {"host" if a.guest_pause else "guest"}\'s game runs before the pause ({info})')

    # the pause menu, as a player
    pauser.call('key esc')
    t0 = time.time()
    while time.time() - t0 < 8 and not uifind.shown(pauser, 'Resume Game'):
        time.sleep(0.3)
    shown = uifind.shown(pauser, 'Resume Game')
    print(f'  pause menu shown: {shown}; pauser busy {busy(pauser)}', flush=True)
    if not ck(shown, 'the pause menu is up (Esc)'):
        ck.faults(host=host, guest=guest)
        return ck.result()

    expect_wait = not (a.rivals or a.guest_pause)
    if expect_wait:
        ok, dt, info = wait_held(other, a.stage, True, 8.0 if a.stage == 'cell' else 6.0)
        note = [t for t in toasts(other) if 'paused the game' in t]
        if ok and not note:
            time.sleep(1.0)
            note = [t for t in toasts(other) if 'paused the game' in t]
        print(f'  guest held={ok} after {dt:.1f} s ({info}); {hb(other)}; panels {toasts(other)}', flush=True)
        ck(ok, f'the guest\'s game waits while the host\'s game is paused ({info})')
        ck(bool(note) and note[0].startswith(HOST), f'the guest\'s screen says the host paused the game ({note})')
        keyframe(f'host_paused_{a.stage}', f'{a.stage}: the host in its pause menu (Resume Game ...); the guest\'s game '
                                          'standing with the panel "Alice paused the game: yours waits ..."', settle=0)
    else:
        time.sleep(6.0)
        h, info = held(other, a.stage)
        note = [t for t in toasts(other) if 'paused the game' in t]
        who = 'host' if a.guest_pause else 'guest'
        print(f'  {who} held={h} ({info}); {hb(other)}; panels {toasts(other)}', flush=True)
        ck(not h, f'the {who}\'s game runs on ({info})' + (' (Rivals mode)' if a.rivals else ' while the guest is paused'))
        ck(not note, f'no "paused the game" line in the {who}\'s game ({note})')
        keyframe(f'pause_{"rivals" if a.rivals else "guest"}_{a.stage}',
                 f'{a.stage}: one game in its pause menu, the other ({who}) plays on, no wait line', settle=0)

    # resume
    uifind.press(pauser, 'Resume Game')
    if expect_wait:
        ok, dt, info = wait_held(other, a.stage, False, 10.0)
        print(f'  {dt:.1f} s after Resume Game: guest held={not ok} ({info}); panels {toasts(other)}', flush=True)
        ck(ok, f'the guest\'s game runs again after the host\'s Resume Game ({info})')
    t1 = time.time()
    while time.time() - t1 < 8 and uifind.shown(pauser, 'Resume Game'):
        time.sleep(0.3)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
