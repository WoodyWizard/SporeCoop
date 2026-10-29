#!/usr/bin/env python3
"""A host editor or load counts as "host away" only in Creature, Tribe and Civ (BUG-0233, GitHub #167).

In Space (and Cell) no co-op module plays on alone while the host edits, yet since 0.7.3.0 every
editor visit of the host longer than 4 s gave the guest "<host> (host) is in an editor: you play
on alone" and then "is back". The main menu still counts in every stage (BUG-0151).

Space fixture, host and guest. The host's player states report another mode for a while (test
hook `coop.hoststate mode <hex> <seconds>`: exactly the input the guest's rule reads):
  1. the editor mode (kEdit 0x1654C06) for 10 s: for 15 s the guest's notification panels (what
     the player sees, `coop.status` toast lines) never say "play on alone"
  2. control: the galaxy menu mode for 10 s: the guest does say "left for the main menu: you play
     on alone" (the hook reaches the guest and the menu case still counts in Space)

  coop_away_stages.py [--nofix]   (--nofix: coop.fix core.away_stages off: part 1 must FAIL)
"""
import sys
import time

from twoinst import Checks, start

KEDIT, KMENU = '1654C06', '2CCD1D2'


def toasts(con):
    return [l for l in con.call('coop.status')['out'].splitlines() if l.startswith('toast ')]


def watch(guest, seconds, text):
    """the toast lines of the guest that contain `text` within `seconds`"""
    seen = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        seen += [t for t in toasts(guest) if text in t and t not in seen]
        time.sleep(0.5)
    return seen


def main():
    nofix = '--nofix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    print(' ', guest.call(f'coop.fix core.away_stages {"off" if nofix else "on"}')['out'].strip())
    time.sleep(5)

    print(' ', host.call(f'coop.hoststate mode {KEDIT} 10')['out'].strip())
    seen = watch(guest, 15, 'play on alone')
    print('  guest toasts while the host "edits":', seen or 'none')
    ck(not seen, 'no "you play on alone" in Space for a host editor')
    time.sleep(3)

    print(' ', host.call(f'coop.hoststate mode {KMENU} 10')['out'].strip())
    seen = watch(guest, 12, 'play on alone')
    print('  guest toasts while the host is "in the menu":', seen or 'none')
    ck(any('main menu' in t for t in seen), 'the main menu still counts as away in Space (control)')
    time.sleep(4)

    guest.call('coop.fix core.away_stages on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
