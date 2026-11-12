#!/usr/bin/env python3
"""Tribe: while the host's game-over caption is up the guest's game waits for it (BUG-0960).

When every member of the player tribe is knocked out the game-over caption ("Alas! Your population has fallen below
the minimum ...") comes on the host only and stands the host's game until its player answers it; the guest's game
showed nothing and played on against the paused host (lab 20261112-093040, tribe_gameover_partner).

Two games on the tribe fixture, 2 more members. The host's game knocks out every member of the player tribe (the
host's and the guest's, `coop.tribe pincer` with an NPC member the attacker) until its caption comes. Checked on the
games' own state: the guest's game time stands within 6 s of the host's caption (`busy: paused`: the game's
cGameTimeManager, not a counter) and its own caption does not come (no input lock of its own); a key frame of both.
Then the host answers (Enter, as play_tribe's rule does): the host comes back to Tribe play and the guest's time
runs again within 20 s. Without the fix (SPORE_FIX_OFF=tribe.gameover_wait) the guest plays on: its time never
stands.

  SPORE_LANE=5 python3 tools/tests/coop_tribe_gameover_wait.py
"""
import re
import time

from play_tribe import caption_up, tribe_state
from twoinst import Checks, game_runs, keyframe, start


def busy(con):
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1).split(',') if m else ['?']


def mode(con):
    return [w for w in con.call('game.mode')['out'].split() if not w.startswith('0x')][:1]


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip()[:80], flush=True)
    time.sleep(6)
    hs = tribe_state(host)
    members = [i for i, v in hs.items() if v['tribe'] == 'p']
    enemy = [i for i, v in hs.items() if v['tribe'] != 'p' and v['hp'] > 0]
    print(f'  player tribe {members} (owners {[hs[i]["owner"] for i in members]}), an enemy {enemy[:1]}', flush=True)
    if not ck(members and enemy, 'the player tribe and another tribe\'s member'):
        return ck.result()
    print(f'  before: host busy {busy(host)}, guest busy {busy(guest)}', flush=True)
    t0 = time.time()
    while time.time() - t0 < 90 and not caption_up(host):
        hs = tribe_state(host)
        for i in [i for i in members if i in hs and hs[i]['hp'] > 0]:
            host.call(f'coop.tribe pincer {i} {enemy[0]} 5000')
        time.sleep(1.5)
    t_cap = time.time()
    up = caption_up(host)
    print(f'  the host\'s caption after {t_cap - t0:.0f} s: {up}', flush=True)
    if not ck(up, 'the game over came on the host (every member knocked out)'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    waited, gb = None, []
    while time.time() - t_cap < 6:
        gb = busy(guest)
        if 'paused' in gb:
            waited = time.time() - t_cap
            break
        time.sleep(0.3)
    print(f'  guest busy {gb} ({waited and round(waited, 1)} s after the host\'s caption); host busy {busy(host)}; '
          f'{next((ln.strip() for ln in guest.call("coop.status")["out"].splitlines() if "game over wait" in ln), "")}',
          flush=True)
    ck(waited is not None, f'the guest\'s game waits (its time stands) while the host\'s caption is up ({gb})')
    ck('input_lock' not in gb and not caption_up(guest), 'the guest shows no game over of its own (its OK would '
                                                         'reload its own save)')
    time.sleep(1)
    keyframe('caption', 'the host in the game-over caption ("Alas! Your population ..."), the guest\'s game standing '
                        'with the co-op note that it waits for the host', settle=0)
    host.call('key enter')
    t1, back = time.time(), False
    while time.time() - t1 < 60 and not back:
        back = not caption_up(host) and mode(host) == ['Tribe']
        time.sleep(1)
    ck(back, 'the host answered the caption and is back in Tribe play')
    t2, runs, gb = time.time(), False, []
    while time.time() - t2 < 20 and not runs:
        gb = busy(guest)
        runs = 'paused' not in gb
        time.sleep(0.5)
    print(f'  guest busy {gb} {time.time() - t2:.1f} s after the host was back', flush=True)
    ck(runs, f'the guest\'s game goes on after the host\'s answer ({gb})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
