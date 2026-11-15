#!/usr/bin/env python3
"""Tribe: while the host's "Your tribal hut has been destroyed" caption is up the guest's game waits (BUG-1081).

The players' own hut destroyed in the host's game brings the game's other defeat caption ("Oh, the humanity! Your
tribal hut has been destroyed! Perhaps a fresh start is just the thing you need.", its OK reloads the host's save)
and stands the host's game; the guest's game played on for 45 s (lab 20261113-132309-2, tribe_own_hut_fall).
BUG-0960's wait (tribe.gameover_wait) held the guests only for the tribe knocked out.

Two games on the tribe fixture. The host's game gives the players' own hut the game's killing blow
(`coop.tribe hutfall`, docs/06-testing.md#testing-own-hut-fall) until its caption comes. Checked on the games' own
state: the guest's game time stands within 6 s of the host's caption (`busy: paused`, the game's cGameTimeManager)
and its own defeat caption does not come; a key frame of both. Then the host answers (Enter): the host's game
reloads and comes back to Tribe play, the guest's time runs again within 20 s and its players' hut stands.
Without the fix (SPORE_FIX_OFF=tribe.hut_defeat_wait) the guest plays on: its time never stands.

  SPORE_LANE=6 python3 tools/tests/coop_tribe_hut_defeat_wait.py
"""
import re
import time

from coop_tribe_conquest_guest import near, planet
from coop_tribe_own_hut_fall import home_of, hut_near
from twoinst import Checks, game_runs, keyframe, start

HUT_CAPTION = 'humanity'


def caption_up(con, text=HUT_CAPTION):
    """the game's caption with that text is shown: its window and every parent visible (as play_tribe.caption_up)"""
    out = con.call(f'coop.ui find "{text}"').get('out') or 'no window'
    if 'no window' in out:
        return False
    fl = [int(m, 16) for m in re.findall(r' fl=0x([0-9A-Fa-f]+) ', out)]
    return bool(fl) and all(f & 1 for f in fl)


def busy(con):
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1).split(',') if m else ['?']


def mode(con):
    return [w for w in con.call('game.mode')['out'].split() if not w.startswith('0x')][:1]


def wait_line(con):
    return next((ln.strip() for ln in con.call('coop.status')['out'].splitlines() if 'game over wait' in ln), '')


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(3)
    home = home_of(host)
    own = hut_near(host, home) if home else None
    if not ck(bool(own), f"the host's game has the players' hut ({own}, village at {home})"):
        return ck.result()
    _, own_p = own
    print(f'  before: host busy {busy(host)}, guest busy {busy(guest)}', flush=True)
    print('  host: ' + host.call('coop.tribe hutfall %.2f %.2f %.2f' % own_p)['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 30 and not caption_up(host):
        time.sleep(0.5)
    t_cap = time.time()
    up = caption_up(host)
    print(f'  the host\'s caption after {t_cap - t0:.1f} s: {up}; host busy {busy(host)}', flush=True)
    if not ck(up, 'the hut-destroyed caption came on the host'):
        keyframe('no-caption', 'the host after the blow on its own hut')
        ck.faults(host=host, guest=guest)
        return ck.result()
    waited, gb = None, []
    while time.time() - t_cap < 6:
        gb = busy(guest)
        if 'paused' in gb:
            waited = time.time() - t_cap
            break
        time.sleep(0.3)
    print(f'  guest busy {gb} ({waited and round(waited, 1)} s after the host\'s caption); host: {wait_line(host)}; '
          f'guest: {wait_line(guest)}', flush=True)
    ck(waited is not None, f'the guest\'s game waits (its time stands) while the host\'s caption is up ({gb})')
    # still standing a while later (the host repeats its news every 3 s)
    time.sleep(8)
    gb2 = busy(guest)
    ck('paused' in gb2 and caption_up(host), f'the guest still waits 8 s later while the host\'s caption is up ({gb2})')
    ck('input_lock' not in gb2 and not caption_up(guest) and not caption_up(guest, 'Alas'),
       'the guest shows no defeat caption of its own (its OK would reload its own save)')
    keyframe('caption', 'the host in the "Your tribal hut has been destroyed" caption, the guest\'s game standing '
                        'with the co-op note that it waits for the host', settle=0)
    host.call('key enter')
    t1, back = time.time(), False
    while time.time() - t1 < 90 and not back:
        back = not caption_up(host) and mode(host) == ['Tribe'] and 'paused' not in busy(host)
        time.sleep(1)
    ck(back, 'the host answered the caption, reloaded and is back in Tribe play')
    t2, runs, gb = time.time(), False, []
    while time.time() - t2 < 20 and not runs:
        gb = busy(guest)
        runs = 'paused' not in gb
        time.sleep(0.5)
    print(f'  guest busy {gb} {time.time() - t2:.1f} s after the host was back; guest: {wait_line(guest)}', flush=True)
    ck(runs, f'the guest\'s game goes on after the host\'s answer ({gb})')
    time.sleep(5)
    hrow, grow = near(planet(host, 'hut'), own_p, 10), near(planet(guest, 'hut'), own_p, 10)
    print(f"  the players' hut after the reload: host {hrow}, guest {grow}", flush=True)
    ck(hrow and not hrow[2] & 2 and grow and not grow[2] & 2,
       "the players' hut stands again in both games after the host's reload")
    ck(mode(guest) == ['Tribe'] and not caption_up(guest), 'the guest plays on in Tribe without a caption')
    keyframe('after', 'both games back in Tribe play after the host\'s reload, the players\' hut standing')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
