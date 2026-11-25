#!/usr/bin/env python3
"""WP-R2 (BUG-1546): Cell stage Rivals mode (docs/11-rivals.md 11.3.1, 11.5, 11.6).

Cell fixture, two players:
  1. Host enables Cell=Rivals (`coop.rivals mode cell rivals`) and fast timers (`coop.rivals fasttimers 1`).
     Verify both host and guest activate Cell Rivals (`cell_rivals: fix=1 active=1`) and Cell rejects NPC influence.
  2. Per-player evolution bar & DNA: host and guest gain different food amounts independently; neither overwrites the
     other's Cell bar or DNA; both appear on the host's Cell Rivals status and on the guest's F9 Race UI page.
  3. Visibility: rival cell name tag on screen (`coop.find labels`) and off-screen edge arrow (`coop.find`).
  4. War-gated PvP & no KO/revive:
     - Neutral (default): spike contact and `coop.pool rivals hit` deal 0 damage (blocked).
     - War (`coop.rivals war 1`): host spike contact lands real war PvP hit on guest (`pvp_out`/`pvp_in`/`damage_taken`).
     - Lethal war hit kills guest WITHOUT knockout/revive (`downed=0`, no `ko[1]=1`), drops physical meat chunks at the
       victim's position on the host (`meat_drops >= 1`, `meat_chunks >= 3`), and guest respawns normally as in vanilla.
  5. Stage end, catch-up timer & Cell part draft:
     - Host unlocks two Cell parts that guest lacks; verify `parts.shared_cell` does NOT auto-share them in Cell Rivals.
     - Host reaches full Cell bar (food >= 1000) -> host automatically wins (`winner=0`) and starts catch-up timer.
     - Catch-up timer expires -> Cell part draft starts automatically offering the winner's parts.
     - Guest copies two parts -> `SK_DRAFT_APPLY` natively unlocks both parts on guest (`coop.parts cell`), and winner
       accumulates escalating next-stage DNA payout (100 + 150 = 250) claimed via `take_pending_draft_payout`.
  6. SPORE_FIX_OFF=rivals.cell (`rivals_cell_off`): Cell Rivals disabled -> FAIL cleanly.

  SPORE_LANE=2 python3 tools/tests/coop_rivals_cell.py
"""
import re
import time

import coop_ui
from harness import num, pool
import twoinst
from twoinst import Checks, start


def wait_for(fn, timeout=12.0, step=0.25):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


def rivals_out(con):
    return con.call('coop.rivals').get('out') or ''


def cell_rivals_stats(con):
    out = con.call('coop.pool rivals').get('out') or ''
    def inum(pat):
        m = re.search(pat, out)
        return int(m.group(1)) if m else None
    return dict(
        fix=inum(r'cell_rivals:.*\bfix=(\d+)'),
        active=inum(r'cell_rivals:.*\bactive=(\d+)'),
        food=inum(r'cell_rivals:.*\bfood=(-?\d+)'),
        dna=inum(r'cell_rivals:.*\bdna=(-?\d+)'),
        pvp_out=inum(r'cell_rivals:.*\bpvp_out=(\d+)'),
        pvp_in=inum(r'cell_rivals:.*\bpvp_in=(\d+)'),
        pvp_blocked=inum(r'cell_rivals:.*\bpvp_blocked=(\d+)'),
        pvp_kills=inum(r'cell_rivals:.*\bpvp_kills=(\d+)'),
        meat_drops=inum(r'cell_rivals:.*\bmeat_drops=(\d+)'),
        meat_chunks=inum(r'cell_rivals:.*\bmeat_chunks=(\d+)'),
        draft_starts=inum(r'cell_rivals:.*\bdraft_starts=(\d+)'),
        draft_unlocks_applied=inum(r'cell_rivals:.*\bdraft_unlocks=\d+/(\d+)'),
        queued_dna=inum(r'cell_rivals:.*\bqueued_dna=(-?\d+)'),
        raw=out,
    )


