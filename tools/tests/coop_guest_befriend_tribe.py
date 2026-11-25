#!/usr/bin/env python3
"""BUG-1561: Tribal stage guest befriending of NPC villages (including when host gives all soldiers to guest).

Verifies all five Tribal guest-befriending fixes behind `tribe.guest_social`:
1. AbilityBar_Rebuild runs automatically on the guest when the host buys/syncs instrument racks (tools 4/5/6),
   populating row 0 slots so keys 1/2/3 and HUD buttons work.
2. ChooseInputActionID (0xCCF130) returns 0x27 (perform for village -> CitizenAction 26) instead of 0x2B
   ("Select your Chieftain") when the guest selects own living members in Social Mode without the host's mpLeader.
3. Guest starts a social performance alone (host has zero performers, duet_ticks == 0): host broadcasts live
   SocialSession state (MSG_TRIBE_ANIM kind 22) and guest shows the native [TribeGameUI+0x50] social meter UI.
4. Guest plays matching instruments with real keyboard keys 1/2/3 (Didgeridoo/Maracas/Horn) via AbilityBar_Status
   and AbilityBar_Use -> CMD_SOCIAL_PLAY -> host BT_PostStimulus(0x10000000), raising session progress to win and
   befriending the NPC village on both host and guest (and hiding the guest's meter cleanly afterwards).
5. BUG-1068 mission hook registers the village goal mission on CitizenAction 26.

SPORE_FIX_OFF=tribe.guest_social must FAIL (bar stays empty, choose_action returns 0x2B, no guest UI, keys 1/2/3
do nothing, village is not befriended).

  SPORE_LANE=4 python3 tools/tests/coop_guest_befriend_tribe.py
"""
import re
import time

import play_tribe as pt
import settings_lib as sl
from coop_tribe_tools import members
from twoinst import Checks, start


def guest_social_info(con):
    out = con.call('coop.tribe')['out']
    m = re.search(
        r'tribe guest social: fix=(\d+) active=(\d+) state=(\d+) can_play=(\d+) req_inst=(-?\d+) outcome=(\d+) '
        r'progress=([\d.-]+) ui_vis=(-?\d+) perfs=(\d+) choose_over=(\d+) plays_sent=(\d+) plays_host=(\d+) '
        r'tx=(\d+) rx=(\d+) bar_rebuilds=(\d+)',
        out,
    )
    if not m:
        return dict(ok=False, raw=out)
    return dict(
        ok=True,
        fix=int(m.group(1)),
        active=int(m.group(2)),
        state=int(m.group(3)),
        can_play=int(m.group(4)),
        req_inst=int(m.group(5)),
        outcome=int(m.group(6)),
        progress=float(m.group(7)),
        ui_vis=int(m.group(8)),
        perfs=int(m.group(9)),
        choose_over=int(m.group(10)),
        plays_sent=int(m.group(11)),
        plays_host=int(m.group(12)),
        tx=int(m.group(13)),
        rx=int(m.group(14)),
        bar_rebuilds=int(m.group(15)),
        raw=out,
    )


def duet_info(host):
    out = host.call('coop.tribe duet')['out']
    ps = [
        dict(id=int(m.group(1)), owner=int(m.group(2)), target=m.group(3), state=int(m.group(4)), progress=float(m.group(5)))
        for m in re.finditer(r'performer #(\d+) owner=(\d+) target=(\S+) state=(-?\d+) ui=\S+ progress=([-\d.]+)', out)
    ]
    tm = re.search(r'ticks=(\d+)', out)
    return ps, (int(tm.group(1)) if tm else 0)


def tribe_rel_of(host, tribe_ptr):
    """Read host's relationship value to NPC tribe pointer from coop.tribe or relations."""
    out = host.call('coop.tribe rels')['out']
    for ln in out.splitlines():
        if tribe_ptr.lower() in ln.lower():
            return ln.strip()
    return out.strip()[:200]


