#!/usr/bin/env python3
"""Space: a star the allies mirror gives back when a partner's colony goes leaves the game's list of owned stars nearby
(BUG-1484, audit SPC-5; BUG-1144's crash signature).

The Space game keeps the stars near the current one that an empire owns (cSimulatorSpaceGame +0x30 -> +0x7C); its NPC
visitor spawn (0x1009070) calls 0xC32810 on GetEmpire(the star's owner) with no null check. BUG-1144 rebuilt the list
when the whole mirror goes (a save, the session's end). When only one colony of a partner goes (lost, abandoned or
blocked) apply() gave its star back to nobody without the rebuild: the list kept a star of nobody (the visitor spawn's
fault at 0xC32817).

1. The guest's game has a mirror empire "Zed" (`coop.space allies fake`: the same apply() as a partner's roster) with one
   colony P1 in a free star S1.
2. The guest visits S1 and flies home: the game lists S1 with Zed's empire.
3. Zed's colony moves to P2 in another star (`allies fake Zed P2`: Zed lost P1): apply() gives S1 back to nobody. The
   list must have no star without an empire, now and over 20 s (the visitor spawn: about 6 % a second), and no fault.
   0.7.16.1 (SPORE_FIX_OFF=allies.apply_near_rebuild): S1 stays in the list with no empire (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_mirror_near.py
"""
import re
import time

from coop_space_allies import no_movie, place, sp, star_info, travel, wait
from coop_space_allies_near import near
from coop_space_allies_reload import free_planets
from twoinst import Checks, keyframe, start


def main():
    host, guest = start('space')
    ck = Checks()
    for c in (host, guest):
        no_movie(c, esc=True)
    home = place(guest)[1]
    free = free_planets(guest)
    stars = []
    for p in free:
        s = int(p, 16) & 0xFFFFFF
        if s != home and s not in [x for _, x in stars]:
            stars.append((p, s))
    ck(len(stars) >= 2, f'two free planets in two free stars near home: {stars[:3]}')
    if len(stars) < 2:
        return ck.result()
    (p1, s1), (p2, s2) = stars[0], stars[1]

    # 1. Zed's colonies in S1 and S2 (having S2 already means dropping S1 later does not call AddStar for a new star)
    print('  fake 1:', sp(guest, 'allies fake Zed', p1, p2).splitlines()[0], flush=True)
    gi = star_info(guest, s1)
    ck(gi is not None and gi[1] == "Zed's empire", f'star {s1} is Zed\'s in the guest\'s game ({gi and gi[1]})')
    if not gi:
        return ck.result()

    # 2. the guest visits S1 and comes home: the game lists S1 among the owned stars nearby
    print('  guest drive:', sp(guest, 'tool add interstellardrive'), flush=True)
    no_movie(guest, esc=True)
    ck(travel(guest, s1, 'guest') is not None, f'the guest is in Zed\'s star system {s1}')
    time.sleep(2)
    back = None
    for _ in range(3):  # a badge cinematic of the visit may refuse the jump first
        no_movie(guest)
        back = travel(guest, home, 'guest')
        if back is not None:
            break
    ck(back is not None, 'the guest is home again')
    no_movie(guest)
    time.sleep(2)
    listed, stale, text = near(guest)
    print('  near list at home:', text, flush=True)
    if s1 not in listed:
        print('  (S1 is not in the near list: nothing to clean)', flush=True)
        ck.faults(host=host, guest=guest)
        return ck.inconclusive(f'the game did not list Zed\'s star {s1} after the visit')
    ck(stale == 0, f'the game lists Zed\'s star {s1} with its empire')

    # 3. Zed's colony goes from S1 (moves to S2)
    print('  fake 2:', sp(guest, 'allies fake Zed', p2).splitlines()[0], flush=True)
    gi = star_info(guest, s1)
    ck(gi is not None and gi[0] == 'FFFFFFFF', f'star {s1} is nobody\'s again ({gi and gi[0]})')
    listed, stale, text = near(guest)
    print('  near list right after:', text, flush=True)
    ck(stale == 0, f'no star of the list is without an empire (stale {stale})')
    worst = stale
    t0 = time.time()
    while time.time() - t0 < 20:
        worst = max(worst, near(guest)[1])
        time.sleep(2)
    ck(worst == 0, f'over 20 s no star of the list without an empire (worst {worst})')
    keyframe('mirror_near', 'the guest at home after Zed\'s colony moved away: the galaxy shows Zed\'s new star, the old one '
             'free; no crash', players={'host': host, 'guest': guest})
    sp(guest, 'allies drop')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
