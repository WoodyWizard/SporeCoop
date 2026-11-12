#!/usr/bin/env python3
"""BUG-0941: a connected guest in Space follows the host into another Space save the host loads.

Both play the `space` fixture (the host's slot Game0). A copy of the fixture goes into the host's slot Game9 (another
save of the same planet name). The host goes to the galaxy menu and loads Game9 while the guest stays connected in
Space. The guest's game must load the world the host sends: its game leaves Space for the load (game.mode) and comes
back playing the received save (the save file its game plays, `coop.space world` game=, is no longer its old one), and
it is in the host's world again.

Before the fix the host sent nothing (the stage stayed Space, the place the same star): the guest played on in its
copy of the old save, the host's ship shown there, with the shared money of two different saves.
SPORE_FIX_OFF=space.load_follow: FAIL (the guest's game never loads).

  SPORE_LANE=2 python3 tools/tests/coop_space_load_follow.py
"""
import re
import time

import fixtures
import lane
from twoinst import Checks, start, wait_guest_in


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def game_key(con):
    m = re.search(r'\bgame=(\S*)', sp(con, 'world'))
    return m.group(1) if m else ''


def mode(con):
    try:
        return con.call('game.mode').get('out', '').strip()
    except OSError:
        return ''


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return cond()


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    name = fixtures.planet_of('space').split('/')[1]
    k_host, k_guest = game_key(host), game_key(guest)
    print(f'  host plays {k_host}; guest plays {k_guest}', flush=True)
    lane.sh('fixture', 'slot', 'space', lane.HOST_PROFILE, '9')
    print('  menu:', host.call('coop.menu')['out'].strip(), flush=True)
    ck(wait(lambda: 'GalaxyGameEntry' in mode(host), 90), 'the host is in the galaxy menu')
    host.call('wait.frames 240 90000')
    print('  load:', host.call(f'coop.load Game9 {name}')['out'].strip(), flush=True)
    seen = set()
    t0 = time.time()
    while time.time() - t0 < 180:
        g = mode(guest)
        seen.add('Space' if 'Space' in g else (g[:40] or '?'))
        if 'Space' in mode(host) and time.time() - t0 > 20 and len(seen) > 1 and 'Space' in mode(guest):
            break
        time.sleep(1)
    k_host2 = game_key(host)
    print(f'  host plays {k_host2} ({mode(host)}); the guest\'s modes meanwhile: {sorted(seen)}', flush=True)
    print('  host:', [ln for ln in host.call('coop.status')['out'].splitlines() if ln.startswith('space save:')], flush=True)
    ck('Space' in mode(host) and k_host2 and k_host2 != k_host, f'the host plays the other save ({k_host2})')
    ck(len(seen) > 1, f'the guest\'s game left Space to load the host\'s world (modes {sorted(seen)})')
    ok = wait(lambda: 'Space' in mode(guest) and game_key(guest) and game_key(guest) != k_guest, 120, 2)
    print(f'  guest plays {game_key(guest)} ({mode(guest)})', flush=True)
    ck(ok, f'the guest\'s game plays the received save, not its copy of the old one ({game_key(guest)}, was {k_guest})')
    ck(wait_guest_in(host, guest, 120), 'the guest is in the host\'s world')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
