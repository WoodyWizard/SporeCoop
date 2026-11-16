#!/usr/bin/env python3
"""BUG-1218: the start-up video (the big SPORE logo over a starfield) does not play over the stage after a load that
left the galaxy menu within ~16 s.

The game plays its start-up video (Movie::MovieSystem: the logos, then the SPORE logo) while the galaxy menu comes up;
its layer is drawn over the 3D view (under the HUD) until it ends ~16 s after the menu, and its end starts the menu's
GGE_intro (refused outside the menu by BUG-0626). A load from the menu before that (the lab's quick load; a guest that
joins from its menu at once) took the video into the stage: the 0.7.15 final check's key frames menu_splash and
civ_designs show Creature / Civ play under the SPORE logo, 0.3-0.9 s before the video's end.

1. Cold start of both games on the creature fixture (the host loads ~4 s after its menu, the guest joins from its menu).
2. Right after the start, for 20 s, twice a second on both games: the game's mode and whether the video plays
   (MovieIsPlaying: [[0x015FCC68] + 0x34] != 0, the game's own movie object) and `coop.status` "start-up video
   stopped N". A key frame of both games right at the start (the logo must not be over the play).
PASS: no game plays the video in the stage for more than 2 s (a sample in the stage with it playing later than 2 s
after the first such sample). INCONCLUSIVE: no game had the video in the stage (the start was slower than the video).
Without the fix (SPORE_FIX_OFF=world.menu_movie) it plays on in the stage until its end: FAIL.

  SPORE_LANE=3 python3 tools/tests/coop_menu_movie.py
"""
import re
import struct
import time

from coop_creature_kills import read
from harness import mode_word as mode
from twoinst import Checks, keyframe, start

MOVIE_SYSTEM = 0x015FCC68  # Movie::MovieSystem::Get's global (data/symbols/overrides_dc04aee5.sym)


def movie_playing(con):
    ms = struct.unpack('<I', read(con, MOVIE_SYSTEM, 4) or b'\0\0\0\0')[0]
    if not ms:
        return False
    b = read(con, ms + 0x34, 4)
    return len(b) == 4 and struct.unpack('<I', b)[0] != 0


def stopped(con):
    m = re.search(r'start-up video stopped (\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else 0


def main():
    host, guest = start('creature')
    ck = Checks()
    t0 = time.time()
    samples = {'host': [], 'guest': []}
    framed = False
    while time.time() - t0 < 20:
        for role, con in (('host', host), ('guest', guest)):
            md = mode(con)
            samples[role].append((round(time.time() - t0, 1), md, movie_playing(con)))
        if not framed:
            framed = True
            keyframe('stage', 'both games in Creature play right after a quick load from the menu: no SPORE logo, no '
                              'starfield over the play', settle=0)
        time.sleep(0.5)
    first, late = {}, {}
    st = {'host': stopped(host), 'guest': stopped(guest)}
    for role, ss in samples.items():
        in_stage = [(t, p) for t, md, p in ss if md == '0x01654C01']
        on = [t for t, p in in_stage if p]
        first[role] = on[0] if on else None
        late[role] = [t for t in on if t - on[0] > 2.0]
        print(f'  {role}: video playing in the stage at {on} s; stopped by the mod {st[role]}; modes '
              f'{sorted({md for _, md, _ in ss})}', flush=True)
    if all(first[r] is None for r in first) and not any(st.values()):
        return ck.inconclusive('no game had the start-up video in the stage (the start took longer than the video)')
    for role in ('host', 'guest'):
        ck(not late[role], f'the {role}\'s start-up video does not play on over the stage ({late[role] or "no"} s '
                           f'more than 2 s after it was first seen there)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
