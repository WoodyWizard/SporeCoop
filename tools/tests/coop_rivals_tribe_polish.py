#!/usr/bin/env python3
"""Tribe Rivals polish: Neutral food-mat raid block, starting food parity, F9 Give hidden, staff rebuild, hut floor (BUG-1573).

1. Start two games on `tribe` fixture and record host's starting food.
2. Enable Tribe Rivals (`coop.rivals mode tribe rivals`), wait for guest tribe assignment & perspective swap.
   - Verify guest starting food is clamped to match host (not bloated 100+ NPC starting food).
3. While Neutral:
   - Verify guest's `coop.tribe rivals check_foodmat 0` returns `action=-1` and `blocked >= 1` (food-mat steal blocked).
   - Verify F9 `give` button is hidden (`vis=False`) on both host and guest in Tribe Rivals.
4. Transition to War (`coop.rivals war 1`):
   - Verify guest's `coop.tribe rivals check_foodmat 0` returns `action=33` (`0x21`, RaidFood allowed at War).
   - Run `coop.tribe rivals swap_species 1` (rebuilds held staff/tool after species swap) with zero faults.
   - Hit rival main hut twice (`hit_hut 0 1 9999`): stays protected at 10% floor (`conquered=0`, `clamped >= 1`).
PASS: 1..4 with zero faults.
SPORE_FIX_OFF=rivals.tribe_polish (`rivals_tribe_polish_off`): Neutral food-mat steal is NOT blocked (`action=0`,
`blocked=0`) -> FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_rivals_tribe_polish.py
"""
import re
import time

from coop_rivals_tribe import parse_assign, parse_int, rv_out, tr_out, wait_for
from coop_ui import Ui
import twoinst
from twoinst import Checks, start


def main():
    twoinst.forget_settings()
    host, guest = start('tribe')
    ck = Checks()
    hu, gu = Ui(host, 'host'), Ui(guest, 'guest')
    time.sleep(2.0)

    # 1. Enable Tribe Rivals & wait for assignment + perspective swap
    host.call('coop.rivals fasttimers 1')
    host.call('coop.rivals mode tribe rivals')
    ok_assign = wait_for(lambda: parse_assign(tr_out(host), 1) is not None and parse_int(tr_out(guest), 'swapped') == 1,
                         25.0)
    st_h = tr_out(host)
    ck(bool(ok_assign), f'Tribe Rivals assigned guest tribe and swapped perspective ({st_h.splitlines()[0]})')
    if not ok_assign:
        return ck.result()

    # 2. Check starting food parity: guest should match host food (not 100-154 bloated NPC food)
    a0 = parse_assign(st_h, 0)
    a1 = parse_assign(st_h, 1)
    print(f'  starting food: host={a0["food"] if a0 else -1} guest={a1["food"] if a1 else -1}', flush=True)
    ck(a0 is not None and a1 is not None and a1['food'] <= max(35, a0['food'] + 15) and a1['food'] >= 15,
       f'guest starting food matches host ({a1["food"] if a1 else -1} vs host {a0["food"] if a0 else -1})')

    # 3. Neutral: food-mat steal (0x21 RaidFood) MUST be blocked on guest targeting host food mat
    fm_neutral = (guest.call('coop.tribe rivals check_foodmat 0').get('out') or '').strip()
    print(f'  neutral check_foodmat 0: {fm_neutral}', flush=True)
    m_act = re.search(r'action=(-?\d+)', fm_neutral)
    m_blk = re.search(r'blocked=(\d+)', fm_neutral)
    ck(m_act is not None and int(m_act.group(1)) == -1 and m_blk is not None and int(m_blk.group(1)) >= 1,
       f'Neutral food-mat steal blocked by override_choose_action ({fm_neutral})')

    # F9 Give button must be hidden in Tribe Rivals (host and guest)
    host.call('coop.uistate 1')
    guest.call('coop.uistate 1')
    time.sleep(0.4)
    hs, gs = hu.state(), gu.state()
    h_give_vis = (hs.get('give') or {}).get('vis', False)
    g_give_vis = (gs.get('give') or {}).get('vis', False)
    print(f'  F9 give vis: host={h_give_vis} guest={g_give_vis}', flush=True)
    ck(not h_give_vis and not g_give_vis,
       f'F9 Give button hidden in Tribe Rivals (host={h_give_vis}, guest={g_give_vis})')
    host.call('coop.uistate 0')
    guest.call('coop.uistate 0')

    # 4. War: food-mat steal allowed (action=33 / 0x21), species swap rebuilds hand, main hut clamped at 10% floor
    host.call('coop.rivals war 1')
    war_ok = wait_for(lambda: '0-1=War' in rv_out(host) and '0-1=War' in rv_out(guest), 8.0)
    ck(bool(war_ok), 'players transitioned to War')
    if war_ok:
        fm_war = (guest.call('coop.tribe rivals check_foodmat 0').get('out') or '').strip()
        print(f'  war check_foodmat 0: {fm_war}', flush=True)
        m_wact = re.search(r'action=(-?\d+)', fm_war)
        ck(m_wact is not None and int(m_wact.group(1)) == 0x21,
           f'War food-mat raid allowed (0x21 / 33) ({fm_war})')

        sw_out = (host.call('coop.tribe rivals swap_species 1').get('out') or '').strip()
        print(f'  swap_species 1: {sw_out}', flush=True)
        ck(sw_out.startswith('ok:') or 'no species profile' in sw_out, f'swap_species ran cleanly ({sw_out})')

        hh1 = (host.call('coop.tribe rivals hit_hut 0 1 9999').get('out') or '').strip()
        hh2 = (host.call('coop.tribe rivals hit_hut 0 1 9999').get('out') or '').strip()
        print(f'  hit_hut floor: #1=[{hh1}] #2=[{hh2}]', flush=True)
        ck('conquered=0' in hh2 and parse_int(tr_out(host), 'hut_clamped') >= 1,
           f'rival main hut stays protected at 10% floor ({hh2})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
