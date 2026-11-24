#!/usr/bin/env python3
"""BUG-0231 (GitHub #165), the Civ part: a guest that plays another world of the host's Civ stage
applies nothing of the host's vehicle and city state to its own planet, and its own vehicles keep
their own AI.

0.7.3.0 dropped the host's Creature, Cell and Tribe state for such a guest (BUG-0130), but not
MSG_CIV_STATE / MSG_CIV_BUILD / MSG_CIV_ALARM: the guest's Civ module bound the guest's own
vehicles as mirrors of the host's and drove them, killed the ones it could not bind after a full
cycle, and founded cities up to the host's count.

  1. the usual Civ session, then the guest leaves and plays a world of its own: a copy of the
     fixture planet under another name (Otherworld), coop.worldfix autoload 0 (auto_load_world=0)
  2. the guest joins the host and stays in Otherworld for 40 s
  3. on the guest's game (coop.census): none of its vehicles becomes a mirror of a host vehicle,
     none of the vehicles alive before the join dies, and it is still in Otherworld

  4. BUG-0231 (res.elsewhere): before the join the guest's own world gets 777 more money than the
     host's (coop.res add money 777, offline): while it plays that world in the session, its game's
     money never becomes the host's total (0.7.3.0 overwrote it), and the host's does not move by it

  5. --slow-join (BUG-1371): the guest runs at 2 fps (frame.limit 500) from just before the join until 6 s after it,
     so WELCOME and the host's first money total (sent in the host's tick after the join) arrive in one frame of the
     guest's, before any tick of its knows it plays another world (lab 20261120-133703: a loaded lab did this by itself,
     the guest's money became the host's 3000 one millisecond after the join). The money check of 4. decides.

  6. --income (BUG-1478, CIV-5): the guest's own world keeps its own cities' income while it is in the session: its
     game's money (+0x98) grows by the 6 s city tick in the 40 s (the guest hooks gated on the Civ mode only dropped
     every income of the player civilization, its own world's too).

  coop_civ_other_world.py [--nofix]   (coop.fix core.world_gate off: must FAIL)
  SPORE_FIX_OFF=civ.local_play_gate coop_civ_other_world.py --income   (its own income dropped: must FAIL)
  SPORE_FIX_OFF=res.elsewhere coop_civ_other_world.py   (the host's money total applied: must FAIL)
  SPORE_FIX_OFF=res.elsewhere_join coop_civ_other_world.py --slow-join   (the total of the join's frame taken: must FAIL)
"""
import glob
import os
import shutil
import sys
import time

from harness import shared_money as money, world_of
import lane
from census import dump
from twoinst import Checks, start


