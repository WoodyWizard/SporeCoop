#!/usr/bin/env python3
"""BUG-0899: friendly fire (all.friendly_fire) in the Tribe stage.

Tribe fixture, two players:
  1. Find one host-owned citizen (owner=0) and one guest-owned citizen (owner=1) in the player tribe.
  2. Place them near each other (`coop.tribe put`).
  3. With all.friendly_fire off (default): ChooseInputActionID (`coop.tribe choose`) on a partner's citizen returns 0,
     and ordering attack (`coop.tribe act 7`) deals no damage.
  4. Host enables all.friendly_fire on: ChooseInputActionID returns 10 (attack) on both games, host ordering its
     member to attack guest's member lowers the guest member's HP on both games, and guest ordering its member to
     attack host's member lowers the host member's HP on both games.
  5. SPORE_FIX_OFF=tribe.friendly_fire: even with all.friendly_fire on, ChooseInputActionID stays 0 and attack orders
     deal no damage -> FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_ff_tribe.py
"""
import re
import time

import settings_lib as sl
import twoinst
from play_tribe import tribe_state
from twoinst import Checks, start

KEYS = ('all.friendly_fire',)


def ff_counts(con):
    out = con.call('coop.status')['out']
    m = re.search(r'tribe_ff: on=(\d+) fix=(\d+) orders=(\d+) active=(\d+) hits=(\d+) blocked=(\d+)', out)
    if not m:
        return None
    return dict(on=int(m.group(1)), fix=int(m.group(2)), orders=int(m.group(3)),
                active=int(m.group(4)), hits=int(m.group(5)), blocked=int(m.group(6)))


def choose_action(con, mid):
    out = con.call(f'coop.tribe choose {mid}')['out']
    m = re.search(r'action=(-?\d+)', out)
    return int(m.group(1)) if m else None


def main():
    twoinst.forget_settings()
    host, guest = start('tribe')
    ck = Checks()
    try:
        time.sleep(3)
        sl.force(host, [guest], 'all.friendly_fire', 'off')
        st = sl.wait(lambda: tribe_state(host) if any(m['owner'] == 1 for m in tribe_state(host).values()) else None, 15)
        ck(bool(st), 'player tribe has both host and guest members')
        if not st:
            return ck.result()
        h_ids = [i for i, m in st.items() if m['tribe'] == 'p' and m['owner'] == 0 and m['hp'] > 10]
        g_ids = [i for i, m in st.items() if m['tribe'] == 'p' and m['owner'] == 1 and m['hp'] > 10]
        ck(bool(h_ids) and bool(g_ids), f'found host ({h_ids}) and guest ({g_ids}) members')
        if not (h_ids and g_ids):
            return ck.result()
        hm, gm = h_ids[0], g_ids[0]
        hp_pos = st[hm]['pos']
        # Place the guest member 2.5 m from the host member so melee swings land quickly
        host.call(f'coop.tribe put {gm} {hp_pos[0] + 2.5:.2f} {hp_pos[1]:.2f} {hp_pos[2]:.2f}')
        time.sleep(1.5)

        # Select own member on each game
        for i, m in st.items():
            if m['tribe'] == 'p':
                host.call(f'coop.tribe select {i} {1 if i == hm else 0}')
                guest.call(f'coop.tribe select {i} {1 if i == gm else 0}')
        time.sleep(0.5)

        # 1. Friendly fire OFF: choose returns 0, attack deals no damage
        hc0, gc0 = choose_action(host, gm), choose_action(guest, hm)
        print(f'  FF off: choose host->{gm}={hc0} guest->{hm}={gc0}', flush=True)
        ck(hc0 == 0 and gc0 == 0, f'friendly fire off: ChooseInputActionID returns 0 ({hc0}, {gc0})')
        st0 = tribe_state(host)
        host.call(f'coop.tribe act 7 {gm}')
        guest.call(f'coop.tribe act 7 {hm}')
        time.sleep(3.0)
        st1 = tribe_state(host)
        ck(st1[gm]['hp'] >= st0[gm]['hp'] - 0.1 and st1[hm]['hp'] >= st0[hm]['hp'] - 0.1,
           f'friendly fire off: attack orders deal no damage (hm {st0[hm]["hp"]}->{st1[hm]["hp"]}, '
           f'gm {st0[gm]["hp"]}->{st1[gm]["hp"]})')

        # 2. Friendly fire ON: choose returns 10 (attack), both players damage each other's member
        ck(sl.force(host, [guest], 'all.friendly_fire', 'on'), 'guest received all.friendly_fire on')
        time.sleep(1.0)
        hc1, gc1 = choose_action(host, gm), choose_action(guest, hm)
        print(f'  FF on: choose host->{gm}={hc1} guest->{hm}={gc1}', flush=True)
        ck(hc1 == 10 and gc1 == 10, f'friendly fire on: ChooseInputActionID returns 10 (attack) ({hc1}, {gc1})')

        st2 = tribe_state(host)
        h_hp0, g_hp0 = st2[hm]['hp'], st2[gm]['hp']
        ff0 = ff_counts(host)
        host.call(f'coop.tribe act 7 {gm}')
        guest.call(f'coop.tribe act 7 {hm}')
        hit_both = sl.wait(lambda: (
            tribe_state(host)[gm]['hp'] < g_hp0 - 1.0 and tribe_state(host)[hm]['hp'] < h_hp0 - 1.0
        ), 12.0)
        st3 = tribe_state(host)
        gst3 = tribe_state(guest)
        ff1 = ff_counts(host)
        print(f'  FF on: host view hm {h_hp0:.1f}->{st3[hm]["hp"]:.1f}, gm {g_hp0:.1f}->{st3[gm]["hp"]:.1f} | '
              f'guest view hm={gst3.get(hm, {}).get("hp")} gm={gst3.get(gm, {}).get("hp")} | ff={ff1}', flush=True)
        ck(bool(hit_both), f'friendly fire on: both players damaged each other\'s tribe member '
           f'(hm {h_hp0:.1f}->{st3[hm]["hp"]:.1f}, gm {g_hp0:.1f}->{st3[gm]["hp"]:.1f})')
        ck(ff1 and ff0 and ff1['hits'] >= ff0['hits'] + 2,
           f'friendly fire on: host recorded FF hits ({ff0} -> {ff1})')
        ck(gm in gst3 and gst3[gm]['hp'] < g_hp0 - 1.0 and hm in gst3 and gst3[hm]['hp'] < h_hp0 - 1.0,
           f'friendly fire on: guest mirrors reflect the HP loss (hm={gst3.get(hm, {}).get("hp")}, '
           f'gm={gst3.get(gm, {}).get("hp")})')
    finally:
        sl.reset([host], KEYS)
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
