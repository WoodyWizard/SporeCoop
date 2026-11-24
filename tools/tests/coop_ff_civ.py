#!/usr/bin/env python3
"""BUG-0899: friendly fire (all.friendly_fire) in the Civilization stage.

civ_npc fixture, two players:
  1. Ensure host (owner=0) and guest (owner=1) each own a military vehicle (purpose=0), positioned within firing
     range of each other.
  2. With all.friendly_fire off (default): ordering host's vehicle to attack guest's vehicle (`coop.civ attack`) and
     vice versa deals no damage.
  3. Host enables all.friendly_fire on: both players' military vehicles attack and lower each other's vehicle HP on
     both games.
  4. SPORE_FIX_OFF=civ.friendly_fire: even with all.friendly_fire on, cross-player vehicle attack orders deal no
     damage -> FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_ff_civ.py
"""
import re
import time

import settings_lib as sl
import twoinst
from play_civ import civ_state
from twoinst import Checks, start

KEYS = ('all.friendly_fire',)


def ff_counts(con):
    out = con.call('coop.status')['out']
    m = re.search(r'civ_ff: on=(\d+) fix=(\d+) orders=(\d+) active=(\d+) hits=(\d+) blocked=(\d+)', out)
    if not m:
        return None
    return dict(on=int(m.group(1)), fix=int(m.group(2)), orders=int(m.group(3)),
                active=int(m.group(4)), hits=int(m.group(5)), blocked=int(m.group(6)))


def main():
    twoinst.forget_settings()
    host, guest = start('civ_npc')
    ck = Checks()
    try:
        time.sleep(3)
        sl.force(host, [guest], 'all.friendly_fire', 'off')
        cities, hv, _ = civ_state(host)
        h_mil = [i for i, v in hv.items() if v['owner'] == 0 and v['purpose'] == 0 and v['hp'] > 20]
        g_mil = [i for i, v in hv.items() if v['owner'] == 1 and v['purpose'] == 0 and v['hp'] > 20]
        # Give one military vehicle to guest if guest has none yet
        if not g_mil and len(h_mil) >= 2:
            host.call(f'coop.give 1 {h_mil[1]}')
            time.sleep(1.5)
            cities, hv, _ = civ_state(host)
            h_mil = [i for i, v in hv.items() if v['owner'] == 0 and v['purpose'] == 0 and v['hp'] > 20]
            g_mil = [i for i, v in hv.items() if v['owner'] == 1 and v['purpose'] == 0 and v['hp'] > 20]
        print(f'  host military={h_mil}, guest military={g_mil}', flush=True)
        ck(bool(h_mil) and bool(g_mil), f'both players have a military vehicle (host={h_mil}, guest={g_mil})')
        if not (h_mil and g_mil):
            return ck.result()
        hv_id, gv_id = h_mil[0], g_mil[0]

        # Give both vehicles plenty of HP and place them 15 m apart
        host.call(f'coop.civ vhp {hv_id} 400')
        host.call(f'coop.civ vhp {gv_id} 400')
        p = hv[hv_id]['pos']
        host.call(f'coop.civ vput {gv_id} {p[0] + 15.0:.2f} {p[1]:.2f} {p[2]:.2f}')
        time.sleep(1.5)

        # 1. Friendly fire OFF: attack orders do nothing
        _, hv0, _ = civ_state(host)
        host.call(f'coop.civ attack {hv_id} v{gv_id} 1')
        guest.call(f'coop.civ attack {gv_id} v{hv_id} 1')
        time.sleep(4.0)
        _, hv1, _ = civ_state(host)
        print(f'  FF off: host veh {hv0[hv_id]["hp"]}->{hv1[hv_id]["hp"]}, '
              f'guest veh {hv0[gv_id]["hp"]}->{hv1[gv_id]["hp"]}', flush=True)
        ck(hv1[hv_id]['hp'] >= hv0[hv_id]['hp'] - 0.1 and hv1[gv_id]['hp'] >= hv0[gv_id]['hp'] - 0.1,
           f'friendly fire off: no cross-player vehicle damage '
           f'(h {hv0[hv_id]["hp"]}->{hv1[hv_id]["hp"]}, g {hv0[gv_id]["hp"]}->{hv1[gv_id]["hp"]})')

        # 2. Friendly fire ON: both vehicles damage each other
        ck(sl.force(host, [guest], 'all.friendly_fire', 'on'), 'guest received all.friendly_fire on')
        time.sleep(1.0)
        _, hv2, _ = civ_state(host)
        h_hp0, g_hp0 = hv2[hv_id]['hp'], hv2[gv_id]['hp']
        ff0 = ff_counts(host)
        host.call(f'coop.civ attack {hv_id} v{gv_id} 1')
        guest.call(f'coop.civ attack {gv_id} v{hv_id} 1')
        hit_both = sl.wait(lambda: (
            civ_state(host)[1][gv_id]['hp'] < g_hp0 - 10.0 and civ_state(host)[1][hv_id]['hp'] < h_hp0 - 10.0
        ), 12.0)
        _, hv3, _ = civ_state(host)
        _, gv3, _ = civ_state(guest)
        ff1 = ff_counts(host)
        print(f'  FF on: host view hv {h_hp0:.1f}->{hv3[hv_id]["hp"]:.1f}, gv {g_hp0:.1f}->{hv3[gv_id]["hp"]:.1f} | '
              f'guest view hv={gv3.get(hv_id, {}).get("hp")} gv={gv3.get(gv_id, {}).get("hp")} | ff={ff1}', flush=True)
        ck(bool(hit_both), f'friendly fire on: both players\' vehicles damaged each other '
           f'(hv {h_hp0:.1f}->{hv3[hv_id]["hp"]:.1f}, gv {g_hp0:.1f}->{hv3[gv_id]["hp"]:.1f})')
        ck(ff1 and ff0 and ff1['hits'] >= ff0['hits'] + 2,
           f'friendly fire on: host recorded FF hits ({ff0} -> {ff1})')
        ck(gv_id in gv3 and gv3[gv_id]['hp'] < g_hp0 - 10.0 and hv_id in gv3 and gv3[hv_id]['hp'] < h_hp0 - 10.0,
           f'friendly fire on: guest mirrors reflect the vehicle HP loss '
           f'(hv={gv3.get(hv_id, {}).get("hp")}, gv={gv3.get(gv_id, {}).get("hp")})')
    finally:
        sl.reset([host], KEYS)
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