def pool_nums(con):
    out = pool(con)
    def inum(pat):
        m = re.search(pat, out)
        return int(m.group(1)) if m else None
    return dict(
        food=inum(r'cell\([^)]+\): food=(-?\d+)'),
        hp=inum(r'\bavatar hp=(-?\d+)'),
        dmg=inum(r'\bdamage_taken=(\d+)'),
        downed=inum(r'\bdowned=(\d+)'),
        dying=inum(r'\bdying=(\d+)'),
        dead=inum(r'\bdead=(\d+)'),
        mirrors=inum(r'\bmirrors=(\d+)'),
        raw=out,
    )


def cell_parts_str(con):
    out = con.call('coop.parts cell').get('out') or ''
    m = re.search(r'cell parts=([0-9]{13})', out)
    return m.group(1) if m else ''


def host_sees_ko(host):
    return bool(re.search(r'^ko\[1\]=1', pool(host), re.MULTILINE))


def alive_again(con):
    st = pool_nums(con)
    return st['downed'] == 0 and st['dying'] == 0 and st['dead'] == 0 and (st['hp'] or 0) > 0


def main():
    twoinst.forget_settings()
    host, guest = start('cell')
    ck = Checks()
    try:
        ck(bool(wait_for(lambda: (pool_nums(guest)['mirrors'] or 0) > 5, 30.0)),
           'guest mirrors host Cell pool')

        # 1. Activate Cell Rivals mode + fast timers
        host.call('coop.rivals preset all_together')
        host.call('coop.rivals mode cell rivals')
        host.call('coop.rivals fasttimers 1')

        both_active = wait_for(
            lambda: cell_rivals_stats(host)['active'] == 1 and cell_rivals_stats(guest)['active'] == 1,
            10.0,
        )
        hs0, gs0 = cell_rivals_stats(host), cell_rivals_stats(guest)
        print('  host cell_rivals:', hs0['raw'].splitlines()[0] if hs0['raw'] else '', flush=True)
        print('  guest cell_rivals:', gs0['raw'].splitlines()[0] if gs0['raw'] else '', flush=True)
        ck(bool(both_active), f'Cell Rivals active on host and guest (host={hs0["active"]}, guest={gs0["active"]})')
        if not both_active:
            return ck.result()

        # Verify no NPC influence in Cell stage (11.3.1)
        inf_buy = host.call('coop.rivals influence buy').get('out') or ''
        ck('error:' in inf_buy, f'influence buy rejected in Cell stage ({inf_buy.strip()})')

        # 2. Per-player evolution bar & DNA (unpooled)
        hf0 = pool_nums(host)['food'] or 0
        gf0 = pool_nums(guest)['food'] or 0
        host.call('coop.pool foodlocal 150')
        time.sleep(2.2)  # several MSG_CELL_FOOD cycles (0.5 s)
        hf1 = pool_nums(host)['food'] or 0
        gf1 = pool_nums(guest)['food'] or 0
        print(f'  separate bars after host +150: host {hf0}->{hf1}, guest {gf0}->{gf1}', flush=True)
        ck(hf1 >= hf0 + 120, f'host own Cell bar increased ({hf0}->{hf1})')
        ck(abs(gf1 - gf0) < 40, f'guest Cell bar stayed independent of host ({gf0}->{gf1})')

        guest.call('coop.pool foodlocal 60')
        time.sleep(2.2)
        hf2 = pool_nums(host)['food'] or 0
        gf2 = pool_nums(guest)['food'] or 0
        print(f'  separate bars after guest +60: host {hf1}->{hf2}, guest {gf1}->{gf2}', flush=True)
        ck(gf2 >= gf1 + 45, f'guest own Cell bar increased ({gf1}->{gf2})')
        ck(abs(hf2 - hf1) < 40, f'host Cell bar stayed independent of guest ({hf1}->{hf2})')

        # Host reports both players' live Cell progress; guest F9 Race page shows Cell Rivals progress rows
        prog_ok = wait_for(
            lambda: ('p0: food=' in cell_rivals_stats(host)['raw'] and 'p1: food=' in cell_rivals_stats(host)['raw']),
            8.0,
        )
        ck(bool(prog_ok), 'host tracks both players live Cell progress')

        guest.call('coop.uistate 1')
        gui = coop_ui.Ui(guest, 'guest')
        clicked = wait_for(lambda: gui.click('rivals'), 8.0, 0.4)
        ui_p1 = wait_for(
            lambda: guest.call('coop.uistate').get('out')
            if 'page=rivals' in (guest.call('coop.uistate').get('out') or '') else None,
            8.0,
        )
        ui_p2 = ui_p1 or ''
        if ui_p1 and 'Cell Rivals' not in ui_p1:
            if gui.click('rnext'):
                ui_p2 = wait_for(
                    lambda: guest.call('coop.uistate').get('out')
                    if 'Cell Rivals' in (guest.call('coop.uistate').get('out') or '') else None,
                    6.0,
                ) or (guest.call('coop.uistate').get('out') or '')
        guest.call('coop.uistate 0')
        ck(bool(clicked) and bool(ui_p1) and 'Race progress (Cell)' in (ui_p1 or '')
           and 'Cell Rivals' in ui_p2 and 'bar ' in ui_p2,
           'guest F9 Race page shows Cell Rivals progress rows')

        # 3. Visibility: rival name tag on screen and edge arrow when off-screen
        guest.call('coop.pool swimto partner tp')
        time.sleep(1.0)
        lbl_out = wait_for(
            lambda: host.call('coop.find labels').get('out')
            if 'label kind=player' in (host.call('coop.find labels').get('out') or '') else None,
            6.0,
        )
        ck(bool(lbl_out), 'rival cell name tag visible on screen')

        guest.call('coop.pool tp 90 0')
        time.sleep(1.0)
        arr_out = wait_for(
            lambda: host.call('coop.find').get('out')
            if re.search(r'arrows=[1-9]\d*\s+on=1', host.call('coop.find').get('out') or '') else None,
            6.0,
        )
        ck(bool(arr_out), 'off-screen edge arrow visible for distant rival cell')

        # 4. War-gated PvP: Neutral blocked vs War deals real damage + kill meat drop + NO KO/revive
        guest.call('coop.pool swimto partner tp')
        time.sleep(1.0)
        hb = host.call('coop.pool borrow spike graft').get('out') or ''
        ck(hb.startswith('borrowed'), f'host borrowed spike part ({hb.strip()[:48]})')

        # 4a. Neutral: contact + direct war hit must deal 0 damage
        g_dmg0 = pool_nums(guest)['dmg'] or 0
        h_pvp0 = cell_rivals_stats(host)['pvp_out'] or 0
        for _ in range(4):
            host.call('coop.pool swimto partner tp')
            time.sleep(0.75)
        blocked_out = host.call('coop.pool rivals hit 1 2').get('out') or ''
        h_pvp1 = cell_rivals_stats(host)['pvp_out'] or 0
        g_dmg1 = pool_nums(guest)['dmg'] or 0
        print(f'  Neutral PvP: host pvp_out {h_pvp0}->{h_pvp1}, guest dmg {g_dmg0}->{g_dmg1}, hit={blocked_out.strip()}', flush=True)
        ck('blocked' in blocked_out and h_pvp1 == h_pvp0 and g_dmg1 == g_dmg0,
           f'Neutral blocks Cell PvP (pvp_out +{h_pvp1 - h_pvp0}, guest dmg +{g_dmg1 - g_dmg0})')

        # 4b. War: real spike contact lands war PvP hit and deals real damage to guest
        host.call('coop.rivals war 1')
        war_ok = wait_for(lambda: '0-1=War' in rivals_out(host) and '0-1=War' in rivals_out(guest), 8.0)
        ck(bool(war_ok), 'War declared and synced (0-1=War)')

        h_w0 = cell_rivals_stats(host)
        g_w0 = cell_rivals_stats(guest)
        g_dmg_w0 = pool_nums(guest)['dmg'] or 0
        t0 = time.time()
        while time.time() - t0 < 12.0:
            host.call('coop.pool swimto partner tp')
            time.sleep(0.5)
            hc = cell_rivals_stats(host)
            gc = cell_rivals_stats(guest)
            if (hc['pvp_out'] or 0) > (h_w0['pvp_out'] or 0) and (gc['pvp_in'] or 0) > (g_w0['pvp_in'] or 0):
                break
        h_w1 = cell_rivals_stats(host)
        g_w1 = cell_rivals_stats(guest)
        g_dmg_w1 = pool_nums(guest)['dmg'] or 0
        print(f'  War PvP contact: host pvp_out +{(h_w1["pvp_out"] or 0) - (h_w0["pvp_out"] or 0)}, '
              f'guest pvp_in +{(g_w1["pvp_in"] or 0) - (g_w0["pvp_in"] or 0)}, dmg +{g_dmg_w1 - g_dmg_w0}', flush=True)
        ck((h_w1['pvp_out'] or 0) > (h_w0['pvp_out'] or 0) and (g_w1['pvp_in'] or 0) > (g_w0['pvp_in'] or 0)
           and g_dmg_w1 > g_dmg_w0,
           'At War: real spike contact dealt war PvP damage to rival')

        # 4c. Kill rival at war: NO knockout/revive, physical meat dropped on host, vanilla respawn
        host.call('coop.pool borrow off')
        if not alive_again(guest):
            wait_for(lambda: alive_again(guest), 45.0, 1.0)
            time.sleep(4.0)  # clear post-respawn invuln
        guest.call('coop.pool swimto partner tp')
        time.sleep(1.0)

        h_k0 = cell_rivals_stats(host)
        g_deaths0 = num(guest.call('coop.pool deaths').get('out') or '', 'deaths') or 0
        hit_kill = host.call('coop.pool rivals hit 1 50').get('out') or ''
        seen_down = False
        seen_ko = False
        died = False
        t0 = time.time()
        while time.time() - t0 < 6.0:
            gn = pool_nums(guest)
            seen_down |= (gn['downed'] == 1)
            seen_ko |= host_sees_ko(host)
            g_deaths1 = num(guest.call('coop.pool deaths').get('out') or '', 'deaths') or 0
            if gn['dying'] == 1 or gn['dead'] == 1 or g_deaths1 > g_deaths0:
                died = True
                break
            time.sleep(0.2)
        h_k1 = wait_for(
            lambda: cell_rivals_stats(host)
            if (cell_rivals_stats(host)['meat_drops'] or 0) > (h_k0['meat_drops'] or 0) else None,
            6.0,
        ) or cell_rivals_stats(host)
        meat_out = host.call('coop.pool meat').get('out') or ''
        print(f'  War kill: hit={hit_kill.strip()} died={died} downed={seen_down} ko={seen_ko} '
              f'kills={h_k1["pvp_kills"]} meat_drops={h_k1["meat_drops"]} meat_chunks={h_k1["meat_chunks"]}', flush=True)
        ck(died and not seen_down and not seen_ko,
           f'War kill killed rival without KO/revive (died={died}, downed={seen_down}, ko={seen_ko})')
        ck((h_k1['pvp_kills'] or 0) > (h_k0['pvp_kills'] or 0)
           and (h_k1['meat_drops'] or 0) > (h_k0['meat_drops'] or 0)
           and (h_k1['meat_chunks'] or 0) >= (h_k0['meat_chunks'] or 0) + 3,
           f'War kill dropped physical meat chunks on host (drops={h_k1["meat_drops"]}, chunks={h_k1["meat_chunks"]})')
        ck((num(meat_out, 'meat_count') or 0) >= 3, 'Host pool holds live physical meat chunks')

        # Wait for guest vanilla respawn
        ck(bool(wait_for(lambda: alive_again(guest), 60.0, 1.0)),
           'Killed rival respawned normally as in vanilla')
        time.sleep(1.5)

        # 5. Stage end -> catch-up timer -> Cell part draft copy + escalating winner payout
        gp0 = cell_parts_str(guest)
        # Pick two unlockable part indices (in 2..11) that guest currently lacks
        pick_indices = [i for i in range(2, 12) if len(gp0) == 13 and gp0[i] == '0'][:2]
        ck(len(pick_indices) == 2, f'guest has >=2 locked Cell parts for draft test (guest parts={gp0})')
        if len(pick_indices) < 2:
            return ck.result()

        for idx in pick_indices:
            host.call(f'coop.parts cell collect {idx}')
        time.sleep(2.5)  # verify parts.shared_cell does NOT auto-share in Cell Rivals
        hp1 = cell_parts_str(host)
        gp1 = cell_parts_str(guest)
        print(f'  parts before draft: host={hp1} guest={gp1} chosen={pick_indices}', flush=True)
        ck(all(hp1[i] != '0' for i in pick_indices), f'host unlocked draft parts {pick_indices}')
        ck(all(gp1[i] == '0' for i in pick_indices),
           f'Cell Rivals did not auto-share winner parts to guest ({gp1})')

        # Push host bar to full (>= 1000) -> host wins & starts fast 3 s catch-up timer
        host.call('coop.pool foodlocal 1000')
        win_synced = wait_for(
            lambda: ('winner=0' in rivals_out(host) and 'catchup=1' in rivals_out(host)
                     and 'winner=0' in rivals_out(guest) and 'catchup=1' in rivals_out(guest)),
            8.0,
        )
        ck(bool(win_synced), 'host reaching full bar automatically triggered win & catch-up timer on both peers')

        # Wait for catch-up timer (3 s) to expire -> Cell part draft starts automatically
        draft_up = wait_for(
            lambda: ('draft: active=1 winner=0' in rivals_out(host)
                     and 'draft: active=1 winner=0' in rivals_out(guest)),
            10.0,
        )
        ck(bool(draft_up), f'catch-up expiry automatically started Cell part draft ({rivals_out(host).splitlines()[3] if rivals_out(host) else ""})')

        # Guest picks BOTH winner parts (item_id = part_idx + 1)
        guest.call(f'coop.rivals draft pick {pick_indices[0] + 1}')
        wait_for(lambda: 'total_picks=1 payout=100' in rivals_out(host), 6.0)
        guest.call(f'coop.rivals draft pick {pick_indices[1] + 1}')
        picks_ok = wait_for(
            lambda: ('total_picks=2 payout=250' in rivals_out(host)
                     and 'total_picks=2 payout=250' in rivals_out(guest)),
            6.0,
        )
        ck(bool(picks_ok), 'guest copied 2 Cell parts with escalating winner payout (100 + 150 = 250)')

        # Verify copied parts were natively unlocked on guest via SK_DRAFT_APPLY
        gp_after = wait_for(
            lambda: cell_parts_str(guest)
            if len(cell_parts_str(guest)) == 13 and all(cell_parts_str(guest)[i] != '0' for i in pick_indices) else None,
            8.0,
        )
        print(f'  guest parts after draft: {gp_after or cell_parts_str(guest)}', flush=True)
        ck(bool(gp_after), f'SK_DRAFT_APPLY natively unlocked copied Cell parts {pick_indices} on guest')

        # Host claims pending payout via take_pending_draft_payout
        pay_out = host.call('coop.pool rivals payout 0').get('out') or ''
        print(f'  winner payout claim: {pay_out.strip()}', flush=True)
        ck('payout=250' in pay_out and 'queued_dna=250' in pay_out,
           f'winner claimed escalating next-stage DNA payout ({pay_out.strip()})')

        host.call('coop.rivals fasttimers 0')
    finally:
        for con in (host, guest):
            try:
                con.call('coop.pool borrow off')
                con.call('coop.uistate 0')
            except Exception:
                pass
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
