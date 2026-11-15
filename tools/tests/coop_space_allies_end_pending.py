#!/usr/bin/env python3
"""Space: a session that ended on a mirror planet leaves no old mirror for the next session (BUG-1036, BUG-0038
trigger 5).

The session ends while the player is on a planet of a partner's mirror colony (space_allies): the mirrors' end waits
until the player leaves the planet (g_end_pending). The player then went to the menu and joined another session: the
pending end was skipped outside Space and dropped in the new session, so the old partner's mirror colony came back
in the new galaxy as a frozen ally.

1. Two games on the space fixture. The guest has a mirror empire "Zed" (`coop.space allies fake`: the same code
   path as a partner's colony) on a free planet P, flies to P's star and lands on P.
2. The guest leaves the session there (`allies status`: end_pending=1).
3. The guest goes to the galaxy menu and joins the host again (it loads the host's world).
PASS: in the guest's new galaxy P's star belongs to no "Zed's empire" (`allies star`: the game's star record and its
empire) and the mod keeps no mirror "Zed". SPORE_FIX_OFF=space.allies_end_pending: Zed's colony is back (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_allies_end_pending.py
"""
import re
import time

import lane
from coop_space_allies_reload import free_planets, sp
from coop_space_map import context, no_movie, place, wait
from twoinst import Checks, keyframe, start, wait_guest_in


def owner(con, star):
    m = re.search(r"owner='(.*?)'(?: \(mine\))? tech=", sp(con, 'allies star', star))
    return m.group(1) if m else None


def free_in(con, star):
    """the free planets (tech 0-3, no colony) of a star"""
    return re.findall(r"planet ([0-9A-F]+) '[^']*' tech=([0-3])\b", sp(con, 'allies star', star))


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    free = free_planets(guest)
    if not ck(free, f'a free planet for the mirror: {free}'):
        return ck.result()
    # the ship's active planet after a jump is the one `land` lands on: the mirror goes on it when it is free
    planet = None
    for cand in free:
        star = int(cand, 16) & 0xFFFFFF
        print(f'  guest jumpto {star}:', sp(guest, 'jumpto', star), flush=True)
        if not wait(lambda: place(guest)[1] == star and context(guest) == 'system', 60):
            continue
        no_movie(guest)
        act = place(guest)[2]
        ok = [p for p, _ in free_in(guest, star)]
        print(f'  star {star}: active {act}, free {ok}', flush=True)
        if act in ok:
            planet = act
            break
    if planet is None:
        print('RESULT: SKIP (no free planet was the active one after a jump)', flush=True)
        ck.faults(host=host, guest=guest)
        return 0
    print('  fake:', sp(guest, 'allies fake Zed', planet).splitlines()[0], flush=True)
    ck(owner(guest, star) == "Zed's empire", f'star {star} is Zed\'s in the guest\'s game ({owner(guest, star)})')
    print('  land:', sp(guest, 'land'), flush=True)
    landed = wait(lambda: context(guest) == 'planet' and place(guest)[2] == planet, 40)
    ck(landed, f'the guest landed on the mirror colony {planet} ({sp(guest, "star")})')
    if not landed:
        return ck.result()
    no_movie(guest)
    print('  guest cities:', sp(guest, 'allies cities').replace('\n', ' | ')[:300], flush=True)

    # 2. the session ends on the mirror planet
    guest.call('coop.leave')
    st = wait(lambda: (lambda s: s if 'end_pending=1' in s else None)(sp(guest, 'allies status')), 10)
    print('  guest after leaving:', sp(guest, 'allies status'), flush=True)
    ck(st is not None, 'the mirrors\' end waits: the guest is on the mirror planet')

    # 3. the menu, then the host's session again
    print('  menu:', guest.call('coop.menu')['out'].strip(), flush=True)
    ck(wait(lambda: 'GalaxyGameEntry' in guest.call('game.mode')['out'], 90), 'the guest is in the galaxy menu')
    guest.call('wait.frames 240 90000')
    print('  guest in the menu:', sp(guest, 'allies status'), flush=True)
    guest.call(lane.JOIN)
    ck(wait(lambda: 'Space' in guest.call('game.mode')['out'], 240, 2), 'the guest is in Space again')
    ck(wait_guest_in(host, guest, timeout=150), 'the guest joined the host\'s world')
    no_movie(guest)
    time.sleep(12)  # the allies sync runs every 5 s
    o = owner(guest, star)
    lst = sp(guest, 'allies list')
    print(f'  star {star} owner {o!r}; ' + lst.replace('\n', ' | ')[:400], flush=True)
    keyframe('allies_end_pending', 'the guest back in the host\'s world after a session that ended on a mirror planet',
             players={'host': host, 'guest': guest})
    ck(o != "Zed's empire", f'the old mirror colony is not in the new galaxy (star {star} owner {o!r})')
    ck("mirror 'Zed'" not in lst, 'the mod keeps no mirror of the old session')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
