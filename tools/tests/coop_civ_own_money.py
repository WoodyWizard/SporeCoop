#!/usr/bin/env python3
"""BUG-1468: Civilization, every player's own money (the session setting civ.own_money; idea and rules of PR #205 by
IltonBJSilva). Checks the money each game holds (the player civilization's mWealth +0x98, read with mem.read), not
the mod's bookkeeping.

--case main (the default):
  1. the join split: the guest leaves, the host switches the setting on (coop.settings force: a session setting) and the
     guest joins again: the guest's game shows an equal part of the host's money (host 2000 -> 1000 / 1000 within the
     income of the seconds between), the sum stays (money moves, none is made); key frame.
  2. the cities' income: 20 s of the game's own income (the 6 s city tick, no command) reach the guest's game as its
     own cities' part (its money grows by itself, without a purchase), and `coop.wallet income 400` (the same split as
     the game's tick) gives the guest its part.
  3. a land vehicle the guest buys (`coop.civ buy`, made by the host's game) is paid from the guest's money: the
     guest's game shows less, the host's money is unchanged (within the income); the vehicle is there.
  4. a refused purchase: the guest's money is set below a vehicle's price on the host's wallet copy (the guest's game
     spends it: `mem.write` of its own +0x98, as a planner purchase would); its vehicle is refused with a notice on the
     guest ("not enough money of your own"), no vehicle comes, the host's money is unchanged.
  5. a superweapon of the guest (unlocked type 3, 2000, the vote passed by the host) is paid from the guest's money.
  6. a rejoin: the guest leaves and joins again: its money is its own again (kept by its store key, not a new part).
--case off: after 1., the guest leaves, the host switches the setting off (the next session): the kept wallet goes back
  into the host's money once (host before + guest's wallet = host after, within the income), and the rejoining guest
  plays the one shared money again (its game shows the host's total).
--case old: the guest plays as a 0.7.16.1 peer (`coop.manifest plant civ 1 1` before its join: civ 1.1): with the
  setting on it gets no wallet (no MSG_CIV_WALLET traffic), its game shows the host's shared money, its vehicle is paid
  from the shared money.
Civ -> Space with own money (the RE: the Civ money is not carried, every new empire gets SetMoney 100000,
  docs/re/civ.md#civ-own-money) and BUG-1531 (the guest's own spaceship editor): the campaign segment civ -> space
  (suite civ_own_money_space: CAMPAIGN_HOST_CMDS forces the setting, CAMPAIGN_GUEST_SHIP_EDITOR=1).

Twins: SPORE_FIX_OFF=civ.wallet_row --case main (the guest's money jumps back to the host's shared total: 1. or 2.
fails); with the setting off (`--setting off`) 1. fails (one money).

  SPORE_LANE=2 python3 tools/tests/coop_civ_own_money.py [--case main|off|old] [--setting on|off]
"""
import argparse
import re
import struct
import time

import lane
from coop_civ import civ
from twoinst import Checks, keyframe, start, wait_guest_in


def wait(f, secs, step=1.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except (OSError, ValueError, TypeError):
            pass
        time.sleep(step)
    return False


def civ_ptr(con):
    out = con.call('coop.civ sw')['out']
    m = re.search(r'civ=(?:0x)?([0-9A-Fa-f]+)', out)
    return int(m.group(1), 16) if m and int(m.group(1), 16) else 0


def read_f32(con, addr):
    out = con.call(f'mem.read 0x{addr:08X} 4')['out']
    b = bytes(int(x, 16) for x in out.split()[1:5])
    return struct.unpack('<f', b)[0] if len(b) == 4 else None


def money(con):
    """the game's own money of this machine's player civilization (+0x98)"""
    c = civ_ptr(con)
    return read_f32(con, c + 0x98) if c else None


def write_money(con, v):
    c = civ_ptr(con)
    if c:
        con.call(f'mem.write 0x{c + 0x98:08X} {struct.pack("<f", float(v)).hex()}')


def wallet(con):
    return con.call('coop.wallet')['out']


def counter(con, name):
    m = re.search(rf'\b{name}=(-?[\d.]+)', wallet(con))
    return float(m.group(1)) if m else -1


def has_wallet(guest):
    return ' have=1 ' in wallet(guest).split('\n', 1)[0]


def toasts(con):
    return [ln for ln in con.call('coop.status')['out'].splitlines() if ln.startswith('toast ')]


def rejoin(ck, host, guest, what):
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN)
    return ck(wait_guest_in(host, guest), f'the guest joined {what}')


def setting(host, v):
    r = host.call(f'coop.settings force civ.own_money {v}')['out'].strip()
    print('  host:', r, flush=True)
    return r


