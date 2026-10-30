#!/usr/bin/env python3
"""Many rejoins and world loads in a row: the guest leaves and joins again, the host sends its world
again and the guest loads it through the galaxy menu (world.cpp load_slot / via_menu_tick: quit
message, menu, slot switch, LoadGameByName outside the frame). After every round both games' fault
logs (`exc`) must be empty.

  SPORE_LANE=5 python3 tools/tests/coop_rejoin_loop.py [--rounds 30] [--fixture tribe] [--stop]
      [--reshare a:b | --race a:b | --menu a:b [--clicks] | --local-load a:b [--cold] [--clicks]]

Found 2026-10-23 with 3-4 players churning (coop_multi.py): guests faulted in a planet render of the
galaxy menu while a world loaded (0xFB89D0 cTerrainStateMgr_GetTemperatureScore with a null terrain
state, 0xF9E660 / 0xF9E67E / 0xF9E6FE in the same draw): a load started from inside Present disposed
planets whose texture jobs the render frame had already taken (docs/re/space.md 8.3). The direct
repro is `--local-load 0:1.5 --cold` (a load right after the menu came up, in fresh games: 4 of 17
faulted before the fix). `--reshare` also found overlapping world transfers to one player
(checksum mismatch). --stop ends the run at the first fault.
"""
import argparse
import os
import random
import re
import sys
import time

import lane
from coop_rejoin_net import log_times
from twoinst import Checks, alive, start, wait_guest_in


SLOT_WINDOW = '0x02E5A4A8'  # the hotspot over a planet slot ring in the galaxy menu (new_stage_game.py)
# where the slot rings of the test profiles are while the menu still moves (hotspots unplaced)
RINGS = [(384, 376), (470, 597), (564, 261), (704, 288), (806, 278), (857, 290), (480, 288), (1288, 570)]


