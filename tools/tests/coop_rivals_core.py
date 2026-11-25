#!/usr/bin/env python3
"""WP-R1 (BUG-0794): Rivals shared systems (stage modes, 4x4 relations + break countdown, influence, race/catch-up,
stage-end draft & winner payout, Chronicle, UI Rivals/Race page, rejoin persistence, and faction diagnostics).

Tribe fixture, two players:
  1. Host toggles stage modes (Cell=Rivals, Tribe=Rivals, catchup=240, draftpicks=3, distance=far); guest sees
     updated modes in `coop.rivals` AND on the F9 Rivals/Race UI page (read-only on guest).
  2. Guest leaves and rejoins; guest receives the authoritative Rivals snapshot and still sees the toggled stage modes.
  3. Alliance -> Break (fast timer 2 s) -> War -> Peace flow synced on both host and guest (direct war while allied
     is rejected).
  4. Influence earn / buy / protect / turn (TurnAgainst blocked while rival protects NPC, succeeds on unprotected NPC).
  5. Stage win -> catch-up timer -> catch-up expiry -> Draft start -> guest picks 2 items (100 + 150 = 250 payout) ->
     winner claims payout -> Chronicle totals synced on both host and guest.
  6. Read-only faction diagnostics `coop.tribe factions` (live cTribe entries) and `coop.civ factions` return valid output.
  7. SPORE_FIX_OFF=rivals.core: Rivals mode disabled, guest sees no synced stage modes -> FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_rivals_core.py
"""
import re
import time

import coop_ui
import lane
import settings_lib as sl
import twoinst
from twoinst import Checks, start


def rivals_out(con):
    return con.call('coop.rivals').get('out') or ''


def wait_for(fn, timeout=12.0, step=0.25):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


