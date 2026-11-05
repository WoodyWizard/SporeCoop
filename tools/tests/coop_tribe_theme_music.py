#!/usr/bin/env python3
"""Tribe: the guest hears the stage's hunt and fight music of its tribe (BUG-0568).

Player report (guest): "I don't have any music in the Tribe stage". The Tribe stage's action music is started by the
player tribe's members' behaviour trees through the stage's theme music manager (TribeStage +0x1C8,
SporeCoop::TribeTheme_Play); a guest's members are mirrors without a tree, so its game never started it. The lab
games run muted: the test reads the game's own audio commands (`coop.audio`: every play the game's AudioSystem
submitted, with the sound id and the caller), not our bookkeeping.

1. Two games on the tribe fixture, 6 more members; both games' audio lists marked; the host's player members attack
   an NPC tribe's member (the player's attack order) and fight for 40 s.
2. The theme plays (Audio::PlayProceduralAudio called from TribeTheme_Play, return address 0xBC2414) of each game.
PASS: the host's game played at least 2, the guest's game at least 80 % as many, with the same sound ids.

SPORE_FIX_OFF=tribe.theme_music: the guest's game plays none (FAIL).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_theme_music.py
"""
import re
import time

from coop_tribe_gameover import player_members
from twoinst import Checks, start

THEME_CALLER = 'BC2414'  # the return address of TribeTheme_Play's call of PlayProceduralAudio


def themes(con):
    """[sound id] of the theme plays the game's audio system got since the mark"""
    out = con.call('coop.audio list 20000')['out']
    return [m.group(1) for m in re.finditer(r'cmd=03475365 sound=([0-9A-F]{8}) .* from=([0-9A-F]+),([0-9A-F]+)', out)
            if m.group(3) == THEME_CALLER]


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    print(' ', host.call('coop.tribe spawn 6')['out'].strip())
    time.sleep(6)
    own, enemy = player_members(host)
    ck(own and enemy, f'the player tribe ({len(own)}) and another tribe ({len(enemy)}) have members')
    if not (own and enemy):
        return ck.result()
    for c in (host, guest):
        print(' ', c.call('coop.audio mark')['out'].strip())
    for i in own:
        host.call(f'coop.tribe select {i} 1')
    print('  attack:', host.call(f'coop.tribe act 7 {enemy[0]}')['out'].strip())
    time.sleep(40)
    h, g = themes(host), themes(guest)
    print(f'  theme plays: host {len(h)} {sorted(set(h))}, guest {len(g)} {sorted(set(g))}')
    print('  host', host.call('coop.tribe anims')['out'].splitlines()[-1])
    print('  guest', guest.call('coop.tribe anims')['out'].splitlines()[-1])
    ck(len(h) >= 2, f'the host\'s game plays the fight theme ({len(h)} >= 2)')
    ck(len(g) >= 0.8 * len(h) and len(g) > 0 and set(g) <= set(h),
       f'the guest\'s game plays it too ({len(g)} of {len(h)}, the same sounds)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