def slot_centers(con):
    out = []
    for line in con.call('coop.ui tree 3').get('out', '').splitlines():
        m = re.search(r'id=' + SLOT_WINDOW + r' .*?\[(-?\d+) (-?\d+) (-?\d+) (-?\d+)\]', line)
        if m:
            x0, y0, x1, y1 = map(int, m.groups())
            if x0 > 0 and y0 > 0:
                out.append(((x0 + x1) // 2, (y0 + y1) // 2))
    return out or RINGS


def faults(con):
    """(total, [first lines of each record]) of `exc`"""
    out = con.call('exc')['out'].splitlines()
    total = int(out[0].split(':')[1]) if out and out[0].startswith('total faults:') else -1
    return total, [x.strip() for x in out[1:] if x.strip()]


def local_loads(a, rng, host, guest, ck, seen):
    """rounds of: stage -> galaxy menu -> planet view -> load the local save while the view is textured"""
    import glob
    guest.call('coop.leave')
    first_fault = None
    for rnd in range(1, a.rounds + 1):
        if a.cold and rnd > 1:  # the first trip to the menu after the join faulted most (fresh games)
            host, guest = start(a.fixture)
            guest.call('coop.leave')
            for con in (host, guest):
                con.call('exc clear')
            seen = {'host': 0, 'guest': 0}
        saves = sorted(glob.glob(os.path.join(lane.GUEST_DIR, 'Games', 'Game*', '*.spo')))
        saves = [x for x in saves if not x.endswith('snapshot.spo')]
        if not saves:
            ck(False, 'the guest has a save to load')
            break
        # the planet it plays (the host's world): the menu draws it while it opens
        m = re.search(r"world '([^']+)\.spo'", guest.call('coop.avatar').get('out', ''))
        mine = [x for x in saves if m and os.path.basename(x) == m.group(1) + '.spo'] or saves
        mine.sort(key=os.path.getmtime)
        slot, planet = mine[-1].split(os.sep)[-2], os.path.basename(mine[-1])[:-4]
        guest.call('coop.menu')
        t0 = time.time()
        while time.time() - t0 < 30 and 'GalaxyGameEntry' not in guest.call('game.mode')['out']:
            time.sleep(0.2)
        if a.clicks:  # open a planet view first, the delay counts from the click
            time.sleep(rng.uniform(0.5, 2.5))
            x, y = rng.choice(RINGS)
            guest.call(f'move {x} {y}')
            time.sleep(0.2)
            guest.call(f'click {x} {y}')
        time.sleep(rng.uniform(*map(float, a.local_load.split(':'))))
        out = guest.call(f'coop.load {slot} {planet}')['out'].strip()
        t1 = time.time()
        while time.time() - t1 < 60 and alive(lane.GUEST) and not guest.call('coop.avatar')['ok']:
            time.sleep(0.5)
        new = []
        for name, con, inst in (('host', host, lane.HOST), ('guest', guest, lane.GUEST)):
            if not alive(inst):
                new.append(f'{name}: the console stopped answering (crash?)')
                continue
            total, recs = faults(con)
            if total != seen[name]:
                new.append(f'{name}: {total - seen[name]} new fault(s): ' + ' | '.join(recs[:6]))
                seen[name] = total
        back = alive(lane.GUEST) and guest.call('coop.avatar')['ok']
        m = re.search(r'texture job guard \w+, skipped (\d+)(.*)', guest.call('coop.status')['out']) if back else None
        if m and m.group(1) != '0':  # gamefixes.cpp caught a draw of a torn-down planet (the fault without it)
            print(f'    the planet texture guard skipped {m.group(1)} draw(s) in this game{m.group(2)}', flush=True)
        ck(back and not new, f'round {rnd}: {slot}/{planet} ({out}) loaded from the galaxy menu in '
           f'{time.time() - t1:.1f} s' + ('' if back else ' - NOT back in the stage') + ''.join('; ' + x for x in new))
        if new and first_fault is None:
            first_fault = rnd
        if not alive(lane.GUEST) or (a.stop and new):
            break
    print(f'  first fault in round {first_fault}' if first_fault else '  no fault in any round', flush=True)
    if alive(lane.HOST) and alive(lane.GUEST):
        guest.call(lane.JOIN)
        wait_guest_in(host, guest, timeout=60)
        ck.faults(host=host, guest=guest)
    else:
        ck.ok = False
    return ck.result()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='tribe')
    ap.add_argument('--rounds', type=int, default=30)
    ap.add_argument('--limit', type=float, default=90, help='seconds a rejoin with the reload may take')
    ap.add_argument('--gap', default='0.5:3', help='seconds between leave and join (min:max)')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--stop', action='store_true', help='stop at the first fault')
    ap.add_argument('--race', default='',
                    help='min:max seconds: rejoin again that long after the join, without waiting for the reload, '
                         'so the next world arrives while the guest is in the galaxy menu or still loading '
                         '(every 4th round waits for the reload)')
    ap.add_argument('--reshare', default='',
                    help='min:max seconds: once the guest is loading the world again, the host sends it once more '
                         'that long after the load began (coop.share), so it arrives during the load or in the first '
                         'seconds of the stage')
    ap.add_argument('--clicks', action='store_true',
                    help='with --menu or --local-load: meanwhile click the planet slots of the galaxy menu (as the chaos bot did; '
                         'a click on a saved planet zooms to it and builds its terrain sphere)')
    ap.add_argument('--menu', default='',
                    help='min:max seconds: the guest leaves its stage for the galaxy menu (coop.menu) and joins '
                         'that long after the menu came up, so the world arrives in the menu (no stage to leave)')
    ap.add_argument('--local-load', default='',
                    help='min:max seconds: no rejoins; the guest goes to the galaxy menu and loads its own copy of '
                         'the world (coop.load) that long after the menu came up (with --clicks: after it opened a '
                         'planet view by clicking a slot ring), while the menu still draws planet textures')
    ap.add_argument('--cold', action='store_true', help='with --local-load: fresh games for every round')
    ap.add_argument('--cell-leave', action='store_true',
                    help='Cell (BUG-0308): the guest\'s mod must not have applied cell states or ticks to its pool while '
                         'the stage was being left for the reload (`leave_writes=` of coop.pool stays 0)')
    a = ap.parse_args()
    rng = random.Random(a.seed)
    host, guest = start(a.fixture)
    ck = Checks()
    for con in (host, guest):
        con.call('exc clear')
    seen = {'host': 0, 'guest': 0}
    first_fault = None
    times = []
    if a.local_load:
        return local_loads(a, rng, host, guest, ck, seen)
    for rnd in range(1, a.rounds + 1):
        guest.call('coop.leave')
        if a.menu:
            guest.call('coop.menu')
            t0 = time.time()
            while time.time() - t0 < 30 and 'GalaxyGameEntry' not in guest.call('game.mode')['out']:
                time.sleep(0.2)
            wait = rng.uniform(*map(float, a.menu.split(':')))
            t1 = time.time()
            while a.clicks and time.time() - t1 < wait:
                pts = slot_centers(guest)
                if pts:
                    x, y = rng.choice(pts)
                    guest.call(f'move {x} {y}')
                    time.sleep(rng.uniform(0.1, 0.8))
                    if rng.random() < 0.5:
                        guest.call(f'click {x} {y}')
                time.sleep(0.2)
            time.sleep(max(0.0, wait - (time.time() - t1)))
        else:
            time.sleep(rng.uniform(*map(float, a.gap.split(':'))))
        t = time.time()
        guest.call(lane.JOIN)
        if a.menu and a.clicks:  # keep clicking while the world comes and starts to load
            t1, until = time.time(), rng.uniform(0, 5)
            while time.time() - t1 < until and 'GalaxyGameEntry' in guest.call('game.mode')['out']:
                pts = slot_centers(guest)
                if pts:
                    x, y = rng.choice(pts)
                    guest.call(f'click {x} {y}' if rng.random() < 0.5 else f'move {x} {y}')
                time.sleep(rng.uniform(0.1, 0.6))
        deadline = t + a.limit
        if a.race and rnd % 4:
            time.sleep(rng.uniform(*map(float, a.race.split(':'))))
            new = []
            for name, con in (('host', host), ('guest', guest)):
                if not alive(lane.HOST if name == 'host' else lane.GUEST):
                    new.append(f'{name}: the console stopped answering (crash?)')
                    continue
                total, recs = faults(con)
                if total != seen[name]:
                    new.append(f'{name}: {total - seen[name]} new fault(s): ' + ' | '.join(recs[:6]))
                    seen[name] = total
            mode = guest.call('game.mode')['out'].strip() if alive(lane.GUEST) else '?'
            ck(not new, f'round {rnd} (rejoin again after {time.time() - t:.1f} s, guest in {mode})'
               + ('' if not new else '; ' + '; '.join(new)))
            if new and first_fault is None:
                first_fault = rnd
            if any('stopped answering' in x for x in new) or (a.stop and new):
                break
            continue
        if a.reshare:
            # the load of this round has begun (the game is in the stage mode at once, its loading
            # screen and setup take seconds more): send the world again a little later
            while time.time() < deadline and 'loading' not in log_times(lane.GUEST, t, {'loading': r'world: loading'}):
                time.sleep(0.2)
            t1 = time.time()
            time.sleep(rng.uniform(*map(float, a.reshare.split(':'))))
            playing = guest.call('coop.avatar')['ok']
            print(f'    sent again {time.time() - t1:.1f} s after the load began '
                  f'({"playing" if playing else "still loading"})', flush=True)
            host.call('coop.share')
            t = time.time()
            deadline = t + a.limit
        ok = wait_guest_in(host, guest, timeout=a.limit)
        reloaded = None
        # the guest is "together" in its old copy at once; the host sends its world again, the guest
        # loads it through the galaxy menu and comes back: wait for that
        while ok and time.time() < deadline:
            if not (alive(lane.GUEST) and alive(lane.HOST)):
                break
            p = log_times(lane.GUEST, t, {'loading': r'world: loading'})
            if 'loading' in p and wait_guest_in(host, guest, timeout=2):
                reloaded = time.time() - t
                break
            time.sleep(1)
        alive_h, alive_g = alive(lane.HOST), alive(lane.GUEST)
        new = []
        for name, con, up in (('host', host, alive_h), ('guest', guest, alive_g)):
            if not up:
                new.append(f'{name}: the console stopped answering (crash?)')
                continue
            total, recs = faults(con)
            if total != seen[name]:
                new.append(f'{name}: {total - seen[name]} new fault(s): ' + ' | '.join(recs[:6]))
                seen[name] = total
        good = reloaded is not None and not new
        ck(good, f'round {rnd}: ' + (f'world reloaded, together again in {reloaded:.1f} s' if reloaded
                                     else 'the guest did not come back with the reloaded world')
           + ('' if not new else '; ' + '; '.join(new)))
        if reloaded:
            times.append(reloaded)
        if new and first_fault is None:
            first_fault = rnd
        if not (alive_h and alive_g) or (a.stop and new):
            break
    if times:
        print(f'  rejoin times: min {min(times):.1f} max {max(times):.1f} s over {len(times)} rounds', flush=True)
    print(f'  first fault in round {first_fault}' if first_fault else '  no fault in any round', flush=True)
    if a.cell_leave and alive(lane.GUEST):
        out = guest.call('coop.pool')['out']
        w = re.search(r'leave_writes=(\d+) leave_dropped=(\d+)', out)
        lw, ld = (int(w.group(1)), int(w.group(2))) if w else (-1, -1)
        print(f'  guest: pool work while leaving the stage: {lw} applied, {ld} skipped', flush=True)
        ck(lw == 0, f'BUG-0308: no cell state or tick of ours ran on the guest\'s pool while it left the stage '
           f'for the reloads ({lw} ran, {ld} skipped)')
    if alive(lane.HOST) and alive(lane.GUEST):
        ck.faults(host=host, guest=guest)
    else:
        ck.ok = False
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
