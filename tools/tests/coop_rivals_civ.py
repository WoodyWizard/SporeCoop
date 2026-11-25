#!/usr/bin/env python3
"""WP-R5 (BUG-1549): Civilization Rivals mode (R1 perspective swap & own money, R2 NPC cCivilization takeover & AI
disable with base distance selection, R4 per-player NPC nation relations & Civ influence TurnAgainst/Protect/SmoothOver/
buy, alliance money & spice gifts, Neutral/Allied PvP block vs War vehicle PvP & ordinary city capture, R7 capital
inviolability & superweapon block, and >50% NPC cities win -> catch-up -> Civ draft).

civ_npc fixture, two players:
  1. Host switches Civ to Rivals with distance=far and fasttimers=1.
  2. R2 Takeover: guest is assigned the farthest free NPC cCivilization from host's capital, NPC AI is disabled
     (ai_blocked > 0), and guest nation has its own political ID != host pol.
  3. R1 Perspective Swap & Own Money: guest's GetPlayerCivilization / GetPlayerPoliticalID return guest's taken-over
     nation; host and guest hold independent Sporebucks wallets (addmoney changes only that player's money).
  4. R4 Per-player NPC Relations & Civ Influence:
     - Buy influence charges buyer's own Civ money (500 Sporebucks) without touching the other player's money.
     - Guest protects npcA -> Host TurnAgainst on npcA is rejected ("protected").
     - Host TurnAgainst on unprotected npcB lowers guest's relation with npcB (host table & guest synced/live)
       without altering host's relation with npcB.
     - Guest SmoothOver on npcB raises guest's relation back on both host and guest.
  5. Alliance Money/Spice Gifts & Neutral/Allied PvP Block:
     - Form Alliance (0-1=Ally): host sends money gift (500) and guest sends spice gift (200) via SK_GIFT -> both
       wallets update on host and guest.
     - Allied vehicle PvP hit (pvp_test) is blocked (0 HP lost, pvp_blocked increments).
  6. War PvP, Ordinary City Capture vs R7 Capital Inviolability & Superweapon Block:
     - Break alliance -> fast timer transitions both to War (0-1=War).
     - War vehicle PvP hit damages guest vehicle (HP drops, pvp_allowed increments).
     - Guest captures ordinary NPC city npc_ci_a, then Host captures npc_ci_a from Guest at War (war_captures >= 1,
       owner changes on host & guest).
     - Host attempts to capture Guest's capital, convert Guest's capital, and target targeted/global superweapons at
       Guest's capital -> ALL blocked (pol unchanged, cap_blocked/conv_blocked/sw_blocked increment, can_target=0).
       Guest superweapon targeting Host's capital is also blocked.
  7. Stage End (>50% NPC cities) -> Catch-up -> Civ Draft:
     - At 1/2 non-capital cities (50%), race is not won yet.
     - Host captures 2nd NPC city (2/2 > 50%) -> winner=0, catchup=1 synced on both.
     - Catch-up timer expires -> race_ended=1, draft_started=1, transition_ready=1, draft active on guest.
     - Guest picks first Civ draft item -> total_picks=1 payout=100 synced and draft_applied >= 1 on guest.
  8. SPORE_FIX_OFF=rivals.civ: Civ Rivals disabled (fix=0, active=0, no takeover/swap) -> FAIL.

  SPORE_LANE=5 python3 tools/tests/coop_rivals_civ.py
"""
import math
import re
import time

import twoinst
from play_civ import civ_state
from twoinst import Checks, start


def rivals_out(con):
    return con.call('coop.rivals').get('out') or ''


def cr_out(con):
    return con.call('coop.civ rivals').get('out') or ''


