#!/usr/bin/env python3
"""Civilization: the host's tables of game pointers across a load and a leave (BUG-0040, BUG-0059),
checked in the game (BUG-0222).

1. Two games play the civ_npc fixture; the host raises the alarm for its capital (`coop.civ
   alarm`): the vehicles of the other player near it are boosted. Precondition (fails the test): at
   least one vehicle is boosted, and the game's own speed of each (+0x218, read with mem.read) is
   1.5x its speed before.
2. The host loads the same planet again (a fresh slot) while connected: every old vehicle and city
   is freed. No boost or alarm of the old world is left (table check, BUG-0059's on_world_change).
3. BUG-0059's pol compare, forced (the address reuse it guards against is rare): `coop.civ stale_hv
   <id> 1` rewrites an NPC nation's vehicle entry as if the address had held the guest's vehicle of
   the old world (owner = the guest, another pol). The guest selects that vehicle and orders it to
   drive somewhere: in the host's game the NPC vehicle must not take the guest's order (its order
   list, +0xB68, has no order at the guest's target). Before the fix the stale owner stayed and the
   guest drove another nation's vehicle.
4. BUG-0040: the host raises the alarm again (precondition: boosted) and leaves the session: the
   game's speed of every boosted vehicle is back to its own speed 2 s later (before the fix the boost
   map outlived the session; nothing restored the speed and later ticks wrote into freed vehicles).
   The speed is read again after 10 s and 30 s to show whether the game's RecalcStats brings it
   back by itself (printed: the BUG-0222 step 0 measurement).

--nofix: both fixes off (`coop.civ fix boost 0`, `coop.civ fix world 0`): must FAIL. One at a time:
SPORE_FIX_OFF=civ.boost (step 4 fails) or SPORE_FIX_OFF=civ.world (step 3 fails).

  SPORE_LANE=5 python3 tools/tests/coop_civ_reload.py [--nofix]
"""
import argparse
import math
import re
import struct
import time

import fixtures
import lane
from lane import sh
from twoinst import Checks, Monitor, build_id, lane_state, start, wait_guest_in

CIV = 0x1654C04
VEH = re.compile(r'\s+veh #(\d+) (\S+) pol=(\w+) owner=(-?\d+) .*?sel=(\d+) .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)'
                 r' order=(-?\d+)(?: opos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\))?')


def mode(con):
    try:
        return int(con.call('game.mode')['out'].split()[0], 16)
    except (OSError, ValueError, IndexError):
        return -1


def tables(con):
    out = con.call('coop.civ tables')['out']
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', out)}, out.strip()


def vehicles(con):
    """player pol, {id: dict(addr, pol, owner, sel, pos, order, opos)} as the game has them"""
    ppol, veh = None, {}
    for line in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'city -?\d+ \S+ pol=(\w+) \(player\)', line)
        if m:
            ppol = m.group(1)
        m = VEH.match(line)
        if m:
            g = m.groups()
            veh[int(g[0])] = dict(addr=g[1], pol=g[2], owner=int(g[3]), sel=int(g[4]),
                                  pos=tuple(map(float, g[5:8])), order=int(g[8]),
                                  opos=tuple(map(float, g[9:12])) if g[9] else None)
    return ppol, veh


def player_cities(con):
    return [int(m.group(1)) for m in re.finditer(r'^city (\d+) \S+ pol=\w+ \(player\)', con.call('coop.civ')['out'], re.M)]


def speed(con, addr):
    """the game's speed field of a vehicle (+0x218, the one the boost writes)"""
    out = con.call(f'mem.read 0x{int(addr, 16) + 0x218:08X} 4')['out']
    try:
        return struct.unpack('<f', bytes(int(x, 16) for x in out.split()[1:5]))[0]
    except (ValueError, struct.error):
        return None


def boosts(con):
    """{address: orig speed} of the vehicles the alarm boosted"""
    return {m.group(1): float(m.group(2)) for m in
            re.finditer(r'^boost (?:0x)?(\S+) orig=([-\d.]+)', con.call('coop.civ boosts')['out'], re.M)}


def alarm(ck, host, label):
    """raise the alarm for the host's capital; {addr: orig} of the boosted vehicles"""
    mine = player_cities(host)
    ck(mine, f'[{label}] the host has player cities: {mine}')
    if not mine:
        return {}
    print(' ', host.call(f'coop.civ alarm {mine[0]}')['out'].strip())
    time.sleep(1)
    b = boosts(host)
    sp = {a: speed(host, a) for a in b}
    print(f'  [{label}] boosted {len(b)}: ' + ', '.join(f'{a} {o:.2f} -> {sp[a]}' for a, o in b.items()))
    ck(b, f'[{label}] the alarm boosted a vehicle of the other player (precondition)')
    ck(all(sp[a] is not None and abs(sp[a] - 1.5 * o) < 0.05 * max(o, 1) for a, o in b.items()),
       f'[{label}] the game\'s speed (+0x218) of each boosted vehicle is 1.5x its own')
    return b


