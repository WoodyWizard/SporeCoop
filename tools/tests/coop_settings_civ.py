#!/usr/bin/env python3
"""The Civilization session settings (BUG-0768, F3 step 3 of 0.7.12): civ.joint_siege, civ.superweapon_vote and
civ.roles, each on (the default, 0.7.11's behaviour) and off, checked in the game's state.

civ_npc fixture, two players (cold: the settings are lane knobs):
  1. joint siege: an NPC city attacked by both players (`coop.civ siege`, the game's AddCapture): on, the second
     player's attack adds x1.5 capture; the host switches civ.joint_siege off in play: both add 1.0;
  2. superweapon vote off (the host's `coop.civ super 3`, the game's FireSuperweapon): it fires at once (the game
     takes its cost of 2000 at once, the guest's game shows the same money) and the guest sees no vote; on again:
     the same request starts a vote the guest sees (`coop.vote` active);
  3. roles: two military vehicles of the guest attack an NPC city (as coop_civ_role_damage.py): no role (A), the guest
     picks military with civ.roles on (M >= 1.15 x A), the host switches civ.roles off (R <= 1.10 x A): the damage per
     hit in the host's game (the buildings' hit points).
SPORE_FIX_OFF=settings.all (every step-3 setting reads its default): the off checks must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_settings_civ.py
"""
import re
import time

import settings_lib as sl
import twoinst
from coop_civ import civ
from coop_civ_role_damage import give, owners, phase
from coop_civ_sw_vote import set_money, settle, unlock
from play_civ import civ_state, dist
from twoinst import Checks, start

KEYS = ('civ.joint_siege', 'civ.superweapon_vote', 'civ.roles')


def siege(host, c):
    r1 = host.call(f'coop.civ siege {c} 0')['out']
    r2 = host.call(f'coop.civ siege {c} 1')['out']
    print(f'  siege {c}: {r1.strip()} | {r2.strip()}', flush=True)
    return [float(b) - float(a) for a, b in re.findall(r'capture ([\d.]+) -> ([\d.]+)', r1 + ' ' + r2)]


def unvoted(host):
    m = re.search(r'sw_unvoted=(\d+)', host.call('coop.civ')['out'])
    return int(m.group(1)) if m else None


def main():
    twoinst.forget_settings()
    host, guest = start('civ_npc')
    ck = Checks()
    try:
        time.sleep(4)
        hc, _ = civ(host)
        npc = [k for k, v in hc.items() if not v['player']]
        print('  npc cities', npc[:6], flush=True)
        if not ck(len(npc) >= 2, f'two cities of other nations ({npc})'):
            return ck.result()
        # 1. joint siege
        d = siege(host, npc[0])
        ck(len(d) == 2 and abs(d[1] - 1.5) < 0.05, f'joint siege on: the second player adds x1.5 ({d})')
        ck(sl.force(host, [guest], 'civ.joint_siege', 'off'), 'the running guest got civ.joint_siege off')
        d = siege(host, npc[1])
        ck(len(d) == 2 and abs(d[0] - 1.0) < 0.05 and abs(d[1] - 1.0) < 0.05, f'joint siege off: no bonus ({d})')
        # 2. superweapon vote (type 3 costs 2000: the game's money tells that it fired, coop_civ_sw_vote.py); off
        # first (the shot), then on (the vote: a second shot right after the first may wait for the game's recharge)
        unlock(host, [3])
        ck(sl.force(host, [guest], 'civ.superweapon_vote', 'off'), 'the running guest got civ.superweapon_vote off')
        h0, g0 = set_money(host, guest, 20000.0)
        u0 = unvoted(host)
        print(' ', host.call('coop.civ super 3')['out'].strip(), flush=True)
        seen_vote = sl.wait(lambda: 'vote active' in guest.call('coop.vote')['out'], 3)
        h1, g1 = settle(host, guest, h0)
        u1 = unvoted(host)
        print(f'  money host {h0} -> {h1}, guest {g0} -> {g1}; fired without a vote {u0} -> {u1}', flush=True)
        ck(not seen_vote and h0 is not None and h1 is not None and abs(h0 - h1 - 2000) < 600,
           'superweapon vote off: it fires at once (the host\'s game took its cost), the guest sees no vote')
        ck(g1 is not None and h1 is not None and abs(g1 - h1) < 50, 'the guest\'s game shows the money after the shot')
        ck(sl.force(host, [guest], 'civ.superweapon_vote', 'on'), 'the running guest got civ.superweapon_vote on')
        print(' ', host.call('coop.civ super 3')['out'].strip(), flush=True)
        ck(sl.wait(lambda: 'vote active' in guest.call('coop.vote')['out'], 6), 'superweapon vote on: the guest sees the vote')
        host.call('coop.vote go')
        sl.wait(lambda: 'vote active' not in guest.call('coop.vote')['out'], 10)
        # 3. roles
        before = set(owners(host, range(1, 200)))
        for _ in range(2):
            print('  host:', host.call('coop.civ spawnveh 0 0 0')['out'].strip(), flush=True)
        time.sleep(2)
        ids = sorted(set(owners(host, range(1, 200))) - before)
        got = give(host, guest, ids)
        cities, veh, _ = civ_state(host)
        mine = [veh[i]['pos'] for i in ids if i in veh]
        tgt = [i for i, c in cities.items() if not c['player'] and c['buildings'] > 0]
        tgt = min(tgt, key=lambda i: dist(cities[i]['pos'], mine[0])) if tgt and mine else None
        if not ck(len(ids) == 2 and all(got.get(i) == 1 for i in ids) and tgt is not None,
                  f'the guest owns two vehicles next to an NPC city ({got}, {tgt})'):
            return ck.result()
        a = phase(host, ids, tgt, 'A (no role)')
        print(' ', guest.call('coop.role 1')['out'].strip().replace('\n', ' | '), flush=True)
        time.sleep(1.5)
        m = phase(host, ids, tgt, 'M (military, roles on)')
        ck(a and m and m >= 1.15 * a, f'roles on: the military role hits harder in the host\'s game (A {a}, M {m})')
        ck(sl.force(host, [guest], 'civ.roles', 'off'), 'the running guest got civ.roles off')
        r = phase(host, ids, tgt, 'R (military, roles off)')
        ck(a and r and r <= 1.10 * a, f'roles off: no role bonus (A {a}, R {r})')
    finally:
        sl.reset([host], KEYS)
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