def parse_cr(txt):
    """Parse `coop.civ rivals` output into header dict, nations dict, npcrels dict, and draft_items list."""
    hdr = {}
    first = txt.splitlines()[0] if txt else ''
    for k, v in re.findall(r'(\w+)=([^\s]+)', first):
        hdr[k] = v
    nations = {}
    for m in re.finditer(
        r'nation p=(\d+) pol=0x([0-9A-Fa-f]+) cities=(\d+) vehs=(\d+) wealth=(-?\d+) '
        r'cap=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)',
        txt,
    ):
        pid = int(m.group(1))
        nations[pid] = dict(
            pol=int(m.group(2), 16),
            cities=int(m.group(3)),
            vehs=int(m.group(4)),
            wealth=int(m.group(5)),
            cap=(float(m.group(6)), float(m.group(7)), float(m.group(8))),
        )
    npcrels = {}
    for m in re.finditer(
        r'npcrel pol=0x([0-9A-Fa-f]+) p0=(-?[\d.]+) p1=(-?[\d.]+) p2=(-?[\d.]+) p3=(-?[\d.]+)',
        txt,
    ):
        npcrels[int(m.group(1), 16)] = [float(m.group(2)), float(m.group(3)), float(m.group(4)), float(m.group(5))]
    for m in re.finditer(r'npcrel pol=0x([0-9A-Fa-f]+) synced=(-?[\d.]+) live=(-?[\d.]+)', txt):
        npcrels[int(m.group(1), 16)] = dict(synced=float(m.group(2)), live=float(m.group(3)))
    draft_items = []
    dm = re.search(r'draft_items:([^\n]+)', txt)
    if dm:
        for hx in re.findall(r'0x([0-9A-Fa-f]+)', dm.group(1)):
            draft_items.append(int(hx, 16))
    return hdr, nations, npcrels, draft_items


def wait_for(fn, timeout=15.0, step=0.25):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


