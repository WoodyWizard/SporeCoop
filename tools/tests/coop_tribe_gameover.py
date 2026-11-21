#!/usr/bin/env python3
"""Tribe: a member added while the game-over caption is up (BUG-0172).

When no member of the player tribe has health left the game shows "Alas! Your population has
fallen below the minimum needed to survive ... Bounce back in time" and waits for OK (the bounce
back reloads the last save). A member added to the player tribe meanwhile has no species fields;
the OK then faulted at 0xC0DAE3 and the host froze for good (2 of 2 tries, 2026-10-23).

1. The host knocks out every member of the player tribe through the game's damage path
   (`coop.tribe pincer <member> <enemy> 300`), the caption comes.
2. `coop.tribe spawn 2` on the host (what a guest's mirror spawn does on its machine): with the fix
   no member is added to a tribe that is out.
3. OK (`key enter`): the host bounces back and keeps drawing, no fault in either game.

--nofix: the old way (`coop.tribe fix gameover 0`): must FAIL.
--same-frame: the killing blows on the last two members in one call (one frame): the game shows no game over at all
(BUG-1337, the game's own rule of BUG-1083): the twin of the one-at-a-time blows, must FAIL.

Step 1's blows (BUG-1337): one member at a time, the tribe's leader last, one blow a second. The game posts its game
over at a member's fall only when fewer than 2 members are left in its up list (cTribe +0x354, 0xC8F500); a fallen
member leaves that list at its next behaviour tick, so two members falling in one tick both still count each other and
no game over comes (docs/re/tribe.md#tribe-gameover-list). The old blows (every member with health, back to back, each
second) did that now and then: lab 20261118-033459 (v0.7.14.1, tribe_gameover #3) and 20261119-214823 (0.7.16,
tribe_gameover_reload2): no caption, the chief knocked out, the tribe left with it alone.
--after-ok S (BUG-0172, 0.7.11 N1): the spawn comes S seconds after the OK instead, while the bounce back reloads
the save (the member's creation then meets a tribe being torn down); --count N members.

  SPORE_LANE=5 python3 tools/tests/coop_tribe_gameover.py [--nofix]
"""
import argparse
import re
import time

import twoinst
from twoinst import Checks, start


def player_members(con):
    """{id: hp} of the player tribe, [enemy ids]"""
    own, enemy, player = {}, [], False
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)', line)
        if m:
            player = 'player' in line
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) age=\d+ sel=\d+ hp=([\d.]+)', line)
        if m and m.group(1) != '0':
            if player:
                own[int(m.group(1))] = float(m.group(3))
            else:
                enemy.append(int(m.group(1)))
    return own, enemy


def leaders(con):
    """the ids of the tribes' leaders (the game's own 0xC23430)"""
    return {int(m.group(1)) for m in re.finditer(r'#(\d+) \S+ owner=.* leader=1 ', con.call('coop.tribe')['out'])}


def knock_out(host, secs=60, dmg=300, same_frame=False):
    """Knock out every member of the host's player tribe through the game's damage path for a game over (BUG-1337,
    BUG-1399): one blow a second on one member at a time, the tribe's leader last (a leader with others up stops at 0.3,
    the partner's rescue too), so no two members fall in one behaviour tick (then the game shows no game over at all,
    docs/re/tribe.md#tribe-gameover-list). same_frame: the twin, the last two killing blows in one call.
    Returns ({id: hp} of the player tribe at the end, [enemy ids])."""
    t0, lead = time.time(), leaders(host)
    own, enemy = player_members(host)
    while time.time() - t0 < secs:
        own, enemy = player_members(host)
        alive = sorted((i for i, hp in own.items() if hp > 0), key=lambda i: (i in lead, i))
        if not alive or not enemy:
            break
        if same_frame and len(alive) == 2 and all(own[i] <= dmg for i in alive):
            print('  the last two in one call:', host.call(f'coop.tribe pincer {alive[0]},{alive[1]} {enemy[0]} {dmg}')['out'].strip()[:120])
        else:
            host.call(f'coop.tribe pincer {alive[0]} {enemy[0]} {dmg}')
        time.sleep(1)
    print('  player tribe now', own, flush=True)
    return own, enemy


def frames_go_on(con, secs):
    f0 = twoinst.host_frame(con)
    time.sleep(secs)
    return twoinst.host_frame(con) > f0 + 10


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--after-ok', type=float, default=None)
    ap.add_argument('--count', type=int, default=2)
    ap.add_argument('--same-frame', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.tribe fix gameover 0')['out'].strip())
    time.sleep(6)
    own, enemy = player_members(host)
    print('  player tribe', own, 'enemies', enemy[:4])
    ck(own and enemy, 'the player tribe and another tribe have members')
    if not (own and enemy):
        return ck.result()
    own, enemy = knock_out(host, 60, 300, a.same_frame)  # BUG-1337: one member a second, the leader last
    ck(own and not [i for i, hp in own.items() if hp > 0], 'every member of the player tribe is out')
    time.sleep(15)  # the caption comes some seconds later (play_tribe.py waits 15 s too)
    twoinst.shot(host, 'tribe_gameover_caption.png')
    for word in ('Alas', 'Bounce', 'population'):
        print(f'  ui "{word}":', host.call(f'coop.ui find "{word}"')['out'].strip()[:300])
    if a.after_ok is None:
        print(' ', host.call(f'coop.tribe spawn {a.count}')['out'].strip())
        time.sleep(1)
    host.call('key enter')  # OK: bounce back in time
    if a.after_ok is not None:
        time.sleep(a.after_ok)
        for _ in range(3):
            print(f'  {a.after_ok:.1f} s after OK:', host.call(f'coop.tribe spawn {a.count}')['out'].strip()[:160])
            time.sleep(0.3)
    ok = frames_go_on(host, 15)
    ck(ok, 'the host bounces back and keeps drawing')
    t1 = time.time()
    while time.time() - t1 < 40:  # the bounce back reloads the last save: members with health again
        own, _ = player_members(host)
        if [i for i, hp in own.items() if hp > 0]:
            break
        time.sleep(2)
    print('  player tribe after OK', own)
    ck([i for i, hp in own.items() if hp > 0], 'after the bounce back the player tribe has members again')
    print(' ', [l for l in host.call('coop.status')['out'].splitlines() if 'tribe lifetime' in l])
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