def main():
    nofix = '--nofix' in sys.argv
    slow_join = '--slow-join' in sys.argv
    income = '--income' in sys.argv
    host, guest = start('civ')
    ck = Checks()
    for con in (host, guest):
        print(' ', con.call(f'coop.fix core.world_gate {"off" if nofix else "on"}')['out'].strip())
    print(' ', guest.call('coop.leave')['out'].strip())
    guest.call('coop.worldfix autoload 0')
    games = os.path.join(lane.GUEST_DIR, 'Games')
    src = next(os.path.dirname(p) for p in glob.glob(os.path.join(games, '*', 'Arigine.spo')))
    dst = os.path.join(games, 'Game9')
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    if os.path.exists(os.path.join(dst, 'sporecoop.txt')):
        os.remove(os.path.join(dst, 'sporecoop.txt'))
    os.rename(os.path.join(dst, 'Arigine.spo'), os.path.join(dst, 'Otherworld.spo'))
    print(' ', guest.call('coop.load Game9 Otherworld')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 150:
        if world_of(guest) == 'Otherworld.spo' and 'Civ' in guest.call('game.mode')['out']:
            break
        time.sleep(2)
    ck(world_of(guest) == 'Otherworld.spo', f'the guest plays a world of its own ({world_of(guest)})')
    guest.call('wait.frames 300 60000')
    time.sleep(8)
    c0 = dump(guest)
    alive0 = {r['id'] if r['id'] >= 0 else i: r for i, r in enumerate(c0.rows) if r['kind'] == 'v' and r['alive']}
    n0 = len(alive0)
    print(f'  guest alone: {n0} vehicles alive, census complete={c0.complete}')
    ck(n0 > 0, 'the guest\'s own planet has vehicles')

    print(' ', guest.call('coop.res add money 777')['out'].strip())
    if income:
        # Ensure the guest's own city has connected factories and entertainment so its 6 s passive city income ticks.
        for kind in ('house', 'fun', 'factory', 'house', 'fun', 'factory'):
            guest.call(f'coop.civ build 0 {kind} nopay')
    time.sleep(1)
    gm0, hm0 = money(guest), money(host)
    print(f'  money: guest own world {gm0}, host {hm0}')

    if slow_join:  # BUG-1371: WELCOME and the host's first total in one frame of the guest's
        print('  guest: frame.limit 500 ->', guest.call('frame.limit 500')['out'].strip(), flush=True)
    print(' ', guest.call(lane.JOIN)['out'].strip())
    mirrors_max, alive_min = 0, n0
    pairs = []  # (guest money, host money) per second
    t0 = time.time()
    slow = slow_join
    while time.time() - t0 < 40:
        if slow and time.time() - t0 > 6:
            slow = False
            print('  guest: frame.limit auto ->', guest.call('frame.limit auto')['out'].strip(), flush=True)
        c = dump(guest)
        vs = [r for r in c.rows if r['kind'] == 'v']
        mirrors_max = max(mirrors_max, len([r for r in vs if r['own'] == 'mirror']))
        alive_min = min(alive_min, len([r for r in vs if r['alive']]))
        pairs.append((money(guest), money(host)))
        time.sleep(1)
    st = guest.call('coop.status')['out']
    print(f'  during 40 s in the session: guest vehicles bound as mirrors (max) {mirrors_max}, alive (min) {alive_min} of {n0}')
    print('  guest:', ' | '.join(l for l in st.splitlines() if 'elsewhere' in l)[:200])
    ck('elsewhere=1' in st, 'the guest knows the host plays another world')
    ck(mirrors_max == 0, f'none of the guest\'s own vehicles is driven as a host vehicle ({mirrors_max})')
    ck(alive_min >= n0, f'none of the guest\'s own vehicles died ({alive_min} of {n0} alive)')
    ck(world_of(guest) == 'Otherworld.spo', f'the guest is still in its own world ({world_of(guest)})')
    pairs = [(g, h) for g, h in pairs if g is not None and h is not None]
    gap = min((abs(g - h) for g, h in pairs), default=None)
    drift = max((abs(h - hm0) for g, h in pairs), default=None) if hm0 is not None else None
    print(f'  money during the 40 s: {len(pairs)} samples, closest guest/host {gap}, host drift {drift}; last {pairs[-1:]}')
    # (join_held: host totals the BUG-1371 check held on arrival because no tick had held yet: the case of --slow-join)
    print('  guest:', ' '.join(w for w in guest.call('coop.res')['out'].split() if w.startswith(('held=', 'join_held='))),
          flush=True)
    # (a Civ planet's money moves with its own income: the 777 apart is what must stay)
    ck(gap is not None and gap > 500, f"the guest's own world keeps its own money, never the host's total (closest {gap})")
    ck(drift is not None and drift < 500, f"the host's money is not changed by the guest's world (drift {drift})")
    if income:  # BUG-1478: the guest's own world's income in the session (its game's money, 5 s after the join on)
        own = [g for g, h in pairs[5:]]
        rise = max(own) - own[0] if own else None
        print(f'  the guest\'s own world in the session: money {own[:1]} -> max {max(own) if own else None} (+{rise})', flush=True)
        print('  guest:', next((ln for ln in guest.call('coop.civ')['out'].splitlines() if ln.startswith('money:')), ''), flush=True)
        ck(rise is not None and rise >= 30, f"the guest's own world earns its own cities' income in the session (+{rise} in 35 s)")

    for con in (host, guest):
        con.call('coop.fix core.world_gate on')
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
