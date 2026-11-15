#!/usr/bin/env python3
"""BUG-0985: no partner name tag or edge arrow over a cutscene that holds the game.

Key-frame review of the 0.7.13 final check (campaign advance-cell): the host stood in the Creature stage's intro (a
cinematic with the input locked and the time stopped) while the guests played, and the partners' tags were drawn at
the left edge over the cutscene.

On the guest's game of the creature fixture (the host's creature next to it: its tag drawn), the game's own
PlayCinematic plays CRG_ClaimNest (its caption keeps it running until a key). Read from the overlay's last pass
(`coop.find boxes`: every label as drawn) and a key frame:
  * before: the host's tag is drawn on the guest;
  * while the cinematic holds the guest's game (`busy` cinematic with input_lock or paused): no tag or arrow drawn;
  * after it ended (Enter): the tag is drawn again.
SPORE_FIX_OFF=find.cinematic must FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_tag_cinematic.py
"""
import re
import time

from coop_cinematic_puppet import cinematic, eax, wait
from twoinst import Checks, game_runs, keyframe, start


def drawn(con):
    out = con.call('coop.find boxes').get('out') or ''
    return [m.group(2) for m in re.finditer(r'^box kind=(\S+) id=\d+ .* hidden=0 text=(.*)$', out, re.M)]


def busy(con):
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1) if m else '?'


def main():
    host, guest = start('creature')
    ck = Checks()
    game_runs(guest)
    time.sleep(2)
    before = wait(lambda: drawn(guest), 10) or []
    print(f'  before: drawn on the guest {before}', flush=True)
    ck(any('Alice' in t for t in before), f'before: the host\'s tag is drawn on the guest ({before})')
    cm = eax(guest, 'Simulator::cCinematicManager::Get cdecl')
    guest.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall {cm:#x} s:CRG_ClaimNest 1 1 0 0 0')
    playing = wait(lambda: cinematic(guest), 10)
    time.sleep(1.5)
    b = busy(guest)
    holds = 'cinematic' in b and ('input_lock' in b or 'paused' in b)
    print(f'  the cinematic {playing} plays on the guest, busy {b}', flush=True)
    ck(bool(playing) and holds, f'the guest plays a cinematic that holds the game ({playing}, {b})')
    during = drawn(guest)
    keyframe('cinematic', 'the guest: a cutscene (the nest claim) with no partner tag or arrow over it; the host: play',
             settle=0)
    print(f'  during: drawn on the guest {during}', flush=True)
    ck(holds and not during, f'during the cutscene no partner tag or arrow is drawn ({during})')
    t0 = time.time()
    while time.time() - t0 < 40 and cinematic(guest):
        guest.call('key enter')
        time.sleep(1.0)
    game_runs(guest)
    time.sleep(1.5)
    after = wait(lambda: drawn(guest), 10) or []
    print(f'  after: drawn on the guest {after} (cinematic {cinematic(guest)})', flush=True)
    ck(any('Alice' in t for t in after), f'after the cutscene the host\'s tag is drawn again ({after})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
