#!/usr/bin/env python3
"""BUG-1218: the start-up video (the big SPORE logo over a starfield) does not play over the stage after a load that
left the galaxy menu within ~16 s.

The game plays its start-up video (Movie::MovieSystem: the logos, then the SPORE logo) while the galaxy menu comes up;
its layer is drawn over the 3D view (under the HUD) until it ends ~16 s after the menu, and its end starts the menu's
GGE_intro (refused outside the menu by BUG-0626). A load from the menu before that (the lab's quick load; a guest that
joins from its menu at once) took the video into the stage: the 0.7.15 final check's key frames menu_splash and
civ_designs show Creature / Civ play under the SPORE logo, 0.3-0.9 s before the video's end.

1. Cold start of both games on the creature fixture; the guest joins from its menu 0.5 s after it came up (a player who
   joins at once: SPORE_FIX_OFF reaches the guest before the join) and loads the host's world while its video plays.
   Test seam: right before the join the guest's video is held (Movie::MovieSystem::PauseMovie 0xFD7560): the natural
   race (a load done before the video's own end, ~16 s after the menu) left it 0.6-3.5 s in the stage in the lab, too
   short a window for the twin; held, it stays until something stops it. The
   host's own quick load (the lab's) is stopped as soon as it hosts, before SPORE_FIX_OFF reaches it: its rows count only
   with the fix on.
2. From right before the guest's join (a thread with its own console connections, while start() goes on) for up to
   60 s, twice a second on both games: the game's mode and whether the video plays (MovieIsPlaying: [[0x015FCC68] +
   0x34] != 0, the game's own movie object); after it `coop.status` "start-up video stopped N". A key frame of both
   games 8 s and 12 s after the guest is in the stage (the stage is black for its first ~5 s) (the logo must not be over the play).
PASS: no game plays the video in the stage for more than 2 s (a sample in the stage with it playing later than 2 s
after the first such sample). INCONCLUSIVE: no game had the video in the stage (the start was slower than the video).
Without the fix (SPORE_FIX_OFF=world.menu_movie) it plays on in the stage until its end: FAIL.

  SPORE_LANE=3 python3 tools/tests/coop_menu_movie.py
"""
import re
import struct
import threading
import time

import lane
from coop_creature_kills import read
from harness import mode_word as mode
from sporectl import Console
from twoinst import Checks, keyframe, start

STAGE = '0x01654C01'
MOVIE_SYSTEM = 0x015FCC68  # Movie::MovieSystem::Get's global (data/symbols/overrides_dc04aee5.sym)


def movie_system(con):
    b = read(con, MOVIE_SYSTEM, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else 0


def movie_playing(con):
    ms = movie_system(con)
    if not ms:
        return False
    b = read(con, ms + 0x34, 4)
    return len(b) == 4 and struct.unpack('<I', b)[0] != 0


def stopped(con):
    m = re.search(r'start-up video stopped (\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else 0


def sampler(out, stop):
    """from right before the guest's join: both games' mode and video twice a second (own console connections: start()
    goes on meanwhile); a key frame of both 1 s after the guest first is in the stage"""
    cons = {'host': Console(lane.HOST, timeout=10), 'guest': Console(lane.GUEST, timeout=10)}
    t0, framed, shots = time.time(), None, [8.0, 12.0]
    while not stop.is_set() and time.time() - t0 < 60:
        for role, con in cons.items():
            try:
                out[role].append((round(time.time() - t0, 1), mode(con), movie_playing(con)))
            except (OSError, ValueError):
                pass
        g = out['guest'][-1] if out['guest'] else None
        if g and g[1] == STAGE and framed is None:
            framed = time.time()
        if framed and shots and time.time() - framed >= shots[0]:  # (the stage is black for its first ~5 s: the load)
            keyframe(f'stage_{shots.pop(0):.1f}s', 'both games in Creature play, the guest seconds after it joined from '
                     'its menu at once: no SPORE logo, no starfield over the play', players=dict(cons), settle=0)
        time.sleep(0.5)


def main():
    samples = {'host': [], 'guest': []}
    stop = threading.Event()
    th, held = [], []

    def before_join(g):
        # the test seam: the guest's video is held (PauseMovie: it stays, drawn, until something stops it), so it is
        # surely still there when the guest's stage starts (the natural race: a join that loads within the ~16 s)
        ms = movie_system(g)
        out = g.call(f'call Movie::MovieSystem::PauseMovie thiscall 0x{ms:X}')['out'].strip() if ms else 'no movie system'
        held.append(movie_playing(g))
        print(f'  guest before the join: the start-up video plays {held[0]}, held ({out[:80]})', flush=True)
        th.append(threading.Thread(target=sampler, args=(samples, stop), daemon=True))
        th[0].start()
    # the guest joins 0.5 s after its menu (SPORE_FIX_OFF reaches it before)
    host, guest = start('creature', join_at=0.5, before_join=before_join)
    ck = Checks()
    time.sleep(15)
    stop.set()
    if th:
        th[0].join(15)
    first, late = {}, {}
    st = {'host': stopped(host), 'guest': stopped(guest)}
    for role, ss in samples.items():
        on = [t for t, md, p in ss if md == STAGE and p]
        first[role] = on[0] if on else None
        late[role] = [t for t in on if t - on[0] > 2.0]
        print(f'  {role}: {len(ss)} samples from the join; the video playing in the stage at {on} s; stopped by the mod '
              f'{st[role]}; modes {sorted({md for _, md, _ in ss})}', flush=True)
    if movie_playing(guest):  # (the switch off: the held video would stay for good)
        ms = movie_system(guest)
        print('  guest: the held video stopped by the test: ' +
              guest.call(f'call Movie::MovieSystem::StopMovie thiscall 0x{ms:X}')['out'].strip()[:80], flush=True)
    if not (held and held[0]) or (first['guest'] is None and not st['guest']):
        return ck.inconclusive('the guest had no start-up video at its join or in the stage')
    for role in ('host', 'guest'):
        ck(not late[role], f'the {role}\'s start-up video does not play on over the stage ({late[role] or "no"} s '
                           f'more than 2 s after it was first seen there)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