def reload(host, guest, slot):
    """the host loads civ_npc again from a fresh slot while connected; True when back in Civ"""
    planet = fixtures.planet_of('civ_npc').split('/')[1]
    sh('fixture', 'slot', 'civ_npc', lane.HOST_PROFILE, str(slot))  # never the loaded save's files
    print('  ', host.call(f'coop.load Game{slot} {planet}')['out'].strip()[:60], flush=True)
    t0, left = time.time(), False
    while time.time() - t0 < 150 and not Monitor.events:
        m = mode(host)
        left |= m != CIV or not host.call('coop.avatar')['ok']
        if left and m == CIV and host.call('coop.avatar')['ok']:
            break
        f = host.call('frame')['out']
        time.sleep(0.7)
        if host.call('frame')['out'] == f:
            host.call('key enter')  # a box that stops the frames ("game saved")
    else:
        return False
    print(f'   host back in Civ after {time.time() - t0:.0f} s', flush=True)
    return wait_guest_in(host, guest, 120)


def stale_owner(ck, host, guest):
    """step 3: an NPC vehicle's entry made stale with the guest as owner; the guest's order must not
    reach the host's game"""
    ppol, hv = vehicles(host)
    npc = [(vid, v) for vid, v in hv.items() if ppol and v['pol'] != ppol and v['owner'] in (254, -1)]
    ck(npc, f'[stale] the host has an NPC nation\'s vehicle ({len(npc)})')
    if not npc:
        return
    _, gv = vehicles(guest)
    seen = [x for x in npc if x[0] in gv]
    ck(seen, f'[stale] the guest mirrors an NPC nation\'s vehicle of the host ({[x[0] for x in npc]} / {sorted(gv)})')
    vid, v = (seen or npc)[0]
    print('  [stale] host:', host.call(f'coop.civ stale_hv {vid} 1')['out'].strip(), f'(veh #{vid} {v["addr"]})')
    time.sleep(1.5)
    _, gv = vehicles(guest)
    print(f'  [stale] the guest sees #{vid}:', gv.get(vid))
    tgt = (v['pos'][0] + 60.0, v['pos'][1], v['pos'][2] + 60.0)
    print('  [stale] guest:', guest.call(f'coop.civ select {vid} 1')['out'].strip(), '|',
          guest.call('coop.civ move %d %.1f %.1f %.1f' % ((vid,) + tgt))['out'].strip())
    time.sleep(3)
    _, hv = vehicles(host)
    now = next((x for x in hv.values() if x['addr'] == v['addr']), None)
    print(f'  [stale] host\'s game, the NPC vehicle: {now}')
    took = now is not None and now['opos'] is not None and now['order'] >= 0 and \
        math.dist(now['opos'], tgt) < 3.0
    ck(not took, 'the guest\'s order did not reach the NPC nation\'s vehicle in the host\'s game')
    _, gv = vehicles(guest)
    g = gv.get(vid)
    ck(not g or g['owner'] != 1 or g['sel'] == 0, f'the guest does not hold that vehicle as its own ({g})')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.civ fix boost 0')['out'].strip(), '|', c.call('coop.civ fix world 0')['out'].strip())
    time.sleep(4)
    if not alarm(ck, host, 'before the load'):
        return ck.result()

    st = lane_state()
    slot = 10 + (st.get('slot', 9) - 9) % 10
    ok = reload(host, guest, slot)
    lane_state(**{**st, 'build': build_id(), 'fixture': 'civ_npc', 'planet': fixtures.planet_of('civ_npc'),
                  'slot': slot})
    ck(ok, 'the host loaded the planet again and the guest is with it')
    time.sleep(3)
    t1, txt = tables(host)
    print('  after the load:', txt)
    ck(t1.get('alarm_at', 1) == 0 and t1.get('boost', 1) == 0,
       'no boost or alarm of the old world is left after the load')

    stale_owner(ck, host, guest)

    # an alarm, then the host leaves: every boosted vehicle has its own speed again (in the game)
    b = alarm(ck, host, 'before the leave')
    host.call('coop.leave')
    for wait, label in ((2, '2 s'), (8, '10 s'), (20, '30 s')):
        time.sleep(wait)
        sp = {a: speed(host, a) for a in b}
        print(f'  speed {label} after the leave: ' + ', '.join(f'{a} {o:.2f} -> {sp[a]}' for a, o in b.items()),
              flush=True)
        if label == '2 s':
            ck(b and all(sp[a] is not None and abs(sp[a] - o) < 0.01 * max(o, 1) for a, o in b.items()),
               'the game\'s speed of every boosted vehicle is back to its own 2 s after the leave')
    t3, txt = tables(host)
    print('  after the leave:', txt)
    ck(t3.get('alarm_at', 1) == 0 and t3.get('hv', 1) == 0, 'the alarm and vehicle tables end with the session')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
