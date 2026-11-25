#!/usr/bin/env python3
"""WP-R4 (BUG-1548): Tribe Rivals end-to-end two-game Wine lab test.

Exercises:
  1. Enable Tribe Rivals (`coop.rivals mode tribe rivals`) + ensure >= 2 remaining NPC tribes.
  2. R2 NPC cTribe takeover on host: distinct tribe assigned to guest, planner AI disabled
     (`planner_disabled=1`, `planner_skipped > 0`), species slot & members swapped.
  3. R1 Guest perspective swap: guest's player tribe swaps to taken-over tribe (`swapped=1`),
     distinct from host's tribe, own food independent from host's food.
  4. R4 Per-player NPC relations + Tribe influence: earn influence on music & gift, buy influence
     charging own Tribe food, Protect blocks rival TurnAgainst, TurnAgainst drops rival relation
     by 1 step (synced via SK_NPC_RELS), SmoothOver raises own relation by 1 step.
  5. War raids, member PvP & R7 Main Village Inviolability: Neutral blocks member & hut damage;
     War allows member PvP, extra tool hut damage, and food raid, while main hut massive hit is
     clamped above floor (`conquered=0`, `clamped >= 1`).
  6. Alliance food & tool gifts: Peace -> Ally -> host gifts 25 food and tool 9 to guest.
  7. Stage End (>50% NPC tribes) -> catch-up timer -> Tribe Draft: host allies >50% NPC tribes,
     auto-triggers stage win -> fast catch-up expires -> Tribe Draft starts -> guest copies tool
     with escalating payout.
  8. Clean leave restores guest perspective (`swapped=0`) with 0 faults.
  9. Twin (`SPORE_FIX_OFF=rivals.tribe`): Tribe Rivals disabled -> FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_rivals_tribe.py
"""
import re
import time

import twoinst
from twoinst import Checks, start


def tr_out(con):
    return con.call('coop.tribe rivals').get('out') or ''


def rv_out(con):
    return con.call('coop.rivals').get('out') or ''


