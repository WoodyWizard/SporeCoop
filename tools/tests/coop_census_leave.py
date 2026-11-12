#!/usr/bin/env python3
"""BUG-0885: the detector's `coop.census all` reads no game list while the game leaves its stage.

coop.wstate stopped reading during a stage change (BUG-0605: the lists hold freed civilizations and vehicles until
the mode changes); coop.census, which the detector calls just as often (worlddiff.py), read on. The Creature stage
left for the galaxy menu frees the avatar and the creatures before the game's mode changes (BUG-0264) the same way.

The guest's game leaves the Creature stage for the galaxy menu (`coop.menu`, the game's own quit message); from the
moment the message is posted until the game's mode is no longer Creature, `coop.census all` is asked again and
again. Every census answered in that window (the test posted the quit, the census header's own game mode
`gamemode=` is still Creature) must list 0 objects (`st phase changing=1`); before the quit the census lists the
world (control); the game has no fault and writes no crash report.
SPORE_FIX_OFF=census.census_live must FAIL (the census lists the creatures of a stage being torn down).

  SPORE_LANE=N python3 tools/tests/coop_census_leave.py
"""
import re
import time

from twoinst import Checks, start

HEAD = re.compile(r'census v1 role=\S+ me=\d+ mode=(.*?) modeid=([0-9A-F]+) .*?gamemode=([0-9A-F]+) leaving=(\d)')
CREATURE = '01654C01'


def census(con):
    out = con.call('coop.census all 250')['out']
    m = HEAD.search(out)
    rows = len(re.findall(r'^obj ', out, re.M))
    return (m.group(2), m.group(3), m.group(4)) if m else None, rows, 'changing=1' in out


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    head, rows, _ = census(guest)
    print(f'  before the quit: {head} rows={rows}', flush=True)
    ck(head is not None and head[1] == CREATURE and rows >= 10, f'the census lists the Creature world first ({rows} objects)')
    print('  guest: ' + guest.call('coop.menu')['out'].strip(), flush=True)
    t0 = time.time()
    inside, listed, asked = 0, 0, 0
    while time.time() - t0 < 60:
        head, rows, changing = census(guest)
        asked += 1
        if head is None:
            time.sleep(0.05)
            continue
        if head[1] != CREATURE:  # the game's mode changed: the stage is gone
            break
        inside += 1
        if rows:
            listed += 1
            if listed <= 3:
                print(f'  a census while leaving: {head} rows={rows} changing={int(changing)}', flush=True)
    print(f'  {asked} censuses after the quit, {inside} while the game was still in Creature, {listed} of them listed '
          f'objects; mode now {guest.call("game.mode")["out"].strip()}', flush=True)
    ck(inside >= 3, f'the census was asked while the game left its stage ({inside} times)')
    ck(listed == 0, f'no census listed objects while the game left its stage ({listed} of {inside})')
    time.sleep(5)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
