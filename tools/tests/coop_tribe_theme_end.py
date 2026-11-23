#!/usr/bin/env python3
"""Tribe: the host's fight music the relay plays on a guest stops when the guest leaves the session (BUG-1496, audit
TR-11).

The guest's game plays the host's theme music (TribeTheme_Play on its own theme manager, BUG-0568). At the session's
end the mod only forgot which slots the relay played (theme_forget); the guest's game stays in Tribe and played them
on. Same shape as BUG-0890 (party music, fixed for its kind 8 only).

1. Two games on the tribe fixture, 6 more members; both games' audio lists marked (`coop.audio`: the game's own audio
   commands, not our bookkeeping). The host's player members attack an NPC member and fight.
2. Up to 60 s: the guest's game plays at least one theme track (a play from TribeTheme_Play, return address 0xBC2414)
   that has no stop yet (`coop.audio list`: `playing`). Else INCONCLUSIVE.
3. The guest leaves the session (`coop.leave`). PASS: within 4 s none of the theme tracks that were playing there has
   no stop (each got the game's stop command).
SPORE_FIX_OFF=tribe.theme_end must FAIL (they play on: no stop command).
? Whether a theme track ends by itself (a one-shot) is not known: the audio list keeps a track as playing until a stop.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_theme_end.py
"""
import re
import time

from coop_tribe_gameover import player_members
from twoinst import Checks, start

THEME_CALLER = 'BC2414'


def theme_playing(con):
    """the tracks of theme plays since the mark that have no stop yet"""
    out = con.call('coop.audio list 20000')['out']
    tracks = {m.group(1) for m in re.finditer(r'cmd=03475365 sound=[0-9A-F]{8} track=(\d+) .*from=[0-9A-F]+,([0-9A-F]+)', out)
              if m.group(2) == THEME_CALLER}
    playing = {m.group(1) for m in re.finditer(r'^playing track=(\d+)', out, re.M)}
    return tracks & playing


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    print(' ', host.call('coop.tribe spawn 6')['out'].strip()[:80], flush=True)
    time.sleep(6)
    own, enemy = player_members(host)
    if not ck(own and enemy, f'the player tribe ({len(own)}) and another tribe ({len(enemy)}) have members'):
        return ck.result()
    for c in (host, guest):
        print(' ', c.call('coop.audio mark')['out'].strip(), flush=True)
    for i in own:
        host.call(f'coop.tribe select {i} 1')
    print('  attack:', host.call(f'coop.tribe act 7 {enemy[0]}')['out'].strip(), flush=True)
    t0, now = time.time(), set()
    while time.time() - t0 < 60:
        time.sleep(2)
        now = theme_playing(guest)
        if now:
            break
    print(f'  the guest plays theme tracks {sorted(now)} after {time.time() - t0:.0f} s', flush=True)
    if not now:
        ck.faults(host=host, guest=guest)
        return ck.inconclusive('the guest\'s game played no theme track of the host\'s fight in 60 s')
    print('  guest:', guest.call('coop.leave')['out'].strip()[:80], flush=True)
    t0, left = time.time(), now
    while time.time() - t0 < 4:
        time.sleep(1)
        left = now & theme_playing(guest)
        if not left:
            break
    print('  guest:', [ln for ln in guest.call('coop.tribe')['out'].splitlines() if 'theme_end' in ln][:1], flush=True)
    ck(not left, f'the relay\'s theme tracks stopped on the guest at the session\'s end (still playing {sorted(left)} of '
       f'{sorted(now)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