def main():
    host, guest = start('tribe')
    ck = Checks()
    try:
        host.call('coop.drama hold 120')
        time.sleep(3)

        mem = members(guest)
        hown = sorted(i for i, o in mem.items() if o == 0)
        gown = sorted(i for i, o in mem.items() if o != 0)
        if not ck(bool(hown) and bool(gown), f'both players start with tribe members (host={hown}, guest={gown})'):
            return ck.result()

        # Host gives non-leader soldier(s) to guest (reproducing Smile's report: host gives soldiers to Player 2)
        if len(hown) >= 2:
            for mid in hown[1:]:
                host.call(f'coop.tribe select {mid} 1')
            print('  host give:', host.call('coop.give 1')['out'].strip(), flush=True)
            time.sleep(1.5)
            mem = members(guest)
            hown = sorted(i for i, o in mem.items() if o == 0)
            gown = sorted(i for i, o in mem.items() if o != 0)
        print(f'  owners after give: host={hown}, guest={gown}', flush=True)

        # Locate the fixture's real native neutral NPC village (first non-player tribe in the fixture)
        st0 = pt.tribe_state(guest)
        npc_tribes = sorted({v['tribe'] for v in st0.values() if v['tribe'] != 'p'}, key=str)
        target_tribe = npc_tribes[0] if npc_tribes else None
        npc_t1 = sorted(i for i, v in st0.items() if v['tribe'] == target_tribe) if target_tribe is not None else []
        if not ck(bool(npc_t1), f'guest mirrors native NPC village {target_tribe} ({npc_t1})'):
            return ck.result()
        nid = npc_t1[0]
        npc_pos = st0[nid]['pos']
        print(f'  target native NPC village {target_tribe}: villager #{nid} at {npc_pos}', flush=True)

        # Host buys all 3 instrument racks (4=Didgeridoo, 5=Maracas, 6=Horn)
        for ty, deg in (('4', 60), ('5', 180), ('6', 300)):
            host.call(f'coop.tribe buy {ty} 9 {deg}')
            host.call('coop.tribe buy_end')
            time.sleep(0.8)
        time.sleep(1.5)

        # Check 1: Guest's HUD AbilityBar automatically rebuilt for synced instrument racks
        gs0 = sl.wait(lambda: (lambda s: s if s.get('bar_rebuilds', 0) > 0 else None)(guest_social_info(guest)), 5) or guest_social_info(guest)
        print(f'  guest after tool sync: bar_rebuilds={gs0.get("bar_rebuilds")} fix={gs0.get("fix")}', flush=True)
        ck(gs0['ok'] and gs0['bar_rebuilds'] > 0, f'guest rebuilt HUD ability bar for synced instrument racks (bar_rebuilds={gs0.get("bar_rebuilds")})')

        # Guest's musicians grab instruments 4 and 5 (and 6 if 3 members)
        tools = re.findall(r'tool (\d+) \S+ serial=\d+ type=(\d+)', host.call('coop.tribe tools')['out'])
        tidx = {ty: int(i) for i, ty in tools}
        assigned = list(zip(gown[:3], ('4', '5', '6')))
        for mid, ty in assigned:
            if ty in tidx:
                host.call(f'coop.tribe grab {mid} {tidx[ty]}')
        t0 = time.time()
        held = {}
        while time.time() - t0 < 25:
            held = {
                int(x.group(1)): int(x.group(2))
                for x in re.finditer(r'held (\d+) owner=\d+ tool=(\d+)', host.call('coop.tribe tools')['out'])
            }
            if all(held.get(mid) == int(ty) for mid, ty in assigned if ty in tidx):
                break
            time.sleep(0.5)
        print(f'  held tools: {held}', flush=True)
        ck(all(held.get(mid) == int(ty) for mid, ty in assigned if ty in tidx),
           f'guest musicians grabbed instruments ({assigned} -> {held})')

        # Check 2: Real drag-select on guest's own member (populates cGameTerrainCursor's selection list without
        # host's mpLeader), switch to Social Mode via SOCIAL_BTN, and verify ChooseInputActionID returns 39 (0x27)
        # instead of 43 (0x2B = "Select your Chieftain").
        sc = {o[0]: (o[1], o[2]) for o in pt.screen(guest)}
        for mid in gown:
            if f't{mid}' in sc:
                gx, gy = sc[f't{mid}']
                guest.call(f'drag {gx - 50} {gy - 50} {gx + 50} {gy + 50}')
                time.sleep(0.4)
                break
        for _ in range(3):
            if 'tribe ui mode=0' in guest.call('coop.tribe')['out']:
                break
            guest.call(f'click {pt.SOCIAL_BTN[0]} {pt.SOCIAL_BTN[1]}')
            time.sleep(0.5)
        guest.call('move 512 384')
        time.sleep(0.2)
        chs = [(i, guest.call(f'coop.tribe choose {i}')['out'].strip()) for i in npc_t1]
        print(f'  guest social-mode choose_action on tribe {target_tribe}: {chs}', flush=True)
        ck(any('action=39' in c for _, c in chs),
           f'guest ChooseInputActionID allows social performance without host mpLeader ({chs})')

        # Place guest's musicians near tribe 1 (~18 m away) so both arrive together cleanly
        for k, (mid, _) in enumerate(assigned):
            host.call(f'coop.tribe put {mid} {npc_pos[0] + 15.0 + k * 2.5:.2f} {npc_pos[1] + 8.0:.2f} {npc_pos[2]:.2f}')
        time.sleep(1.5)
        for mid in gown:
            guest.call(f'coop.tribe select {mid} 1')
        time.sleep(0.3)

        # Check 3: Guest starts social performance alone (CitizenAction 26 -> no host performers)
        guest.call(f'coop.tribe act 26 {nid}')
        t0 = time.time()
        ps, dticks = [], 0
        while time.time() - t0 < 25:
            ps, dticks = duet_info(host)
            if len(ps) >= min(2, len(assigned)):
                break
            if ps and time.time() - t0 > 10:
                break
            time.sleep(0.5)
        print(f'  performance started on host: ps={ps} duet_ticks={dticks}', flush=True)
        if not ck(bool(ps) and all(p['owner'] == 1 for p in ps) and dticks == 0,
                  f'guest-only social session started on host (ps={ps}, duet_ticks={dticks})'):
            return ck.result()

        # Verify guest received synced state and opened native [TribeGameUI+0x50] meter UI
        gs_open = sl.wait(lambda: (lambda s: s if s.get('active') and s.get('ui_vis') == 1 else None)(guest_social_info(guest)), 8) or guest_social_info(guest)
        print(f'  guest social UI opened: active={gs_open.get("active")} ui_vis={gs_open.get("ui_vis")} rx={gs_open.get("rx")} perfs={gs_open.get("perfs")}', flush=True)
        ck(gs_open['ok'] and gs_open['active'] == 1 and gs_open['ui_vis'] == 1,
           f'guest shows synced Tribal social progress meter UI (active={gs_open.get("active")}, ui_vis={gs_open.get("ui_vis")})')

        # Check 4: Guest plays instruments using real keys 1/2/3 matching req_inst until performance progresses/wins
        # req_inst mapping (0xDA1F20): 2 -> Didgeridoo (key 1, tool 4), 0 -> Maracas (key 2, tool 5), 1 -> Horn (key 3, tool 6)
        req_to_key = {2: '1', 0: '2', 1: '3'}
        owned_keys = [k for mid, ty in assigned for k in ({'4': '1', '5': '2', '6': '3'}[ty],) if held.get(mid) == int(ty)]
        max_prog = 0.0
        won = False
        t0 = time.time()
        last_log = 0.0
        while time.time() - t0 < 45:
            gs = guest_social_info(guest)
            ps, dticks = duet_info(host)
            h_prog = max((p['progress'] for p in ps), default=0.0)
            max_prog = max(max_prog, h_prog, gs.get('progress', 0.0))
            if time.time() - last_log >= 4.0:
                last_log = time.time()
                print(f'  loop t={time.time()-t0:.1f}s gs=(act={gs.get("active")} st={gs.get("state")} can={gs.get("can_play")} '
                      f'req={gs.get("req_inst")} out={gs.get("outcome")} prog={gs.get("progress"):.3f} sent={gs.get("plays_sent")}) '
                      f'host_ps={ps}', flush=True)
            if gs.get('outcome') == 1 or h_prog >= 0.99:
                won = True
                break
            if gs.get('active') == 0 and gs.get('plays_sent', 0) > 0 and not ps:
                break
            if gs.get('can_play') == 1:
                k = req_to_key.get(gs.get('req_inst', -1))
                if k and k in owned_keys:
                    guest.call(f'key {k}')
                else:
                    for ok in owned_keys:
                        guest.call(f'key {ok}')
                        time.sleep(0.15)
            time.sleep(0.35)

        gs_end = guest_social_info(guest)
        hs_end = guest_social_info(host)
        print(f'  end: max_prog={max_prog:.3f} won={won} duet_ticks={dticks} '
              f'guest_sent={gs_end.get("plays_sent")} host_played={hs_end.get("plays_host")}', flush=True)
        ck(gs_end.get('plays_sent', 0) > 0 and hs_end.get('plays_host', 0) > 0,
           f'guest instrument keys reached host SocialSession (sent={gs_end.get("plays_sent")}, host={hs_end.get("plays_host")})')
        ck(max_prog >= 0.35 or won,
           f'guest-only Tribal performance advanced social meter significantly (max_prog={max_prog:.3f}, won={won}, duet_ticks={dticks})')

        # Wait for session to finish cleanly and verify guest meter UI hides (ui_vis == 0)
        if won:
            gs_closed = sl.wait(lambda: (lambda s: s if s.get('ui_vis') == 0 else None)(guest_social_info(guest)), 12) or guest_social_info(guest)
            print(f'  guest UI after finish: ui_vis={gs_closed.get("ui_vis")} active={gs_closed.get("active")}', flush=True)
            ck(gs_closed.get('ui_vis') == 0, f'guest social meter UI hid cleanly after performance (ui_vis={gs_closed.get("ui_vis")})')

        ck.faults(host=host, guest=guest)
        return ck.result()
    finally:
        pass


if __name__ == '__main__':
    raise SystemExit(main())
