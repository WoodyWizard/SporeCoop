#!/usr/bin/env python3
"""Tribe: the host's game over while the guest's members live, and the play's rule that answers it (BUG-0922).

The 0.7.12 final check's play_tribe ended with the host in the game-over caption ("Alas! Your population has fallen
below the minimum ...") for its last 40 s while the guest played on: the play pressed OK only when no member of the
player tribe had health on the host, and the guest's members are in the host's player tribe too.

1. Two games on the tribe fixture, 2 more members (the player tribe's members are split between the players). The
   host's game knocks out every member of the HOST (owner 0, the chief too) with the game's own blows (`coop.tribe
   pincer`, an NPC member the attacker); the guest's members keep their health.
2. Whether the game shows the caption is the game's; when it does, play_tribe's rule (play_tribe.GameOver, the same
   object the play uses) must press OK within 25 s of it, and the host bounce back (the caption gone, Tribe play).
   With the members rule (PLAY_TRIBE_OLD_GAMEOVER=1, the rule before the fix) it never fires while a guest member
   lives: the host is still in the caption 40 s later (FAIL).
Key frame `caption`: both games while the caption is up (the host's caption, the guest's play).

  SPORE_LANE=5 python3 tools/tests/coop_tribe_gameover_partner.py
"""
import time

from play_tribe import GameOver, caption_up, tribe_state
from twoinst import Checks, game_runs, keyframe, start


def mode(con):
    return (con.call('game.mode').get('out') or '').split()[-1:]


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip()[:80], flush=True)
    time.sleep(6)
    hs = tribe_state(host)
    mine = [i for i, v in hs.items() if v['tribe'] == 'p' and v['owner'] == 0]
    theirs = [i for i, v in hs.items() if v['tribe'] == 'p' and v['owner'] != 0]
    enemy = [i for i, v in hs.items() if v['tribe'] != 'p' and v['hp'] > 0]
    print(f'  host members {mine}, guest members {theirs}, an enemy {enemy[:1]}', flush=True)
    if not ck(mine and theirs and enemy, 'the player tribe has the host\'s and the guest\'s members, another tribe one'):
        return ck.result()
    t0 = time.time()
    while time.time() - t0 < 60:
        hs = tribe_state(host)
        alive = [i for i in mine if i in hs and hs[i]['hp'] > 0]
        if not alive:
            break
        for i in alive:
            host.call(f'coop.tribe pincer {i} {enemy[0]} 5000')
        time.sleep(1)
    hs = tribe_state(host)
    living = [i for i in theirs if i in hs and hs[i]['hp'] > 0]
    print(f'  host members with health {[i for i in mine if i in hs and hs[i]["hp"] > 0]}, guest members {living}',
          flush=True)
    ck(living, f'the guest\'s members keep their health ({living})')

    rule = GameOver()
    print(f'  the play\'s game-over rule: {"members (old)" if rule.old else "the caption"}', flush=True)
    t1, seen, pressed, shot = time.time(), None, None, False
    while time.time() - t1 < 70:
        up = caption_up(host)
        if up and seen is None:
            seen = time.time() - t1
        if up and not shot and seen is not None and time.time() - t1 > seen + 1:
            keyframe('caption', 'the host in the game-over caption ("Alas! Your population has fallen below the '
                     'minimum..."), the guest in play: the caption must be closed by the play within seconds')
            shot = True
        if rule.due(host):
            host.call('key enter')
            pressed = time.time() - t1
            break
        if seen is not None and time.time() - t1 > seen + 40:
            break
        time.sleep(0.5)
    print(f'  caption on the host after {seen if seen is None else round(seen, 1)} s; OK pressed at '
          f'{pressed if pressed is None else round(pressed, 1)} s', flush=True)
    if seen is None:  # the game kept the tribe alive with the guest's members: no game over to answer
        print('  (no caption: the host\'s game counts the guest\'s members)', flush=True)
        ck(not caption_up(host), 'no game over on the host while the guest\'s members live')
    else:
        ck(pressed is not None and pressed - seen < 25,
           f'the play\'s rule closes the caption within 25 s (seen {seen:.0f} s, OK at {pressed})')
        t2, back = time.time(), False
        while pressed is not None and time.time() - t2 < 60 and not back:
            back = not caption_up(host) and mode(host) == ['Tribe']
            time.sleep(1)
        ck(back, 'the host bounced back: no caption, Tribe play')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
