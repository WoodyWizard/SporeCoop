#!/usr/bin/env python3
"""Tribe: an editor visit of the host keeps the co-op epic tracked (BUG-0217).

1. Two games on the `tribe` fixture. The host spawns the co-op epic (`coop.tribe epic`): the game
   lists one more live epic, the mod tracks it.
2. The host opens an editor through the evolution vote (`coop.vote editor`, the guest's real
   "I'm ready" click; in Tribe the request carries a member's species) and leaves it with the red X.
   In the editor and after it the epic is still the tracked one.
3. The world share that follows the editor ("evolved the species") saves the host's world: the
   epic left before that save (BUG-0121's guard), so the host's game has no co-op epic any more.
   (The guest's game lists no live epic at all, the planet's own one included: printed only.)
4. The host ends its session: no co-op epic stays.

--nofix: `coop.fix tribe.editor_world off` (the old trigger: every editor frame is a world change):
the editor visit drops the epic untracked, so it stays alive through the share save; must FAIL.

  SPORE_LANE=5 python3 tools/tests/coop_tribe_editor_world.py [--nofix]
"""
import argparse
import re
import time

from coop_tribe import members
from coop_ui import Ui
from twoinst import Checks, start

CANCEL = (1380, 1062)  # the red X (leave without saving) of the creature editor (1440x1080 test window)
TRIBE = '01654C02'


def mode(con):
    return con.call('game.mode')['out'].split()[0] if con.call('game.mode')['out'].split() else ''


def herds(con):
    out = con.call('coop.tribe epic herds')['out']
    v = {k: x for k, x in re.findall(r'(\w+)=(\w+)', out)}
    return v, out.strip()


def num(v, k, d=-1):
    try:
        return int(v.get(k, d))
    except ValueError:
        return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    gu = Ui(guest, 'guest')
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix tribe.editor_world off')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 90:  # the split of the player tribe arrives with the first states
        if any(v[0] == 1 for v in members(host).values()):
            break
        time.sleep(3)
    m0 = mode(host)
    print('  host mode', m0)
    h0, out0 = herds(host)
    print('  before:', out0)
    base = num(h0, 'live_epics', 0)
    gbase = num(herds(guest)[0], 'live_epics', 0)
    print('  guest before:', herds(guest)[1])

    # 1. the co-op epic
    print('  ', host.call('coop.tribe epic')['out'].strip())
    time.sleep(2)
    h1, out1 = herds(host)
    print('  epic alive:', out1)
    if not ck(num(h1, 'live_epics') == base + 1 and h1.get('tracked', '0').strip('0'), 'the co-op epic lives and is tracked'):
        return ck.result()

    # 2. the host's editor through the vote, then the red X (the species' parts are not all in
    # this editor's list, so its Done is blocked; leaving without changes is the same world)
    print(' ', host.call('coop.vote editor')['out'].strip().splitlines()[0])
    time.sleep(2)
    if gu.wait(lambda s: s['ready']['vis'], 10, 'ready button'):
        gu.click('ready')
    t0 = time.time()
    while time.time() - t0 < 90 and mode(host) == m0:
        time.sleep(1)
    m_ed = mode(host)
    print(f'  host mode in the editor: {m_ed} ({time.time() - t0:.0f} s)')
    if not ck(m_ed != m0, f'the host is in an editor ({m_ed})'):
        return ck.result()
    time.sleep(3)
    he, oute = herds(host)
    print('  in the editor:', oute)
    ck(he.get('tracked') == h1.get('tracked'), 'in the editor the co-op epic is still tracked')
    g0 = num(herds(guest)[0], 'resets', 0)
    host.call('wait.frames 200 60000')
    host.call('move %d %d' % CANCEL)
    time.sleep(0.5)
    host.call('click %d %d' % CANCEL)
    t0 = time.time()
    while time.time() - t0 < 90 and mode(host) != m0:
        time.sleep(1)
    if not ck(mode(host) == m0, f'the host is back in Tribe ({mode(host)})'):
        return ck.result()

    # 3. the world share after the editor ("evolved the species"): the guest loads it
    t0 = time.time()
    while time.time() - t0 < 150:
        if num(herds(guest)[0], 'resets', 0) > g0 and mode(guest) == m0 and guest.call('coop.tribe')['ok']:
            break
        time.sleep(2)
    print(f'  the guest loaded the shared world after {time.time() - t0:.0f} s')
    time.sleep(8)
    h2, out2 = herds(host)
    hg, outg = herds(guest)
    print('  host after the share:', out2)
    print('  guest after the share:', outg)
    ck(num(h2, 'live_epics', 99) == base,
       'the host\'s game has no co-op epic left: it left before the share save (BUG-0121 guard ran)')
    # (printed only: the guest's game lists no live epic even for the planet's own one, 0 before and after)

    # 4. the session end: nothing of ours stays
    print('  ', host.call('coop.leave')['out'].strip())
    time.sleep(3)
    h3, out3 = herds(host)
    print('  host after its leave:', out3)
    ck(num(h3, 'live_epics', 99) == base, 'no co-op epic left after the session')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