def join_split(ck, host, guest, on):
    """1. the guest leaves, the setting is switched, the guest joins: its part of the host's money"""
    guest.call('coop.leave')
    time.sleep(2)
    setting(host, 'on' if on else 'off')
    write_money(host, 2000)  # the plan's numbers: 2000 -> 1000 / 1000
    time.sleep(1)
    total = money(host)
    guest.call(lane.JOIN)
    if not ck(wait_guest_in(host, guest), 'the guest joined with the setting ' + ('on' if on else 'off')):
        return None
    ck(wait(lambda: has_wallet(guest), 60, 2), "the guest's game has its own money (its wallet came)")
    wait(lambda: money(guest) is not None, 20, 1)
    time.sleep(2)
    h, g = money(host), money(guest)
    print(f'  the host had {total}; now host {h}, guest {g}', flush=True)
    # an equal part; the cities' income of the seconds between may add a little (never more than the whole)
    ck(g is not None and total // 2 - 1 <= g < total // 2 + 400, f"the guest's game shows its own part ({g} of {total})")
    ck(h is not None and total // 2 - 1 <= h < total // 2 + 400, f"the host's game shows its own part ({h} of {total})")
    ck(h is not None and g is not None and abs((h + g) - total) < 800, f'money moved, none made ({h} + {g} vs {total})')
    keyframe('split', "each game's money counter at the top shows about 1000 (half of the host's 2000)")
    return total


def case_main(ck, host, guest, setting_on):
    total = join_split(ck, host, guest, setting_on)
    if total is None:
        return
    # 2. the cities' own income, 20 s without a command; then an income of the test command
    hc0, _ = civ(host)
    for k, v in hc0.items():
        if v['player']:
            for kind in ('house', 'factory', 'factory'):
                host.call(f'coop.civ build {k} {kind} nopay')
    time.sleep(2)
    g0, h0 = money(guest), money(host)
    samples = []
    for _ in range(10):
        time.sleep(2)
        samples.append((money(host), money(guest)))
    g1, h1 = money(guest), money(host)
    print(f'  20 s of income: host {h0} -> {h1}, guest {g0} -> {g1}; samples {samples}', flush=True)
    ck(g1 is not None and g0 is not None and g1 > g0, f"the guest's game earned its cities' part by itself ({g0} -> {g1})")
    ck(all(h is not None and g is not None and abs(h - g) > 1 for h, g in samples[-3:]) or abs(h1 - g1) > 1,
       'the two games show two moneys, not one shared total')
    g0, out0 = money(guest), counter(host, 'shared_out')
    print('  host:', host.call('coop.wallet income 400')['out'].strip(), flush=True)
    ck(wait(lambda: money(guest) > g0 + 20, 15), f"the guest's game got its part of an income ({g0} -> {money(guest)})")
    ck(counter(host, 'shared_out') > out0, "the host's game gave a part of the income away")
    # 3. a land vehicle the guest buys in its city (the host's game makes it)
    write_money(guest, 3000)  # its own money (as a sale in its own game would): a vehicle fits
    time.sleep(3)  # its report reaches the host (every 2 s)
    hc, hv = civ(host)
    mine = [k for k, v in hc.items() if v['player'] and v['owner'] in (1, 253)]
    ck(mine, f"a city the guest may buy in ({ {k: v['owner'] for k, v in hc.items() if v['player']} })")
    if mine:
        g0, h0, ch0, nveh0 = money(guest), money(host), counter(host, 'charges'), len(hv)
        print('  guest buys:', guest.call(f'coop.civ buy {mine[0]} 0')['out'].strip(), flush=True)
        bought = wait(lambda: counter(host, 'charges') > ch0, 20)
        time.sleep(4)
        g1, h1 = money(guest), money(host)
        print(f'  guest {g0} -> {g1}, host {h0} -> {h1}', flush=True)
        ck(bought, 'the purchase was charged to the guest (the host made the vehicle)')
        ck(g1 is not None and g1 < g0 - 50, f"the guest's game shows the price paid ({g0} -> {g1})")
        ck(h1 is not None and h1 >= h0 - 50, f"the host's money did not pay it ({h0} -> {h1})")
        ck(wait(lambda: len(civ(host)[1]) > nveh0, 10), 'the vehicle is in the host\'s game')
        keyframe('bought', "the guest's new land vehicle at its city; the guest's money counter lower than the host's")
        # 4. a refused purchase: the guest's own money below a vehicle's price
        write_money(guest, 50)
        time.sleep(3)
        h0, ch0, nveh0 = money(host), counter(host, 'charges'), len(civ(host)[1])
        print('  guest buys (short):', guest.call(f'coop.civ buy {mine[0]} 0')['out'].strip(), flush=True)
        t = None
        t0 = time.time()
        while time.time() - t0 < 8 and not t:
            t = next((x for x in toasts(guest) if 'not enough money of your own' in x), None)
            time.sleep(0.3)
        print('  guest toast:', t, flush=True)
        ck(t is not None, "the guest's game says its own money is short")
        ck(counter(host, 'charges') == ch0 and len(civ(host)[1]) <= nveh0, 'no vehicle came, nothing was charged')
        ck(abs(money(host) - h0) < 400, f"the host's money did not pay it ({h0} -> {money(host)})")
        keyframe('refused', "the guest's screen shows the notice 'no vehicle: not enough money of your own'")
    # 5. a superweapon of the guest, paid from its money (type 3 costs 2000, docs/re/civ.md)
    write_money(guest, 5000)
    c = civ_ptr(host)
    if c:
        host.call(f'mem.write 0x{c + 0x484 + 3:08X} 01')
    time.sleep(3)
    g0, h0 = money(guest), money(host)
    print('  guest superweapon:', guest.call('coop.civ super 3')['out'].strip(), flush=True)
    time.sleep(2)
    host.call('coop.vote go')
    paid = wait(lambda: money(guest) < g0 - 1000, 15)
    g1, h1 = money(guest), money(host)
    mline = next((ln for ln in host.call('coop.civ')['out'].splitlines() if ln.startswith('money:')), '')
    print(f'  superweapon: guest {g0} -> {g1}, host {h0} -> {h1}; host {mline}', flush=True)
    ck(paid, f"the guest's superweapon is paid from its own money ({g0} -> {g1})")
    ck(h1 is not None and h1 > h0 - 1000, f"the host's money did not pay it ({h0} -> {h1})")
    # 6. a rejoin keeps the wallet
    time.sleep(3)
    g0, kb0, nc0 = money(guest), counter(host, 'kept_back'), counter(host, 'newcomers')
    if rejoin(ck, host, guest, 'again'):
        ck(wait(lambda: has_wallet(guest) and abs(money(guest) - g0) < 600, 60, 2),
           f"after the rejoin the guest's money is its own again ({g0} -> {money(guest)})")
        ck(counter(host, 'kept_back') > kb0 and counter(host, 'newcomers') == nc0,
           'the host gave the guest its kept wallet back, not a new part')


def case_off(ck, host, guest):
    if join_split(ck, host, guest, True) is None:
        return
    time.sleep(3)
    guest.call('coop.leave')
    time.sleep(3)
    h0 = money(host)
    kept = counter(host, 'kept_back')  # (the line shows the kept wallet below)
    m = re.search(r'^kept \S+ \(.*\): (\d+)', wallet(host), re.MULTILINE)
    g_kept = float(m.group(1)) if m else None
    print(f'  the guest left: host {h0}, its kept wallet {g_kept} ({kept})', flush=True)
    ck(g_kept is not None and g_kept > 500, "the host keeps the guest's wallet")
    setting(host, 'off')  # the next session plays without it
    ck(wait(lambda: counter(host, 'merged') >= 1, 10), 'the kept wallets went back once')
    h1 = money(host)
    print(f'  host {h0} -> {h1} (+{g_kept})', flush=True)
    ck(h1 is not None and g_kept is not None and abs(h1 - (h0 + g_kept)) < 400,
       f'sum before = sum after: {h0} + {g_kept} = {h1}')
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest joined the session without own money')
    ck(wait(lambda: abs(money(guest) - money(host)) < 50, 20), f'one shared money again (host {money(host)}, guest {money(guest)})')
    ck(not has_wallet(guest), 'the guest has no wallet')
    keyframe('merged', 'both money counters show the same total (the shared money)')


def case_old(ck, host, guest):
    guest.call('coop.leave')
    time.sleep(2)
    print('  guest:', guest.call('coop.manifest plant civ 1 1')['out'].strip(), flush=True)
    setting(host, 'on')
    guest.call(lane.JOIN)
    if not ck(wait_guest_in(host, guest), 'the 0.7.16.1-like guest joined'):
        return
    time.sleep(8)
    w = wallet(host)
    print('  host:', w.splitlines()[0], flush=True)
    ck(counter(host, 'sets') == 0 and 'player 1:' not in w, 'the host gave the old guest no wallet')
    ck(counter(guest, 'asks') == 0, 'the old guest asked for none')
    ck(wait(lambda: abs(money(guest) - money(host)) < 50, 15), f'it plays the shared money (host {money(host)}, guest {money(guest)})')
    hc, hv = civ(host)
    mine = [k for k, v in hc.items() if v['player'] and v['owner'] in (1, 253)]
    if mine:
        host.call(f'coop.res add money {3000 - money(host):.0f}')
        time.sleep(3)
        h0 = money(host)
        print('  guest buys:', guest.call(f'coop.civ buy {mine[0]} 0')['out'].strip(), flush=True)
        ck(wait(lambda: money(host) < h0 - 50, 15), f'its vehicle is paid from the shared money ({h0} -> {money(host)})')
        ck(wait(lambda: abs(money(guest) - money(host)) < 50, 10), 'both games show the same shared money after it')
    guest.call('coop.manifest reset')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', default='main', choices=('main', 'off', 'old'))
    ap.add_argument('--setting', default='on', choices=('on', 'off'))
    a = ap.parse_args()
    host, guest = start('civ')
    ck = Checks()
    try:
        time.sleep(4)
        if a.case == 'main':
            case_main(ck, host, guest, a.setting == 'on')
        elif a.case == 'off':
            case_off(ck, host, guest)
        else:
            case_old(ck, host, guest)
        print('  host:', wallet(host).replace('\n', ' | ')[:600], flush=True)
        print('  guest:', wallet(guest).replace('\n', ' | ')[:300], flush=True)
    finally:
        for con in (host, guest):
            try:
                con.call('coop.settings force civ.own_money off')
                con.call('coop.manifest reset')
            except OSError:
                pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
