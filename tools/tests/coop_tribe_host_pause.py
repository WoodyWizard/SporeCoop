#!/usr/bin/env python3
"""Tribe: while the host's game is paused (its pause menu) the guest's game waits too, with a note (BUG-1086).

A Discord player: "in Tribe the guest cannot control its units". One cause: every guest member is a mirror of the
host's game, so while the host sat in its pause menu (or the Sporepedia, a tutorial box, a modal) the guest's orders
waited for a game that stood, and the guest was never told: its game ran on, its members stood.

Two games on the tribe fixture.
1. The host opens its pause menu (Esc, as a player): its game time stands (`coop.status` busy: paused).
2. On the guest's game, within 6 s: its game time stands too (`busy: paused`, the game's cGameTimeManager), and its
   notification panels (the game's own windows, `coop.status` toast lines) show "<host> paused the game". A key frame
   of both games.
3. The host resumes (Resume Game, as a player): the guest's game runs again within 10 s, with the note that it goes on.
Without the fix (SPORE_FIX_OFF=tribe.host_pause_wait) the guest plays on with no word: step 2 FAILS.

  SPORE_LANE=6 python3 tools/tests/coop_tribe_host_pause.py
"""
import re
import time

import uifind
from twoinst import Checks, game_runs, keyframe, start

HOST = 'Alice'


def busy(con):
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1).split(',') if m else ['?']


def toasts(con):
    # (the text may hold an apostrophe: up to the quote before the text's line list or the line's end)
    return re.findall(r"^toast \S+ \([^)]*\) '(.*?)'(?= \||$)", con.call('coop.status')['out'], re.M)


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(5)
    print(f'  before: host busy {busy(host)}, guest busy {busy(guest)}', flush=True)
    ck('paused' not in busy(guest), f'the guest\'s game runs before the host pauses ({busy(guest)})')

    # 1. the host's pause menu
    host.call('key esc')
    t0 = time.time()
    while time.time() - t0 < 8 and 'paused' not in busy(host):
        time.sleep(0.3)
    print(f'  host busy {busy(host)}, pause menu shown {uifind.shown(host, "Resume Game")}', flush=True)
    if not ck('paused' in busy(host), 'the host\'s game stands in its pause menu (Esc)'):
        ck.faults(host=host, guest=guest)
        return ck.result()

    # 2. the guest's game waits, and says why
    waited, gb, note = None, [], []
    while time.time() - t0 < 8:
        gb = busy(guest)
        note = [t for t in toasts(guest) if 'paused the game' in t]
        if 'paused' in gb and waited is None:
            waited = time.time() - t0
        if waited is not None and note:
            break
        time.sleep(0.3)
    print(f'  guest busy {gb} ({waited and round(waited, 1)} s after the host\'s Esc), its panels: {toasts(guest)}', flush=True)
    ck(waited is not None, f'the guest\'s game waits while the host\'s game is paused ({gb})')
    ck(note and note[0].startswith(HOST), f'the guest\'s screen says the host paused the game ({note})')
    keyframe('host_paused', 'the host in its pause menu (Resume Game ...); the guest\'s Tribe standing with the panel '
                            '"Alice paused the game: yours waits ..."', settle=0)

    # 3. the host goes on
    uifind.press(host, 'Resume Game')
    t1, runs = time.time(), False
    while time.time() - t1 < 15 and not runs:
        runs = 'paused' not in busy(host) and 'paused' not in busy(guest)
        time.sleep(0.5)
    print(f'  {time.time() - t1:.1f} s after Resume Game: host busy {busy(host)}, guest busy {busy(guest)}, '
          f'panels {toasts(guest)}', flush=True)
    ck(runs, 'both games run again after the host\'s Resume Game')
    st = next((ln.strip() for ln in guest.call('coop.status')['out'].splitlines() if 'game over wait' in ln), '')
    print('  ' + st[:200], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