def main():
    twoinst.forget_settings()
    host, guest = start('tribe')
    ck = Checks()
    try:
        time.sleep(2.0)

        # 1. Host toggles stage modes & lobby config; guest receives snapshot and sees it in coop.rivals + UI
        host.call('coop.rivals preset all_together')
        host.call('coop.rivals mode cell rivals')
        host.call('coop.rivals mode tribe rivals')
        host.call('coop.rivals catchup 240')
        host.call('coop.rivals draftpicks 3')
        host.call('coop.rivals distance far')

        want_stages = 'stages=[cell=Rivals creature=Together tribe=Rivals civ=Together space=Together]'
        h0 = rivals_out(host)
        g0 = wait_for(lambda: rivals_out(guest) if want_stages in rivals_out(guest) else None, 10.0)
        ck(want_stages in h0 and 'catchup=240' in h0 and 'dist=Far' in h0,
           f'host applied stage modes & config ({h0.splitlines()[0] if h0 else ""})')
        ck(bool(g0) and 'catchup=240' in (g0 or '') and 'dist=Far' in (g0 or ''),
           f'guest synced stage modes & config ({(g0 or "").splitlines()[0] if g0 else rivals_out(guest)[:120]})')
        if not g0:
            return ck.result()

        # Verify guest F9 Rivals/Race UI page shows the synced stage modes (read-only, no toggle buttons)
        guest.call('coop.uistate 1')
        gui = coop_ui.Ui(guest, 'guest')
        clicked = wait_for(lambda: gui.click('rivals'), 8.0, 0.4)
        ui_r = wait_for(lambda: guest.call('coop.uistate').get('out')
                        if 'page=rivals' in (guest.call('coop.uistate').get('out') or '') else None, 8.0)
        guest.call('coop.uistate 0')
        ck(bool(clicked) and bool(ui_r), 'guest opened F9 Rivals/Race page')
        if ui_r:
            ck('Cell: Rivals' in ui_r and 'Tribe: Rivals' in ui_r and 'Creature: Together' in ui_r,
               'guest F9 Rivals page displays synced stage modes')
            ck("rrow 1 '  Cell: Rivals' ''" in ui_r,
               'guest stage mode row is read-only (no toggle button)')

        # 2. Rejoin persistence: guest leaves and rejoins; still gets host's stage modes on join
        guest.call('coop.leave')
        wait_for(lambda: 'role=none' in (guest.call('coop.status').get('out') or '')
                 or 'connected=0' in (guest.call('coop.status').get('out') or ''), 8.0)
        time.sleep(0.8)
        guest.call(f'coop.join 127.0.0.1:{lane.PORT}')
        g_rejoin = wait_for(lambda: rivals_out(guest) if want_stages in rivals_out(guest) else None, 20.0)
        ck(bool(g_rejoin), 'rejoined guest received persisted stage modes on join')

        # 3. Alliance -> Break (fast timer 2 s) -> War -> Peace flow synced on both host & guest
        host.call('coop.rivals fasttimers 1')
        host.call('coop.rivals ally 1')
        wait_for(lambda: '0->1:ally' in rivals_out(guest), 8.0)
        guest.call('coop.rivals accept 0')
        allied_both = wait_for(lambda: '0-1=Ally' in rivals_out(host) and '0-1=Ally' in rivals_out(guest), 8.0)
        ck(bool(allied_both), 'alliance synced on both host and guest (0-1=Ally)')

        # Direct war declaration while allied must be rejected (60 s / 2 s break notice required)
        direct_war = host.call('coop.rivals war 1').get('out') or ''
        ck('error:' in direct_war and '0-1=Ally' in rivals_out(host),
           f'direct war while allied rejected ({direct_war.strip()})')

        # Break alliance -> stays Ally during notice, then auto-transitions to War after 2 s
        brk = host.call('coop.rivals break 1').get('out') or ''
        ck('ok' in brk and 'breaks=[0-1:' in rivals_out(host), f'alliance break countdown started ({brk.strip()})')
        war_both = wait_for(lambda: '0-1=War' in rivals_out(host) and '0-1=War' in rivals_out(guest), 8.0)
        ck(bool(war_both), 'alliance break countdown auto-transitioned both host and guest to War')

        # Peace proposal from guest -> host accepts -> Neutral on both
        guest.call('coop.rivals peace 0')
        wait_for(lambda: '1->0:peace' in rivals_out(host), 8.0)
        host.call('coop.rivals accept 1')
        peace_both = wait_for(lambda: '0-1=Neutral' in rivals_out(host) and '0-1=Neutral' in rivals_out(guest), 8.0)
        ck(bool(peace_both), 'peace synced on both host and guest (0-1=Neutral)')

        # 4. Influence: earn, protect, TurnAgainst blocked by protect, TurnAgainst succeeds on unprotected NPC, buy
        host.call('coop.rivals influence add 0 100')
        host.call('coop.rivals influence add 1 100')
        wait_for(lambda: 'bal=[100,100,' in rivals_out(guest), 8.0)

        guest.call('coop.rivals influence protect 0x1234')
        prot_synced = wait_for(lambda: 'prots=1' in rivals_out(host) and 'bal=[100,80,' in rivals_out(guest), 8.0)
        ck(bool(prot_synced), 'guest Protect deducted 20 influence and synced prots=1')

        turn_blocked = host.call('coop.rivals influence turn 0x1234 1').get('out') or ''
        ck('error:' in turn_blocked and 'protected' in turn_blocked and 'bal=[100,80,' in rivals_out(host),
           f'TurnAgainst blocked by active Protect ({turn_blocked.strip()})')

        turn_ok = host.call('coop.rivals influence turn 0x5678 1').get('out') or ''
        ck('ok' in turn_ok and 'bal=[70,80,' in rivals_out(host),
           f'TurnAgainst succeeded on unprotected NPC ({turn_ok.strip()})')

        buy_ok = host.call('coop.rivals influence buy').get('out') or ''
        ck('ok' in buy_ok and 'bal=[95,80,' in rivals_out(host) and 'buys=[1,0,' in rivals_out(host),
           f'Influence buy added +25 and incremented buys ({buy_ok.strip()})')

        # 5. Stage win -> catch-up timer -> expiry -> Draft pick & escalating winner payout -> Chronicle
        win_out = host.call('coop.rivals win 0').get('out') or ''
        ck('rank=1' in win_out, f'host reported stage win ({win_out.strip()})')
        cu_active = wait_for(lambda: 'catchup=1' in rivals_out(guest) and 'winner=0' in rivals_out(guest), 8.0)
        ck(bool(cu_active), 'guest sees stage winner=0 and catch-up timer active')

        # Wait for fast catch-up timer (3 s) to expire
        cu_done = wait_for(
            lambda: ('catchup=0' in rivals_out(host) and 'done=1' in rivals_out(host)
                     and 'catchup=0' in rivals_out(guest) and 'done=1' in rivals_out(guest)),
            8.0,
        )
        ck(bool(cu_done), 'catch-up timer expired cleanly on host and guest')

        # Start draft with 4 items; guest picks 2 items -> escalating payout 100 + 150 = 250
        ds_out = host.call('coop.rivals draft start 0 4').get('out') or ''
        ck('draft started' in ds_out, f'host started draft ({ds_out.strip()})')
        wait_for(lambda: 'draft: active=1 winner=0 items=4' in rivals_out(guest), 8.0)

        guest.call('coop.rivals draft pick 1')
        wait_for(lambda: 'total_picks=1 payout=100' in rivals_out(host), 8.0)
        guest.call('coop.rivals draft pick 2')
        picks_synced = wait_for(
            lambda: 'total_picks=2 payout=250' in rivals_out(host) and 'total_picks=2 payout=250' in rivals_out(guest),
            8.0,
        )
        ck(bool(picks_synced), 'guest draft picks synced with escalating payout (100 + 150 = 250)')

        payout_out = host.call('coop.rivals draft payout 0').get('out') or ''
        ck('payout=250' in payout_out, f'winner claimed pending draft payout ({payout_out.strip()})')

        # Chronicle points synced across host & guest
        host.call('coop.rivals chronicle add 1 40 war_won')
        chr_synced = wait_for(
            lambda: 'totals=[100,40,0,0] leader=0' in rivals_out(host)
            and 'totals=[100,40,0,0] leader=0' in rivals_out(guest),
            8.0,
        )
        ck(bool(chr_synced), 'Chronicle totals=[100,40,0,0] leader=0 synced on both host and guest')

        # 6. Read-only faction diagnostics: coop.tribe factions and coop.civ factions
        tf_h = host.call('coop.tribe factions').get('out') or ''
        tf_g = guest.call('coop.tribe factions').get('out') or ''
        m_th = re.search(r'tribe factions: count=(\d+)', tf_h)
        m_tg = re.search(r'tribe factions: count=(\d+)', tf_g)
        ck(bool(m_th) and int(m_th.group(1)) >= 1 and 'player=1' in tf_h and 'members=' in tf_h and 'food=' in tf_h,
           f'host coop.tribe factions returned valid live tribes ({tf_h.splitlines()[0] if tf_h else ""})')
        ck(bool(m_tg) and int(m_tg.group(1)) >= 1 and 'player=1' in tf_g,
           f'guest coop.tribe factions returned valid live tribes ({tf_g.splitlines()[0] if tf_g else ""})')

        cf_h = host.call('coop.civ factions').get('out') or ''
        ck('civ factions: count=0' in cf_h, f'coop.civ factions safe outside Civ ({cf_h.strip()})')

        host.call('coop.rivals fasttimers 0')
    finally:
        try:
            guest.call('coop.uistate 0')
        except Exception:
            pass
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
