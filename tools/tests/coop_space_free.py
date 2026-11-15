#!/usr/bin/env python3
"""Space after the tutorial: galaxy travel to stars that are not own stars (BUG-0181).

The `space_empire` fixture is still inside the tutorial: a galaxy click on a star that is neither an own
star nor the tutorial's target shows a red "Distance" and the ship stays (docs/06-testing.md 6.15). That is
the tutorial lock cMissionManager +0x18 mbMission201Finished (docs/re/space.md#space-tutorial-lock).
`space_free` is `space_empire` + the release cheat `spaceCreate` (m201=1).

1. Two games on the fixture; `coop.space tutorial` on both: the lock as each game has it (each game has its
   own mission manager; the guest's comes from the host's save).
2. Both games zoom out to the galaxy view. The host clicks the nearest unowned star (a real click on the
   star's screen point): its ship goes there (the host's own galaxy position reaches the star).
3. The guest's game draws the host's ship tag at that star (the tag's point = the star's projected point).
4. The guest clicks another unowned star: its ship goes there, the host's game draws the guest's tag there.
PASS: 2, 3, 4. FIXTURE=space_empire (the lock on) must FAIL at 2 and 4 (the ships stay home).

  SPORE_LANE=3 python3 tools/tests/coop_space_free.py
"""
import os
import re
import time

from coop_space_jump import settle
from coop_space_map import projected, sp, tags, to_galaxy, wait
from twoinst import Checks, shot, start

FIXTURE = os.environ.get('FIXTURE', 'space_free')


def gpos(con, who):
    """a player's galaxy position (`map roster <id>`: 0 the host, 1 the guest, on either game)"""
    m = re.search(rf'roster {who} .* at (\S+) (\S+) (\S+) empire', sp(con, 'map roster', who))
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def d(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5 if a and b else 1e9


def stars_near(con):
    out = []
    for line in sp(con, 'map planets').splitlines():
        m = re.match(r"star (\d+) '([^']*)' empire (\w+) type \d+ at (\S+) (\S+) (\S+)", line)
        if m:
            out.append(dict(id=int(m.group(1)), name=m.group(2), empire=m.group(3),
                            pos=tuple(float(m.group(i)) for i in (4, 5, 6))))
    return out


def travel(con, who, me, avoid):
    """a real click on the nearest unowned star on the screen; (star, arrived)"""
    here = gpos(con, me)
    cands = sorted((d(s['pos'], here), s['id'], s) for s in stars_near(con)
                   if s['empire'] == 'FFFFFFFF' and s['id'] not in avoid and d(s['pos'], here) > 0.3)
    print(f'  {who}: {len(cands)} unowned stars near {here}', flush=True)
    for _, _, s in cands:
        pt = projected(con, s['id'])
        if not pt or not (100 < pt[0] < 1340 and 100 < pt[1] < 900):
            continue
        print(f'  {who}: star {s["id"]} {s["name"]} {d(s["pos"], here):.2f} away at screen {pt}', flush=True)
        con.call(f'move {pt[0]} {pt[1]}')
        time.sleep(0.6)
        con.call(f'click {pt[0]} {pt[1]}')
        time.sleep(2)
        shot(con, f'free_click_{who}.png')
        ok = wait(lambda: d(gpos(con, me), s['pos']) < 0.3, 45, 1.5) is not None
        print(f'  {who}: ship at {gpos(con, me)}, star at {s["pos"]}: {"arrived" if ok else "stayed"}', flush=True)
        return s, ok
    return None, False


def tag_at(con, who_id, star):
    pt = projected(con, star)
    t = [x for x in tags(con) if x['kind'] == 'player' and x['id'] == who_id and x['at'][0] >= 0]
    return bool(t and pt and abs(t[0]['at'][0] - pt[0]) <= 25 and abs(t[0]['at'][1] - pt[1]) <= 25), t, pt


def main():
    host, guest = start(FIXTURE)
    ck = Checks()
    locks = {}
    for con, who in ((host, 'host'), (guest, 'guest')):
        out = sp(con, 'tutorial')
        print(f'  {who}: {out} | {sp(con, "empire").splitlines()[0][:160]}', flush=True)
        m = re.search(r'm201=(\d)', out)
        locks[who] = m.group(1) if m else '?'
        print(f'  {who} tools: {" ".join(sp(con, "tools").split())[:400]}', flush=True)
    print(f'  fixture {FIXTURE}: tutorial finished (m201) host {locks["host"]} guest {locks["guest"]}', flush=True)

    # 2. the host travels by a click in the galaxy view
    ck(to_galaxy(host) and to_galaxy(guest), 'both games in the galaxy view')
    for con, who in ((host, 'host'), (guest, 'guest')):  # (inside the tutorial: the galaxy caption waits for OK)
        settle(con, who)
    time.sleep(3)
    s1, ok = travel(host, 'host', 0, set())
    ck(s1 is not None, 'an unowned star on the host\'s screen')
    ck(ok, f'the host\'s ship went to the unowned star {s1["name"] if s1 else "?"} (the game let it travel)')
    # 3. what the guest's game draws
    if s1 and ok:
        r = wait(lambda: tag_at(guest, 0, s1['id'])[0], 20)
        _, t, pt = tag_at(guest, 0, s1['id'])
        shot(guest, 'free_guest_sees_host.png')
        ck(r is not None, f'the guest\'s game draws Alice\'s ship at {s1["name"]} ({t[0]["at"] if t else None} ~ {pt})')
    # 4. the guest travels in its own game
    s2, ok2 = travel(guest, 'guest', 1, {s1['id']} if s1 else set())
    ck(s2 is not None, 'an unowned star on the guest\'s screen')
    ck(ok2, f'the guest\'s ship went to the unowned star {s2["name"] if s2 else "?"} (its own game let it travel)')
    if s2 and ok2:
        r = wait(lambda: tag_at(host, 1, s2['id'])[0], 20)
        _, t, pt = tag_at(host, 1, s2['id'])
        shot(host, 'free_host_sees_guest.png')
        ck(r is not None, f'the host\'s game draws Bob\'s ship at {s2["name"]} ({t[0]["at"] if t else None} ~ {pt})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
