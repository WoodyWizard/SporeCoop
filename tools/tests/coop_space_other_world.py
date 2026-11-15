#!/usr/bin/env python3
"""Space: the player list's "other world" means another save, not another star (BUG-1063).

Both play the host's Space save. The guest zooms out to the galaxy view (its place is 0 there: no star, no planet).
Both co-op windows (the lobby's player rows, the game's own captions) show the partner as "Space" without
", other world", at the home star and while the guest is in the galaxy view.
SPORE_FIX_OFF=core.ui_other_world: the rows say "Space, other world" (the 0.7.13 label: another place) and it FAILs.

  SPORE_LANE=4 python3 tools/tests/coop_space_other_world.py
"""
import time

from coop_space_map import DRIVE, no_movie, place, sp, to_galaxy
from coop_ui import Ui
from twoinst import Checks, shot, start


def rows_now(ui):
    st = ui.wait(lambda s: s.get('open') == '1' and len(s['rows']) == 2, 15, 'the player rows')
    return st['rows'] if st else []


def main():
    host, guest = start('space')
    ck = Checks()
    for c in (host, guest):
        print('  drive:', sp(c, 'tool add', DRIVE), flush=True)
        no_movie(c)
    hui, gui = Ui(host, 'host'), Ui(guest, 'guest')
    for c in (host, guest):
        c.call('coop.uistate 1')  # the co-op window open (the lobby's player rows)

    def check(when):
        time.sleep(2.5)  # (the partner's state with the new place reaches the other game; the window refreshes)
        for ui in (hui, gui):
            rows = rows_now(ui)
            print(f'  {ui.who} ({when}): rows {rows}', flush=True)
            stages = [r[2] for r in rows if len(r) >= 3]
            ck(len(stages) == 2 and all(s.startswith('Space') for s in stages) and
               not any('other world' in s for s in stages),
               f'BUG-1063: {ui.who}\'s player list says Space without "other world" {when} ({stages})')

    check('(both at the home star)')
    for c in (host, guest):
        c.call('coop.uistate 0')
    ck(to_galaxy(guest), 'the guest is in the galaxy view (its place is 0)')
    print('  guest place:', place(guest), 'host place:', place(host), flush=True)
    for c in (host, guest):
        c.call('coop.uistate 1')
    check('while the guest is in the galaxy view')
    shot(guest, 'space_other_world_guest.png')
    shot(host, 'space_other_world_host.png')
    for c in (host, guest):
        c.call('coop.uistate 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
