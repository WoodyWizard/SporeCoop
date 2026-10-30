#!/usr/bin/env python3
"""Civilization: a second superweapon request during a running vote (BUG-0117), checked in the game.

The weapons are made usable the game's way (no free cheat): `unlocked[type]` of the player civ
(+0x484, bool[12], GOG 2024 CanUse 0xBF8B40) is written for the two types of a case, and the shared
money is raised to 20000. Types 3 and 9 cost 2000, type 1 costs 16000 (GOG 2024 0xBF6DF0), so
the money the game takes in FireSuperweapon (mWealth +0x98 -= cost, 0xBF8BB0) tells which weapon the
game fired. Each case asks for its own cheap type (guest 3, host 9; a fired type is on cooldown), then type 1.

Case guest (the BUG-0117 path, host_superweapon_request):
1. The guest asks for type 3 (`coop.civ super 3`: the game's FireSuperweapon, hooked on the guest:
   the request goes to the host, the host opens a vote).
2. While that vote runs, the guest asks for type 1.
3. The host passes the vote (`coop.vote go`).
4. The game's money on the host (+0x98) went down by about 2000 (type 3), not 16000 (type 1), and
   the guest's own game shows the same money (the shared total); the guest was told its second
   request was not taken.
Case host (the host's own request, h_FireSW): the host asks for type 9, then for type 1 during the
vote, the host passes the vote: again 2000, not 16000.

Before the fix the second request overwrote the statics the vote read: type 1 fired under the
first vote (16000 taken). Spice income during the few seconds is small (tolerance 600).

--nofix: `coop.civ fix sw_vote 0` (the old statics): must FAIL (as SPORE_FIX_OFF=civ.sw_vote).

  SPORE_LANE=7 python3 tools/tests/coop_civ_sw_vote.py [--nofix] [--case guest|host|both]
"""
import argparse
import re
import struct
import time

from twoinst import Checks, start

# (cheap, dear) per case: each case its own cheap type (a fired weapon's cooldown blocks it for minutes).
# The dear one fires only without the fix (then the later case finds it on cooldown: spent 0, also a
# FAIL). Not type 4: fired at the zero vector it crashed the game in cPlanetModel::GetNearestCity+0x1B7
# (0xB897A7, lab 20261030-001517; the game's own UI always passes a place)
PAIR = {'guest': (3, 1), 'host': (9, 1)}
COST = {3: 2000.0, 9: 2000.0, 1: 16000.0}
TOL = 600.0


def sw(con):
    out = con.call('coop.civ sw')['out']
    d = {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)\b', out)}
    m = re.search(r'civ=(?:0x)?([0-9A-Fa-f]+)', out)
    d['civ'] = int(m.group(1), 16) if m else 0
    return d, out.strip()


def read_f32(con, addr):
    out = con.call(f'mem.read 0x{addr:08X} 4')['out']
    b = bytes(int(x, 16) for x in out.split()[1:5])
    return struct.unpack('<f', b)[0] if len(b) == 4 else None


def money(con):
    """the game's own money of this machine's player civ (+0x98)"""
    civ = sw(con)[0]['civ']
    return read_f32(con, civ + 0x98) if civ else None


def unlock(con, types):
    civ = sw(con)[0]['civ']
    for t in types:
        con.call(f'mem.write 0x{civ + 0x484 + t:08X} 01')
    out = con.call(f'mem.read 0x{civ + 0x484:08X} 12')['out']
    return [int(x, 16) for x in out.split()[1:13]]


def set_money(host, guest, want):
    h = money(host)
    host.call(f'coop.res add money {want - h:.1f}')
    for _ in range(20):
        time.sleep(0.5)
        h, g = money(host), money(guest)
        if h is not None and g is not None and abs(h - want) < 50 and abs(g - want) < 50:
            break
    return money(host), money(guest)


def settle(host, guest, before, secs=8):
    """the host's and guest's money once the vote fired (the guest gets the shared total later)"""
    t0 = time.time()
    h = g = None
    while time.time() - t0 < secs:
        time.sleep(1)
        h, g = money(host), money(guest)
        if h is not None and g is not None and before - h > 1000 and abs(h - g) < 50:
            break
    return h, g


def run_case(ck, host, guest, case):
    asker = guest if case == 'guest' else host
    cheap, dear = PAIR[case]
    tag = f'[{case}]'
    print(f'  {tag} unlocked on the host:', unlock(host, (cheap, dear)))
    h0, g0 = set_money(host, guest, 20000.0)
    print(f'  {tag} money host {h0} guest {g0}')
    ck(h0 is not None and h0 >= 19000 and g0 is not None and abs(g0 - h0) < 50,
       f'{tag} both games show 20000 before the request (host {h0}, guest {g0})')
    print(f'  {tag} first request:', asker.call(f'coop.civ super {cheap}')['out'].strip())
    time.sleep(2)
    v = host.call('coop.vote')['out']
    print(f'  {tag} host vote:', v.splitlines()[0] if v else v)
    ck('wants to fire a superweapon' in v and 'active' in v, f'{tag} the request opened a vote')
    print(f'  {tag} second request:', asker.call(f'coop.civ super {dear}')['out'].strip())
    time.sleep(2)
    s, out = sw(host)
    print(f'  {tag} host:', out)
    if case == 'guest':
        ck(s.get('last_request') == dear, f'{tag} the host got the second request')
    ck(abs(money(host) - h0) < TOL, f'{tag} nothing fired before the vote passed')
    host.call('coop.vote go')
    h1, g1 = settle(host, guest, h0)
    print(f'  {tag} after the vote: host {h0} -> {h1}, guest {g0} -> {g1}')
    spent = h0 - (h1 or h0)
    ck(COST[cheap] - TOL <= spent < COST[dear] - TOL,
       f'{tag} the game fired the vote\'s weapon: type {cheap} (2000), not the later request\'s type {dear} (16000): '
       f'spent {spent:.0f}')
    ck(g1 is not None and h1 is not None and abs(g1 - h1) < 50,
       f'{tag} the guest\'s game shows the same money ({g1} vs {h1})')
    if case == 'guest':
        gst = guest.call('coop.status')['out']
        ck('another vote is running' in gst, f'{tag} the guest was told its second request was not taken')
    time.sleep(2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--case', choices=('guest', 'host', 'both'), default='both')
    a = ap.parse_args()
    host, guest = start('civ')
    ck = Checks()
    if a.nofix:
        print(' ', host.call('coop.civ fix sw_vote 0')['out'].strip())
    time.sleep(3)
    for case in (('guest', 'host') if a.case == 'both' else (a.case,)):
        run_case(ck, host, guest, case)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