def wait_for(fn, timeout=14.0, step=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


def parse_int(text, key):
    m = re.search(rf'\b{re.escape(key)}=(-?\d+)', text)
    return int(m.group(1)) if m else None


def parse_assign(text, pid):
    m = re.search(
        rf'assign pid={pid} pol=([0-9A-Fa-f]+) ptr=(\S+) food=(-?\d+) members=(\d+) tools=0x([0-9A-Fa-f]+)',
        text,
    )
    if not m:
        return None
    return {
        'pol': m.group(1).upper(),
        'ptr': m.group(2),
        'food': int(m.group(3)),
        'members': int(m.group(4)),
        'tools': int(m.group(5), 16),
    }


def parse_npcs(text):
    out = []
    for m in re.finditer(r'npc pol=([0-9A-Fa-f]+) pos=\([^)]+\) rels=\[p0=(\w+),p1=(\w+)\]', text):
        out.append({'pol': m.group(1).upper(), 'p0': m.group(2), 'p1': m.group(3)})
    return out


def main():
    twoinst.forget_settings()
    host, guest = start('tribe')
    ck = Checks()
    try:
        time.sleep(2.0)

        # 1. Enable Tribe Rivals + fast timers
        host.call('coop.rivals fasttimers 1')
        host.call('coop.rivals mode tribe rivals')

        # Wait for host R2 takeover of an NPC tribe for guest (pid=1)
        h_asgn = wait_for(lambda: tr_out(host) if 'active=1' in tr_out(host) and 'assign pid=1 ' in tr_out(host) else None, 15.0)
        ck(bool(h_asgn), f'host Tribe Rivals active and took over NPC cTribe for guest ({(h_asgn or tr_out(host)).splitlines()[0]})')
        if not h_asgn:
            return ck.result()

        # Form extra NPC tribes on the host so >= 2 free NPC tribes remain after guest takeover
        for _ in range(3):
            cur_npcs = parse_npcs(tr_out(host))
            if len(cur_npcs) >= 2:
                break
            host.call('coop.tribe rivals form_npc')
            time.sleep(1.2)
        h_st = tr_out(host)
        npcs = parse_npcs(h_st)
        ck(len(npcs) >= 2, f'host has >= 2 remaining NPC tribes for diplomacy & race (got {len(npcs)})')
        if len(npcs) < 2:
            return ck.result()

        # 2. Verify R2: distinct pol, planner AI disabled (planner_skipped > 0), species slot swapped
        a0 = parse_assign(h_st, 0)
        a1 = parse_assign(h_st, 1)
        ck(bool(a0 and a1 and a0['pol'] != a1['pol']), f'host & guest own distinct cTribes (p0={a0} p1={a1})')

        ps_ok = wait_for(lambda: (parse_int(tr_out(host), 'planner_skipped') or 0) > 0, 8.0)
        ck(bool(ps_ok) and 'planner_disabled=1' in tr_out(host),
           f'R2 taken-over NPC tribe planner AI disabled (planner_skipped={parse_int(tr_out(host), "planner_skipped")})')

        sw_out = host.call('coop.tribe rivals swap_species 1').get('out') or ''
        ck('ok:' in sw_out and (parse_int(tr_out(host), 'species_swaps') or 0) >= 1,
           f'R2 taken-over tribe species slot & members swapped ({sw_out.strip()})')

        # 3. Verify R1: Guest perspective swapped to taken-over tribe & own food independent from host
        g_sw = wait_for(lambda: tr_out(guest) if 'active=1 swapped=1' in tr_out(guest) else None, 12.0)
        ck(bool(g_sw), f'R1 guest perspective swapped to own tribe ({(g_sw or tr_out(guest)).splitlines()[0]})')
        if g_sw:
            g_pol_m = re.search(r'my_pol=([0-9A-Fa-f]+)', g_sw)
            g_pol = g_pol_m.group(1).upper() if g_pol_m else ''
            ck(bool(a1) and g_pol == a1['pol'] and g_pol != a0['pol'],
               f'R1 guest GetPlayerTribe pol ({g_pol}) matches guest tribe ({a1["pol"]}), not host ({a0["pol"]})')

        # Own food independence: add different food to host and guest
        host.call('coop.tribe rivals addfood 0 80')
        host.call('coop.tribe rivals addfood 1 35')
        h_f0 = parse_assign(tr_out(host), 0)['food']
        h_f1 = parse_assign(tr_out(host), 1)['food']
        g_food_sync = wait_for(lambda: parse_int(tr_out(guest), 'my_food') == h_f1, 8.0)
        ck(bool(g_food_sync) and h_f0 != h_f1,
           f'R1 own food independent & synced (host={h_f0}, guest_on_host={h_f1}, guest_local={parse_int(tr_out(guest), "my_food")})')

        # 4. R4 Per-player NPC relations + Tribe influence (music/gift earn, food-funded buy, Protect/Turn/Smooth)
        npc_a = '0x' + npcs[0]['pol']
        npc_b = '0x' + npcs[1]['pol']

        m_out = host.call(f'coop.tribe rivals music_win 0 {npc_a}').get('out') or ''
        g_out = host.call(f'coop.tribe rivals gift_win 1 {npc_a}').get('out') or ''
        ck('ok:' in m_out and 'ok:' in g_out and (parse_int(tr_out(host), 'inf_earned') or 0) >= 2,
           f'R4 Tribe influence earned on music & gift ({m_out.strip()} | {g_out.strip()})')

        # Buy influence charges own Tribe food (price=50)
        host.call('coop.tribe rivals addfood 0 100')
        f0_before = parse_assign(tr_out(host), 0)['food']
        buy_out = host.call('coop.rivals influence buy').get('out') or ''
        f0_after = parse_assign(tr_out(host), 0)['food']
        ck('ok' in buy_out and f0_before - f0_after == 50,
           f'R4 buy influence charged host own Tribe food ({f0_before} -> {f0_after}, {buy_out.strip()})')

        # Top up influence & test Protect, TurnAgainst (blocked vs allowed), SmoothOver
        host.call('coop.rivals influence add 0 100')
        host.call('coop.rivals influence add 1 100')
        wait_for(lambda: 'bal=[' in rv_out(guest), 6.0)
        time.sleep(0.5)

        guest.call(f'coop.rivals influence protect {npc_a}')
        prot_ok = wait_for(lambda: f'npc pol={npcs[0]["pol"]}' in tr_out(host) and 'prot=[p0=0,p1=1]' in tr_out(host), 8.0)
        ck(bool(prot_ok), 'R4 guest Protect on npcA synced on host')

        turn_blk = host.call(f'coop.rivals influence turn {npc_a} 1').get('out') or ''
        ck('error:' in turn_blk and 'protected' in turn_blk,
           f'R4 TurnAgainst on protected NPC tribe blocked ({turn_blk.strip()})')

        # TurnAgainst on unprotected npcB drops guest relation Neutral -> Annoyed while host stays Neutral
        host.call(f'coop.tribe rivals setrel 0 {npc_b} 2')
        host.call(f'coop.tribe rivals setrel 1 {npc_b} 2')
        turn_ok = host.call(f'coop.rivals influence turn {npc_b} 1').get('out') or ''
        b_host = [n for n in parse_npcs(tr_out(host)) if n['pol'] == npcs[1]['pol']]
        g_annoyed = wait_for(lambda: f'npc pol={npcs[1]["pol"]}' in tr_out(guest) and 'rel=Annoyed' in tr_out(guest), 8.0)
        ck('ok' in turn_ok and bool(b_host) and b_host[0]['p0'] == 'Neutral' and b_host[0]['p1'] == 'Annoyed' and bool(g_annoyed),
           f'R4 TurnAgainst dropped guest relation to Annoyed while host stayed Neutral ({b_host})')

        # SmoothOver raises guest relation back Annoyed -> Neutral
        guest.call(f'coop.rivals influence smooth {npc_b}')
        g_smooth = wait_for(lambda: f'npc pol={npcs[1]["pol"]}' in tr_out(guest) and 'rel=Neutral' in tr_out(guest), 8.0)
        ck(bool(g_smooth), 'R4 SmoothOver raised guest NPC relation back to Neutral')

        # 5. Neutral block -> War PvP, extra tool hut damage, food raid, & R7 Main Village Inviolability
        blk_mem = host.call('coop.tribe rivals hit_member 0 1 25').get('out') or ''
        blk_hut = host.call('coop.tribe rivals hit_hut 0 1 50').get('out') or ''
        ck((parse_int(tr_out(host), 'pvp_blocked') or 0) >= 2 and 'pvp_hits=0' in blk_mem,
           f'Neutral blocks rival member & hut damage ({blk_mem.strip()} | {blk_hut.strip()})')

        host.call('coop.rivals war 1')
        war_ok = wait_for(lambda: '0-1=War' in rv_out(host) and '0-1=War' in rv_out(guest), 8.0)
        ck(bool(war_ok), 'players transitioned to War')

        pvp_m = host.call('coop.tribe rivals hit_member 0 1 15').get('out') or ''
        m_hp = re.search(r'member hp ([0-9.]+) -> ([0-9.]+)', pvp_m)
        ck(bool(m_hp) and float(m_hp.group(2)) < float(m_hp.group(1)) and (parse_int(tr_out(host), 'pvp_hits') or 0) >= 1,
           f'War member PvP dealt damage ({pvp_m.strip()})')

        pvp_t = host.call('coop.tribe rivals hit_tool 0 1 20').get('out') or ''
        t_hp = re.search(r'tool hp ([0-9.]+) -> ([0-9.]+)', pvp_t)
        ck(bool(t_hp) and float(t_hp.group(2)) < float(t_hp.group(1)) and (parse_int(tr_out(host), 'tool_pvp') or 0) >= 1,
           f'War extra tool hut raid dealt damage ({pvp_t.strip()})')

        host.call('coop.tribe rivals addfood 1 40')
        f0_r0 = parse_assign(tr_out(host), 0)['food']
        f1_r0 = parse_assign(tr_out(host), 1)['food']
        raid_out = host.call('coop.tribe rivals raid 1 15').get('out') or ''
        f0_r1 = parse_assign(tr_out(host), 0)['food']
        f1_r1 = parse_assign(tr_out(host), 1)['food']
        ck('ok:' in raid_out and f0_r1 == f0_r0 + 15 and f1_r1 == f1_r0 - 15,
           f'War food raid transferred 15 food ({f1_r0}->{f1_r1}, {f0_r0}->{f0_r1})')

        # R7 Main Village Inviolability: huge blow on rival's main hut cannot destroy or conquer it
        pvp_h = host.call('coop.tribe rivals hit_hut 0 1 99999').get('out') or ''
        h_hp = re.search(r'hut hp ([0-9.]+) -> ([0-9.]+)', pvp_h)
        ck(bool(h_hp) and float(h_hp.group(2)) >= 1.0 and 'conquered=0' in pvp_h
           and (parse_int(tr_out(host), 'hut_clamped') or 0) >= 1,
           f'R7 main village inviolable under massive war hit ({pvp_h.strip()})')

        # 6. Peace -> Alliance -> Food & Tool gifts
        guest.call('coop.rivals peace 0')
        wait_for(lambda: '1->0:peace' in rv_out(host), 8.0)
        host.call('coop.rivals accept 1')
        wait_for(lambda: '0-1=Neutral' in rv_out(host), 8.0)
        host.call('coop.rivals ally 1')
        wait_for(lambda: '0->1:ally' in rv_out(guest), 8.0)
        guest.call('coop.rivals accept 0')
        ally_ok = wait_for(lambda: '0-1=Ally' in rv_out(host) and '0-1=Ally' in rv_out(guest), 8.0)
        ck(bool(ally_ok), 'players formed Alliance')

        host.call('coop.tribe rivals addfood 0 60')
        host.call('coop.tribe rivals unlock_tool 0 9')
        f0_g0 = parse_assign(tr_out(host), 0)['food']
        f1_g0 = parse_assign(tr_out(host), 1)['food']
        gf_out = host.call('coop.tribe rivals gift food 1 25').get('out') or ''
        gt_out = host.call('coop.tribe rivals gift tool 1 9').get('out') or ''
        f0_g1 = parse_assign(tr_out(host), 0)['food']
        f1_g1 = parse_assign(tr_out(host), 1)['food']
        g_gift_sync = wait_for(lambda: parse_int(tr_out(guest), 'my_food') == f1_g1, 8.0)
        ck('ok:' in gf_out and 'ok:' in gt_out and f0_g1 == f0_g0 - 25 and f1_g1 == f1_g0 + 25 and bool(g_gift_sync),
           f'Alliance food & tool gifts applied and synced (host {f0_g0}->{f0_g1}, guest {f1_g0}->{f1_g1})')

        # 7. Stage End (>50% NPC tribes) -> catch-up timer -> Tribe Draft -> tool copy & payout
        host.call('coop.tribe rivals unlock_tool 0 1')
        host.call('coop.tribe rivals unlock_tool 0 4')
        cur_npcs = parse_npcs(tr_out(host))
        win_need = len(cur_npcs) // 2 + 1
        for i in range(win_need):
            host.call(f'coop.tribe rivals setrel 0 0x{cur_npcs[i]["pol"]} 4')

        win_det = wait_for(lambda: 'winner=0' in rv_out(host) and 'winner=0' in rv_out(guest), 10.0)
        ck(bool(win_det), f'Stage End (>50% NPC tribes = {win_need}/{len(cur_npcs)}) triggered winner=0')

        # Wait for fast catch-up timer (3 s) to end and on_stage_race_end to launch Tribe Draft
        draft_up = wait_for(lambda: 'draft: active=1 winner=0' in rv_out(host) and 'draft: active=1 winner=0' in rv_out(guest), 12.0)
        ck(bool(draft_up), 'catch-up timer expired and launched Tribe Draft automatically')

        if draft_up:
            guest.call('coop.rivals draft pick 1')
            pick_ok = wait_for(
                lambda: 'total_picks=1 payout=100' in rv_out(host)
                and (parse_int(tr_out(guest), 'draft_unlocked') or 0) >= 1,
                8.0,
            )
            ck(bool(pick_ok), 'guest copied Tribe draft tool with winner payout=100 and local tool unlock')

        # 8. Clean session leave restores guest perspective without crashing
        guest.call('coop.leave')
        restored = wait_for(lambda: 'swapped=0' in tr_out(guest), 10.0)
        ck(bool(restored), 'guest perspective cleanly restored on session leave (swapped=0)')

        host.call('coop.rivals fasttimers 0')
    finally:
        pass
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