def dist3(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def main():
    twoinst.forget_settings()
    host, guest = start('civ_npc')
    ck = Checks()
    try:
        time.sleep(2.5)

        # Record all initial cities on host before takeover so we can verify base_distance=Far selection
        cities0, _, _ = civ_state(host)
        ck(len(cities0) >= 4, f'civ_npc fixture has at least 4 cities (got {len(cities0)})')
        if len(cities0) < 4:
            return ck.result()

        # 1. Enable Civ Rivals (distance=far, fasttimers=1)
        host.call('coop.rivals distance far')
        host.call('coop.rivals mode civ rivals')
        host.call('coop.rivals fasttimers 1')

        # 2. R2 Takeover: wait for host to assign an NPC cCivilization to guest and disable its AI
        h_cr = wait_for(
            lambda: cr_out(host)
            if ('active=1' in cr_out(host) and 'assigned=2' in cr_out(host) and 'fix=1' in cr_out(host))
            else None,
            12.0,
        )
        if not h_cr:
            h_cr = cr_out(host)
        h_hdr, h_nat, _, _ = parse_cr(h_cr)
        print('  host civ_rivals:', h_cr.splitlines()[0] if h_cr else '<empty>', flush=True)
        ck(
            h_hdr.get('fix') == '1' and h_hdr.get('active') == '1' and h_hdr.get('assigned') == '2',
            f'R2 host Civ Rivals active with 2 assigned nations ({h_cr.splitlines()[0] if h_cr else ""})',
        )
        if h_hdr.get('fix') != '1' or h_hdr.get('active') != '1' or 0 not in h_nat or 1 not in h_nat:
            return ck.result()

        host_pol = h_nat[0]['pol']
        guest_pol = h_nat[1]['pol']
        ck(
            host_pol != 0 and guest_pol != 0 and host_pol != guest_pol and int(h_hdr.get('takeovers', '0')) >= 1,
            f'R2 guest assigned distinct NPC nation (host=0x{host_pol:08X}, guest=0x{guest_pol:08X}, '
            f'takeovers={h_hdr.get("takeovers")})',
        )

        # Verify base_distance=Far chose the farthest NPC capital from host's capital
        host_cap = h_nat[0]['cap']
        guest_cap = h_nat[1]['cap']
        npc_dists = sorted(
            (dist3(host_cap, c['pos']), ci)
            for ci, c in cities0.items()
            if dist3(host_cap, c['pos']) > 5.0
        )
        guest_dist = dist3(host_cap, guest_cap)
        max_npc_dist = npc_dists[-1][0] if npc_dists else 0.0
        ck(
            abs(guest_dist - max_npc_dist) < 5.0,
            f'R2 distance=Far picked farthest NPC capital (guest_d={guest_dist:.1f} m, max_npc_d={max_npc_dist:.1f} m)',
        )

        # Wait for Civilization_AI hook to intercept guest nation's NPC AI
        ai_ok = wait_for(lambda: int(parse_cr(cr_out(host))[0].get('ai_blocked', '0')) >= 1, 10.0)
        ck(bool(ai_ok), f'R2 NPC AI disabled on guest-owned nation (ai_blocked={parse_cr(cr_out(host))[0].get("ai_blocked")})')

        # 3. R1 Guest perspective swap & independent own money
        g_cr = wait_for(
            lambda: cr_out(guest)
            if (
                parse_cr(cr_out(guest))[0].get('swap') == '1'
                and parse_cr(cr_out(guest))[0].get('player_pol', '').lower() == f'0x{guest_pol:08x}'
            )
            else None,
            15.0,
        )
        if not g_cr:
            g_cr = cr_out(guest)
        g_hdr, _, _, _ = parse_cr(g_cr)
        print('  guest civ_rivals:', g_cr.splitlines()[0] if g_cr else '<empty>', flush=True)
        ck(
            g_hdr.get('active') == '1'
            and g_hdr.get('swap') == '1'
            and g_hdr.get('player_pol', '').lower() == f'0x{guest_pol:08x}'
            and g_hdr.get('api_pol', '').lower() == f'0x{guest_pol:08x}'
            and int(g_hdr.get('swap_hits', '0')) > 0,
            f'R1 guest perspective swapped to own nation (swap={g_hdr.get("swap")} '
            f'player_pol={g_hdr.get("player_pol")} api_pol={g_hdr.get("api_pol")} hits={g_hdr.get("swap_hits")})',
        )

        # Independent money check: give host +1234 and guest +777, verify only each player's wallet moves
        h_m0 = int(parse_cr(cr_out(host))[0].get('player_money', '0'))
        g_m0 = int(parse_cr(cr_out(guest))[0].get('player_money', '0'))
        host.call('coop.civ rivals addmoney 0 1234')
        host.call('coop.civ rivals addmoney 1 777')
        m_synced = wait_for(
            lambda: (
                int(parse_cr(cr_out(host))[0].get('player_money', '0')) >= h_m0 + 1200
                and int(parse_cr(cr_out(guest))[0].get('player_money', '0')) >= g_m0 + 750
            ),
            8.0,
        )
        h_m1 = int(parse_cr(cr_out(host))[0].get('player_money', '0'))
        g_m1 = int(parse_cr(cr_out(guest))[0].get('player_money', '0'))
        ck(
            bool(m_synced) and abs((h_m1 - h_m0) - 1234) < 350 and abs((g_m1 - g_m0) - 777) < 350,
            f'R1 independent own money on host ({h_m0}->{h_m1}, +1234) and guest ({g_m0}->{g_m1}, +777)',
        )

        # 4. R4 Per-player NPC nation relations & Civ influence
        # 4a. Buy influence charges buyer's OWN Civ money (500 Sporebucks per buy)
        h_w0 = parse_cr(cr_out(host))[1][0]['wealth']
        g_w0 = parse_cr(cr_out(host))[1][1]['wealth']
        buy_h = host.call('coop.rivals influence buy').get('out') or ''
        time.sleep(0.5)
        h_hdr_b1, h_nat_b1, _, _ = parse_cr(cr_out(host))
        ck(
            'ok' in buy_h
            and int(h_hdr_b1.get('inf_buy', '0')) >= 1
            and (h_w0 - h_nat_b1[0]['wealth']) >= 400
            and abs(h_nat_b1[1]['wealth'] - g_w0) < 250,
            f'R4 host influence buy charged host Civ money ({h_w0}->{h_nat_b1[0]["wealth"]}), '
            f'guest untouched ({g_w0}->{h_nat_b1[1]["wealth"]}), inf_buy={h_hdr_b1.get("inf_buy")}',
        )

        h_w1 = h_nat_b1[0]['wealth']
        g_w1 = h_nat_b1[1]['wealth']
        guest.call('coop.rivals influence buy')
        buy_g_ok = wait_for(lambda: int(parse_cr(cr_out(host))[0].get('inf_buy', '0')) >= 2, 8.0)
        h_hdr_b2, h_nat_b2, _, _ = parse_cr(cr_out(host))
        ck(
            bool(buy_g_ok)
            and (g_w1 - h_nat_b2[1]['wealth']) >= 400
            and abs(h_nat_b2[0]['wealth'] - h_w1) < 250,
            f'R4 guest influence buy charged guest Civ money ({g_w1}->{h_nat_b2[1]["wealth"]}), '
            f'host untouched ({h_w1}->{h_nat_b2[0]["wealth"]}), inf_buy={h_hdr_b2.get("inf_buy")}',
        )

        # Grant influence for Protect / TurnAgainst / SmoothOver tests
        host.call('coop.rivals influence add 0 100')
        host.call('coop.rivals influence add 1 100')
        wait_for(lambda: len(parse_cr(cr_out(host))[2]) >= 2 and len(parse_cr(cr_out(guest))[2]) >= 2, 10.0)
        _, _, h_npcs, _ = parse_cr(cr_out(host))
        _, _, g_npcs, _ = parse_cr(cr_out(guest))
        common_npcs = sorted(set(h_npcs.keys()) & set(g_npcs.keys()))
        print(f'  remaining NPC pols synced: {[hex(p) for p in common_npcs]}', flush=True)
        ck(len(common_npcs) >= 2, f'R4 at least 2 remaining NPC nations tracked & synced ({len(common_npcs)})')
        if len(common_npcs) < 2:
            return ck.result()
        npc_a, npc_b = common_npcs[0], common_npcs[1]

        # Guest protects npc_a; Host TurnAgainst on npc_a must be rejected
        guest.call(f'coop.rivals influence protect 0x{npc_a:08X}')
        prot_ok = wait_for(lambda: 'prots=1' in rivals_out(host), 8.0)
        ck(bool(prot_ok), f'R4 guest Protect on NPC 0x{npc_a:08X} synced to host')
        turn_blocked = host.call(f'coop.rivals influence turn 0x{npc_a:08X} 1').get('out') or ''
        ck(
            'error:' in turn_blocked and 'protected' in turn_blocked,
            f'R4 TurnAgainst on protected NPC 0x{npc_a:08X} rejected ({turn_blocked.strip()})',
        )

        # Host TurnAgainst on unprotected npc_b targeting guest (1): lowers guest rel only, synced to guest
        p0_before, p1_before = h_npcs[npc_b][0], h_npcs[npc_b][1]
        turn_ok = host.call(f'coop.rivals influence turn 0x{npc_b:08X} 1').get('out') or ''
        turn_synced = wait_for(
            lambda: (
                npc_b in parse_cr(cr_out(guest))[2]
                and parse_cr(cr_out(guest))[2][npc_b]['synced'] < p1_before - 1.5
            ),
            8.0,
        )
        _, _, h_npcs_t, _ = parse_cr(cr_out(host))
        _, _, g_npcs_t, _ = parse_cr(cr_out(guest))
        ck(
            'ok' in turn_ok
            and bool(turn_synced)
            and h_npcs_t[npc_b][1] < p1_before - 1.5
            and abs(h_npcs_t[npc_b][0] - p0_before) < 0.05
            and g_npcs_t[npc_b]['live'] < p1_before - 1.5,
            f'R4 TurnAgainst lowered guest rel with 0x{npc_b:08X} only '
            f'(host p0 {p0_before:.2f}->{h_npcs_t[npc_b][0]:.2f}, p1 {p1_before:.2f}->{h_npcs_t[npc_b][1]:.2f}, '
            f'guest synced={g_npcs_t[npc_b]["synced"]:.2f} live={g_npcs_t[npc_b]["live"]:.2f})',
        )

        # Guest SmoothOver on npc_b raises guest rel back on both host and guest
        p1_after_turn = h_npcs_t[npc_b][1]
        guest.call(f'coop.rivals influence smooth 0x{npc_b:08X}')
        smooth_synced = wait_for(
            lambda: (
                npc_b in parse_cr(cr_out(host))[2]
                and parse_cr(cr_out(host))[2][npc_b][1] > p1_after_turn + 1.5
                and npc_b in parse_cr(cr_out(guest))[2]
                and parse_cr(cr_out(guest))[2][npc_b]['synced'] > p1_after_turn + 1.5
            ),
            8.0,
        )
        _, _, h_npcs_s, _ = parse_cr(cr_out(host))
        _, _, g_npcs_s, _ = parse_cr(cr_out(guest))
        ck(
            bool(smooth_synced) and g_npcs_s[npc_b]['live'] > p1_after_turn + 1.5,
            f'R4 SmoothOver raised guest rel with 0x{npc_b:08X} '
            f'(host p1 {p1_after_turn:.2f}->{h_npcs_s[npc_b][1]:.2f}, '
            f'guest synced={g_npcs_s[npc_b]["synced"]:.2f} live={g_npcs_s[npc_b]["live"]:.2f})',
        )

        # 5. Alliance Money/Spice Gifts + Allied Vehicle PvP Block
        host.call('coop.rivals ally 1')
        wait_for(lambda: '0->1:ally' in rivals_out(guest), 8.0)
        guest.call('coop.rivals accept 0')
        allied = wait_for(lambda: '0-1=Ally' in rivals_out(host) and '0-1=Ally' in rivals_out(guest), 8.0)
        ck(bool(allied), 'Alliance established (0-1=Ally) on host and guest')

        # Host -> Guest money gift (500)
        _, nat_g0, _, _ = parse_cr(cr_out(host))
        hw0, gw0 = nat_g0[0]['wealth'], nat_g0[1]['wealth']
        gift1 = host.call('coop.civ rivals gift 1 money 500').get('out') or ''
        g1_sync = wait_for(lambda: int(parse_cr(cr_out(guest))[0].get('player_money', '0')) >= gw0 + 400, 8.0)
        hdr_g1, nat_g1, _, _ = parse_cr(cr_out(host))
        ck(
            'ok' in gift1
            and bool(g1_sync)
            and hdr_g1.get('gifts', '').startswith('1/')
            and (hw0 - nat_g1[0]['wealth']) >= 400
            and (nat_g1[1]['wealth'] - gw0) >= 400,
            f'Alliance money gift host->guest 500 synced (h {hw0}->{nat_g1[0]["wealth"]}, '
            f'g {gw0}->{nat_g1[1]["wealth"]}, gifts={hdr_g1.get("gifts")})',
        )

        # Guest -> Host spice gift (200) via SK_GIFT
        hw1, gw1 = nat_g1[0]['wealth'], nat_g1[1]['wealth']
        gift2 = guest.call('coop.civ rivals gift 0 spice 200').get('out') or ''
        g2_sync = wait_for(lambda: parse_cr(cr_out(host))[0].get('spice') == '1', 8.0)
        hdr_g2, nat_g2, _, _ = parse_cr(cr_out(host))
        ck(
            'ok' in gift2
            and bool(g2_sync)
            and hdr_g2.get('gifts', '').startswith('2/')
            and (gw1 - nat_g2[1]['wealth']) >= 150
            and (nat_g2[0]['wealth'] - hw1) >= 150,
            f'Alliance spice gift guest->host 200 synced via SK_GIFT (g {gw1}->{nat_g2[1]["wealth"]}, '
            f'h {hw1}->{nat_g2[0]["wealth"]}, gifts={hdr_g2.get("gifts")} spice={hdr_g2.get("spice")})',
        )

        # Find capital city indices and remaining NPC city indices
        cities, hv, _ = civ_state(host)
        host_cap_ci = min(cities.keys(), key=lambda ci: dist3(cities[ci]['pos'], host_cap))
        guest_cap_ci = min(cities.keys(), key=lambda ci: dist3(cities[ci]['pos'], guest_cap))
        npc_city_cis = [ci for ci in sorted(cities.keys()) if ci not in (host_cap_ci, guest_cap_ci)]
        print(f'  cities: host_cap={host_cap_ci} guest_cap={guest_cap_ci} npc_cities={npc_city_cis}', flush=True)
        ck(
            host_cap_ci != guest_cap_ci and len(npc_city_cis) >= 2,
            f'identified host_cap={host_cap_ci}, guest_cap={guest_cap_ci}, npc_cities={npc_city_cis}',
        )
        if len(npc_city_cis) < 2:
            return ck.result()

        # Ensure both host and guest have a live vehicle for PvP tests
        h_vehs = [vid for vid, v in hv.items() if v['owner'] == 0 and v['hp'] > 20]
        g_vehs = [vid for vid, v in hv.items() if v['owner'] == 1 and v['hp'] > 20]
        if not h_vehs:
            print('  spawn host veh:', host.call(f'coop.civ spawnveh {host_cap_ci} 0 0 force').get('out', '').strip())
            time.sleep(1.5)
        if not g_vehs:
            print('  spawn guest veh:', host.call(f'coop.civ spawnveh {guest_cap_ci} 0 0 force').get('out', '').strip())
            time.sleep(1.5)
        _, hv, _ = civ_state(host)
        h_vehs = [vid for vid, v in hv.items() if v['owner'] == 0 and v['hp'] > 20]
        g_vehs = [vid for vid, v in hv.items() if v['owner'] == 1 and v['hp'] > 20]
        ck(bool(h_vehs) and bool(g_vehs), f'both players have live vehicles (host={h_vehs}, guest={g_vehs})')
        if not (h_vehs and g_vehs):
            return ck.result()
        h_vid, g_vid = h_vehs[0], g_vehs[0]
        host.call(f'coop.civ vhp {h_vid} 400')
        host.call(f'coop.civ vhp {g_vid} 400')
        time.sleep(0.5)

        # While Allied: vehicle PvP damage must be blocked
        pvp_ally = host.call(f'coop.civ rivals pvp_test {h_vid} {g_vid} 50').get('out') or ''
        m_pa = re.search(r'hp ([\d.]+) -> ([\d.]+) allowed=(\d+) blocked=(\d+)', pvp_ally)
        ck(
            bool(m_pa) and abs(float(m_pa.group(1)) - float(m_pa.group(2))) < 0.1 and int(m_pa.group(4)) >= 1,
            f'Allied vehicle PvP hit blocked ({pvp_ally.strip()})',
        )

        # 6. Break Alliance -> War: vehicle PvP allowed, ordinary city capture works, capital & superweapons blocked
        host.call('coop.rivals break 1')
        at_war = wait_for(lambda: '0-1=War' in rivals_out(host) and '0-1=War' in rivals_out(guest), 8.0)
        ck(bool(at_war), 'Alliance break countdown transitioned both host and guest to War (0-1=War)')

        # At War: vehicle PvP damage must succeed
        pvp_war = host.call(f'coop.civ rivals pvp_test {h_vid} {g_vid} 50').get('out') or ''
        m_pw = re.search(r'hp ([\d.]+) -> ([\d.]+) allowed=(\d+) blocked=(\d+)', pvp_war)
        ck(
            bool(m_pw) and float(m_pw.group(2)) < float(m_pw.group(1)) - 20.0 and int(m_pw.group(3)) >= 1,
            f'War vehicle PvP hit dealt damage ({pvp_war.strip()})',
        )

        # Guest captures ordinary NPC city npc_city_cis[0]
        npc_ci_a, npc_ci_b = npc_city_cis[0], npc_city_cis[1]
        cap_g = host.call(f'coop.civ rivals capture_test {npc_ci_a} 1').get('out') or ''
        ck(
            f'-> 0x{guest_pol:08x}' in cap_g.lower(),
            f'Guest captured ordinary NPC city {npc_ci_a} ({cap_g.strip()})',
        )
        time.sleep(0.8)

        # Host captures Guest's ordinary city npc_ci_a at War -> owner changes from Guest to Host
        cap_war = host.call(f'coop.civ rivals capture_test {npc_ci_a} 0').get('out') or ''
        h_hdr_w = parse_cr(cr_out(host))[0]
        ck(
            f'0x{guest_pol:08x} -> 0x{host_pol:08x}' in cap_war.lower()
            and int(h_hdr_w.get('war_captures', '0')) >= 1,
            f'War capture of rival ordinary city {npc_ci_a} succeeded ({cap_war.strip()}, '
            f'war_captures={h_hdr_w.get("war_captures")})',
        )

        # R7 Capital Inviolability: capture, conversion, targeted superweapon, global superweapon ALL BLOCKED
        cap_cap = host.call(f'coop.civ rivals capture_test {guest_cap_ci} 0').get('out') or ''
        ck(
            f'0x{guest_pol:08x} -> 0x{guest_pol:08x}' in cap_cap.lower()
            and int(parse_cr(cr_out(host))[0].get('cap_blocked', '0')) >= 1,
            f'R7 rival capital capture blocked ({cap_cap.strip()})',
        )

        conv_cap = host.call(f'coop.civ rivals convert_test {guest_cap_ci} 0').get('out') or ''
        ck(
            'blocked=1' in conv_cap and int(parse_cr(cr_out(host))[0].get('conv_blocked', '0')) >= 1,
            f'R7 rival capital religious conversion blocked ({conv_cap.strip()})',
        )

        sw_icbm = host.call(f'coop.civ rivals sw_test 4 {guest_cap_ci} 0').get('out') or ''
        ck(
            'blocked=1' in sw_icbm and 'can_target=0' in sw_icbm,
            f'R7 targeted superweapon (ICBM) at rival capital blocked ({sw_icbm.strip()})',
        )

        sw_glob = host.call(f'coop.civ rivals sw_test 2 {guest_cap_ci} 0').get('out') or ''
        ck(
            'blocked=1' in sw_glob and int(parse_cr(cr_out(host))[0].get('sw_blocked', '0')) >= 2,
            f'R7 global superweapon (Fanatical Frenzy) blocked while rival capital exists ({sw_glob.strip()})',
        )

        sw_guest = guest.call(f'coop.civ rivals sw_test 4 {host_cap_ci} 1').get('out') or ''
        ck(
            'blocked=1' in sw_guest and 'can_target=0' in sw_guest,
            f'R7 guest superweapon targeting host capital blocked ({sw_guest.strip()})',
        )

        # 7. Stage End (>50% NPC cities) -> Catch-up -> Civ Draft
        # Host owns 1 of 2 non-capital cities (50%, NOT >50%): must NOT have won yet
        time.sleep(1.5)
        r_pre = rivals_out(host)
        ck(
            'winner=255' in r_pre and 'catchup=0' in r_pre,
            f'50% (1/2) non-capital cities does not trigger win yet ({r_pre.splitlines()[3] if len(r_pre.splitlines()) > 3 else ""})',
        )

        # Host captures 2nd NPC city (2/2 > 50%) -> triggers stage win & catch-up timer
        cap_win = host.call(f'coop.civ rivals capture_test {npc_ci_b} 0').get('out') or ''
        ck(
            f'-> 0x{host_pol:08x}' in cap_win.lower(),
            f'Host captured 2nd NPC city {npc_ci_b} ({cap_win.strip()})',
        )

        won_sync = wait_for(
            lambda: (
                'winner=0' in rivals_out(host)
                and 'winner=0' in rivals_out(guest)
                and ('catchup=1' in rivals_out(guest) or 'done=1' in rivals_out(guest))
            ),
            10.0,
        )
        ck(bool(won_sync), 'Host >50% NPC cities triggered stage win (winner=0) synced to guest')

        # Wait for fast catch-up timer (3 s) to expire -> StageHooks::on_stage_race_end starts Civ Draft
        draft_ready = wait_for(
            lambda: (
                parse_cr(cr_out(host))[0].get('race_ended') == '1'
                and parse_cr(cr_out(host))[0].get('draft_started') == '1'
                and parse_cr(cr_out(host))[0].get('transition_ready') == '1'
                and 'draft: active=1 winner=0' in rivals_out(guest)
            ),
            12.0,
        )
        h_hdr_d, _, _, h_items = parse_cr(cr_out(host))
        _, _, _, g_items = parse_cr(cr_out(guest))
        ck(
            bool(draft_ready) and len(g_items) >= 1,
            f'Catch-up expired -> Civ Draft started & transition_ready=1 '
            f'(race_ended={h_hdr_d.get("race_ended")} draft_started={h_hdr_d.get("draft_started")} '
            f'transition_ready={h_hdr_d.get("transition_ready")} guest_items={[hex(x) for x in g_items]})',
        )
        if not draft_ready or not g_items:
            return ck.result()

        # Guest picks the first Civ draft item -> payout=100 synced & draft_applied >= 1 on guest
        pick_id = g_items[0]
        guest.call(f'coop.rivals draft pick 0x{pick_id:08X}')
        pick_ok = wait_for(
            lambda: (
                'total_picks=1 payout=100' in rivals_out(host)
                and 'total_picks=1 payout=100' in rivals_out(guest)
                and int(parse_cr(cr_out(guest))[0].get('draft_applied', '0')) >= 1
            ),
            10.0,
        )
        ck(
            bool(pick_ok),
            f'Civ draft pick 0x{pick_id:08X} synced (total_picks=1 payout=100) and applied on guest '
            f'(draft_applied={parse_cr(cr_out(guest))[0].get("draft_applied")})',
        )

        host.call('coop.rivals fasttimers 0')
    finally:
        try:
            host.call('coop.rivals fasttimers 0')
        except Exception:
            pass
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
