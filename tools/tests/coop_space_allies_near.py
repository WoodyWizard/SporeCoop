#!/usr/bin/env python3
"""Space: when the allies mirror goes, the game's list of owned stars nearby forgets its star (BUG-1144).

The Space game keeps the stars near the current one that an empire owns (cSimulatorSpaceGame +0x30 -> +0x7C, star
flag 0x10 / 0x20), rebuilt only on the game's own star messages. Its NPC visitor spawn (0x1009070) picks one of them
and calls 0xC32810 on GetEmpire(the star's owner) without a null check: the game never lists a star nobody owns. The
mirror of a partner's colony (space_allies.cpp) owns a star in the guest's galaxy; a save (the mirror goes for the
save and comes back 3 s later) or the session's end gives the star back to nobody while the list still held it: the
next visitor spawn faulted at 0xC32817 (lab 20261115-030136 of feat/w51-space, "cEmpire_MakeContact+0x3C7").

1. The host founds a colony in a free star system after the join: the guest's copy shows it as "Alice's empire".
2. The guest visits that star and flies home: the game's list holds the mirror's star, with its empire.
3. The guest saves at home (the mirror goes for the save): no star of the list is without an empire.
4. The host leaves (the mirror goes for good); the guest stays 20 s in its home system: still none, no fault.
PASS: 1 to 4 and no fault. SPORE_FIX_OFF=allies.near_rebuild: the list keeps the star of nobody at 3 (FAIL), and a
visitor spawn in the window faults (about one run in three without the checks of the list).

  SPORE_LANE=6 python3 tools/tests/coop_space_allies_near.py
"""
import re
import time

from coop_space_allies import VK_RETURN, no_movie, place, sp, star_info, status, travel, wait
from twoinst import Checks, start


def near(con):
    """(listed stars, stale count, text) of `coop.space allies near`"""
    out = sp(con, 'allies near')
    stars = [int(x) for x in re.findall(r'^star (\d+) ', out, re.M)]
    m = re.search(r'stale=(\d+)', out)
    return stars, (int(m.group(1)) if m else -1), out.replace('\n', ' | ')


def main():
    host, guest = start('space')
    ck = Checks()
    for c in (host, guest):
        no_movie(c, esc=True)
    home = place(host)[1]

    # 1. the host's colony after the join: the guest's mirror
    free = [int(x) for x in re.findall(r'free (\d+)', sp(host, 'allies free'))]
    ck(bool(free), f'free stars near home: {free[:4]}')
    if not free:
        return ck.result()
    star = free[1] if len(free) > 1 else free[0]  # (the nearest is the tutorial's ruined colony)
    ck(travel(host, star, 'host') is not None, f'the host is in the system of star {star}')
    print('  host:', sp(host, 'allies colonize').splitlines()[0], flush=True)
    ck(travel(host, home, 'host') is not None, 'the host flies home')
    gi = wait(lambda: (lambda i: i if i and i[1] == "Alice's empire" else None)(star_info(guest, star)), 30)
    ck(gi is not None, f'the guest\'s copy: star {star} owned by "Alice\'s empire" ({gi and gi[0]})')
    if not gi:
        return ck.result()

    # 2. the guest visits the mirror's star and comes home: the game lists it among the owned stars nearby
    print('  guest drive:', sp(guest, 'tool add interstellardrive'), flush=True)
    no_movie(guest, esc=True)
    ck(travel(guest, star, 'guest') is not None, 'the guest is in the mirror\'s star system')
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
    stars, stale, text = near(guest)
    print('  guest near list at home:', text, flush=True)
    ck(star in stars and stale == 0, f'the game lists the mirror\'s star {star} with its empire')

    # 3. the save (the mirror goes for it): no listed star without an empire
    saves0 = status(guest).get('saves', 0)
    print('  guest save:', guest.call('coop.save')['out'].strip(), flush=True)
    ck(wait(lambda: status(guest).get('saves', 0) > saves0, 20, 0.2), 'the guest\'s save went through the hook (mirror out)')
    stars, stale, text = near(guest)
    held = status(guest).get('hold', 0)
    print(f'  guest near list right after the save (hold={held}):', text, flush=True)
    ck(stale == 0, f'after the save no star of the list is without an empire (stale {stale})')
    for _ in range(6):  # the "game saved" box
        guest.call(f'key {VK_RETURN}')
        time.sleep(1)
    ck(wait(lambda: status(guest).get('applied', 0) == 1, 20), 'after the save the mirror is back')

    # 4. the session ends: the mirror goes for good; the guest stays in its home system
    print('  host leaves:', host.call('coop.leave')['out'].strip(), flush=True)
    gone = wait(lambda: (lambda i: i if i and i[0] == 'FFFFFFFF' else None)(star_info(guest, star)), 45)
    ck(gone is not None, 'the session ended: the star is nobody\'s again in the guest\'s galaxy')
    worst = 0
    t0 = time.time()
    while time.time() - t0 < 20:  # the NPC visitor spawn has a chance of about 6 % a second
        worst = max(worst, near(guest)[1])
        time.sleep(2)
    stars, stale, text = near(guest)
    print('  guest near list after the session:', text, flush=True)
    ck(worst == 0 and stale == 0, f'after the session no star of the list is without an empire (worst {worst})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
