#!/usr/bin/env python3
"""WP-R6 (BUG-1550): Space Rivals mode (guest homeworld emigration R5, homeworld protection R7,
unified 4x4 relations & rival empire mirrors, per-player NPC relations + Space influence,
Race progress stats, and Chronicle Galaxy Core finale).

Space fixture, two players:
  1. Host switches Space stage to Rivals (`coop.rivals mode space rivals`):
     - Host keeps its original homeworld star/planet.
     - Guest emigrates to a different nearby unowned terrestrial planet with real cities
       (`home_cities >= 1`) and displays the emigration notice ("Your people left ... for ...").
     - Host's original homeworld appears as host's mirror empire in guest's galaxy, and guest's
       emigrated homeworld appears as guest's mirror empire in host's galaxy.
     - Save-safety / rejoin: guest leaves and rejoins; keeps the persisted emigrated homeworld
       while host's original homeworld stays untouched.
  2. Unified rivals.h 4x4 relations + mirror sync:
     - Ally -> direct War rejected -> Break (2 s fast timer) -> auto War on `coop.rivals`,
       `coop.space rivalry`, and `coop.space allies list` (`rel=2`).
  3. R7 Homeworld protection while at War:
     - `cEmpire::CaptureSystem` against rival's protected homeworld star leaves owner unchanged.
     - `cToolManager::OnUseTool` + `cSimPlanetHighLOD::DestroyPlanet` (Planet Buster) on rival's
       protected homeworld are blocked (`tool_blocked=1`) and planet stays intact with cities.
     - Peace -> Neutral.
  4. Per-player NPC empire relations + Space influence:
     - Shared diplomacy is OFF (host's local mission event on an NPC does not change guest's
       relation with that NPC, and earns +15 influence for host).
     - `coop.rivals influence buy` deducts real Space sporebucks (`cEmpire::mEmpireMoney`) and
       adds +25 influence.
     - `SmoothOver` raises actor's real `CalculateRelationship` with target NPC.
     - `Protect` blocks rival's `TurnAgainst` on protected NPC.
     - `TurnAgainst` lowers rival's real `CalculateRelationship` on an unprotected NPC.
  5. Race progress stats (`money`, `colonies`, `stars`, `allied_npcs`, `dist_to_core`) reported
     for both players in `coop.rivals`.
  6. Chronicle Galaxy Core finale awards `+250` (`kPtsGalaxyCore`) once to the first player,
     syncs the Chronicle winner summary on both host and guest, and lets the game continue.
  7. `SPORE_FIX_OFF=rivals.space` twin (`rivals_space_off`): emigration does not occur -> FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_rivals_space.py
"""
import re
import time

from harness import sp, waiter
import coop_ui
from fixtures import FIXTURES
import lane
from play_space import Player
import twoinst
from twoinst import Checks, shot, start

wait = waiter(timeout=20, step=0.5, final=True)


def sr_status(con):
    out = sp(con, 'rivals status')
    d = {}
    for k, v in re.findall(r'(\w+)=([0-9A-Fa-f.-]+)', out):
        d[k] = v
    d['text'] = out
    return d


def rivals_out(con):
    return con.call('coop.rivals').get('out') or ''


def riv_space(con):
    out = sp(con, 'rivalry')
    d = {k: float(v) for k, v in re.findall(r'(\w+)=(-?[\d.]+)\b', out)}
    d['rel'] = {int(i): r for i, r in re.findall(r' (\d+)=(ally|neutral|war)\b', out)}
    d['text'] = out
    return d


def money_of(con):
    m = re.search(r'money=(-?\d+)', sp(con, 'rivalry'))
    return int(m.group(1)) if m else None


def npc_rels(con):
    """Return list of (pol_hex, name, rel_float) from real cStarManager/cRelationshipManager."""
    out = sp(con, 'rivals npc')
    res = []
    for m in re.finditer(r"npc ([0-9A-Fa-f]{8}) '([^']*)' rel=(-?[\d.]+)", out):
        res.append((m.group(1).upper(), m.group(2), float(m.group(3))))
    return res


def npc_rel_of(con, pol_hex):
    for p, _, r in npc_rels(con):
        if p == pol_hex.upper():
            return r
    return None


def main():
    twoinst.forget_settings()
    host, guest = start('space')
    ck = Checks()
    try:
        time.sleep(4.0)

        # Record initial host homeworld before enabling Space Rivals
        h_init = sr_status(host)
        g_init = sr_status(guest)
        h_star0 = int(h_init.get('home_star', '0'))
        h_planet0 = h_init.get('home_planet', '00000000').upper()
        ck(h_star0 != 0 and h_planet0 != '00000000',
           f'initial host homeworld detected (star={h_star0} planet={h_planet0})')
        ck(h_init.get('fix') == '1' and g_init.get('fix') == '1',
           f'rivals.space feature enabled on both games (host={h_init.get("fix")} guest={g_init.get("fix")})')

        # Ensure host has enough sporebucks for influence buy test later
        m0 = money_of(host)
        if m0 is not None and m0 < 50000:
            host.call('coop.res add money 50000')
            time.sleep(1.5)

        # ---- 1. Enable Space Rivals -> R5 Guest Homeworld Emigration ----
        host.call('coop.rivals preset all_together')
        host.call('coop.rivals fasttimers 1')
        host.call('coop.rivals mode space rivals')

        # Wait for Space Rivals active on both & guest emigration completed
        ok_emig = wait(
            lambda: (
                sr_status(host).get('active') == '1'
                and sr_status(guest).get('active') == '1'
                and sr_status(guest).get('emigrated') == '1'
                and int(sr_status(guest).get('home_star', '0')) not in (0, h_star0)
                and sr_status(guest).get('home_planet', '00000000').upper() not in ('00000000', h_planet0)
            ),
            30,
            0.5,
        )
        hs = sr_status(host)
        gs = sr_status(guest)
        g_star = int(gs.get('home_star', '0'))
        g_planet = gs.get('home_planet', '00000000').upper()
        g_cities = int(gs.get('home_cities', '0'))

        ck(bool(ok_emig),
           f'R5: guest emigrated to a different star/planet (host={h_star0}/{h_planet0} -> guest={g_star}/{g_planet})')
        ck(int(hs.get('home_star', '0')) == h_star0 and hs.get('home_planet', '').upper() == h_planet0,
           f'R5: host kept its original homeworld ({hs.get("home_star")}/{hs.get("home_planet")})')
        ck(g_cities >= 1, f'R5: guest emigrated homeworld has cities (home_cities={g_cities})')
        ck('emig_notice=Your people left ' in gs['text'] or 'emig_notice=Ваш народ покинул ' in gs['text'],
           f'R5: guest received emigration notice ({gs["text"].splitlines()[-1] if gs["text"] else ""})')
        shot(guest, 'rivals_space_emigrated.png')

        if not ok_emig:
            return ck.result()

        # Verify mirrors in both galaxies:
        # Guest sees host's original homeworld as host's rival empire; Host sees guest's new homeworld as guest's rival empire.
        host_sees_guest = wait(
            lambda: f'star={g_star}' in sp(host, 'allies list') or f'star {g_star}' in sp(host, f'allies star {g_star}')
            and 'owner=FFFFFFFF' not in sp(host, f'allies star {g_star}'),
            20,
            0.5,
        )
        guest_sees_host = wait(
            lambda: f'star={h_star0}' in sp(guest, 'allies list')
            or ('owner=FFFFFFFF' not in sp(guest, f'allies star {h_star0}') and 'pol=0' not in sp(guest, 'allies list')),
            20,
            0.5,
        )
        ck(bool(host_sees_guest), f'R5: host galaxy mirrors guest emigrated homeworld star {g_star} ({sp(host, "allies list").strip()})')
        ck(bool(guest_sees_host), f'R5: guest galaxy mirrors host original homeworld star {h_star0} ({sp(guest, "allies list").strip()})')

        # Save-safety / rejoin persistence: guest leaves and rejoins -> host shares world, guest reloads
        # and automatically re-applies its persisted emigrated homeworld while host stays untouched.
        def offers_recv(con):
            m = re.search(r'world_offers_recv=(\d+)', con.call('coop.status').get('out') or '')
            return int(m.group(1)) if m else 0

        def in_space_settled(con):
            st = con.call('coop.status').get('out') or ''
            busy = re.search(r'^busy:\s*(.*)$', st, re.M)
            busy_s = busy.group(1).strip() if busy else ''
            return 'client id=' in st and 'loading' not in busy_s and bool(con.call('coop.avatar').get('ok'))

        offers0 = offers_recv(guest)
        guest.call('coop.leave')
        wait(lambda: 'connected=0' in (guest.call('coop.status').get('out') or '')
             or 'role=none' in (guest.call('coop.status').get('out') or ''), 10, 0.3)
        time.sleep(1.0)
        guest.call(f'coop.join 127.0.0.1:{lane.PORT}')

        # Host may pause on "game saved" box while sharing its world to the rejoining guest; dismiss if needed.
        t_rj = time.time()
        saw_offer = False
        while time.time() - t_rj < 60.0:
            f1 = twoinst.host_frame(host)
            time.sleep(0.6)
            if twoinst.host_frame(host) == f1:
                host.call('key enter')
            if offers_recv(guest) > offers0:
                saw_offer = True
            if saw_offer and in_space_settled(guest):
                gs_now = sr_status(guest)
                if (gs_now.get('active') == '1'
                        and int(gs_now.get('home_star', '0')) == g_star
                        and gs_now.get('home_planet', '').upper() == g_planet):
                    break
            elif time.time() - t_rj > 10.0 and in_space_settled(guest):
                # Fallback if warm reconnect skipped world offer
                gs_now = sr_status(guest)
                if (gs_now.get('active') == '1'
                        and int(gs_now.get('home_star', '0')) == g_star
                        and gs_now.get('home_planet', '').upper() == g_planet):
                    break

        rejoined = (
            in_space_settled(guest)
            and sr_status(guest).get('active') == '1'
            and int(sr_status(guest).get('home_star', '0')) == g_star
            and sr_status(guest).get('home_planet', '').upper() == g_planet
        )
        ck(bool(rejoined),
           f'R5: rejoin keeps persisted guest homeworld ({g_star}/{g_planet}; now {sr_status(guest).get("home_star")}/{sr_status(guest).get("home_planet")}, saw_offer={saw_offer})')
        ck(int(sr_status(host).get('home_star', '0')) == h_star0,
           'R5: host original homeworld untouched after guest rejoin')

        # ---- 2. Unified rivals.h 4x4 relations (Ally -> Break countdown -> War) + Mirror Sync ----
        host.call('coop.rivals ally 1')
        wait(lambda: '0->1:ally' in rivals_out(guest), 10, 0.3)
        guest.call('coop.rivals accept 0')
        allied_ok = wait(
            lambda: '0-1=Ally' in rivals_out(host) and '0-1=Ally' in rivals_out(guest)
            and riv_space(host)['rel'].get(1) == 'ally' and riv_space(guest)['rel'].get(0) == 'ally',
            12,
            0.3,
        )
        ck(bool(allied_ok), 'relations: Ally synced on rivals.h and space_rivalry')

        direct_war = host.call('coop.rivals war 1').get('out') or ''
        ck('error:' in direct_war and '0-1=Ally' in rivals_out(host),
           f'relations: direct War while allied rejected ({direct_war.strip()})')

        brk = host.call('coop.rivals break 1').get('out') or ''
        ck('ok' in brk, f'relations: alliance break countdown started ({brk.strip()})')
        war_ok = wait(
            lambda: '0-1=War' in rivals_out(host) and '0-1=War' in rivals_out(guest)
            and riv_space(host)['rel'].get(1) == 'war' and riv_space(guest)['rel'].get(0) == 'war',
            12,
            0.3,
        )
        ck(bool(war_ok), 'relations: 60 s (fast 2 s) break countdown auto-transitioned both to War')

        mirror_war = wait(lambda: 'rel=2' in sp(host, 'allies list'), 12, 0.5)
        ck(bool(mirror_war), f'relations: rival mirror empire updated to War rel=2 ({sp(host, "allies list").strip()})')

        # ---- 3. R7 Homeworld Inviolability (Capture & Planet Buster blocked while at War) ----
        # Make sure both games know both protected homeworlds
        wait(lambda: int(sr_status(host).get('homes', '0')) >= 2 and int(sr_status(guest).get('homes', '0')) >= 2, 10, 0.3)

        cap_g = sp(guest, f'rivals test_capture {h_star0}')
        m_cg = re.search(r'owner ([0-9A-Fa-f]+) -> ([0-9A-Fa-f]+) blocked_delta=(\d+)', cap_g)
        ck(bool(m_cg) and m_cg.group(1) == m_cg.group(2) and int(m_cg.group(3)) >= 1,
           f'R7: guest cannot capture host homeworld star {h_star0} ({cap_g.strip()})')

        cap_h = sp(host, f'rivals test_capture {g_star}')
        m_ch = re.search(r'owner ([0-9A-Fa-f]+) -> ([0-9A-Fa-f]+) blocked_delta=(\d+)', cap_h)
        ck(bool(m_ch) and m_ch.group(1) == m_ch.group(2) and int(m_ch.group(3)) >= 1,
           f'R7: host cannot capture guest homeworld star {g_star} ({cap_h.strip()})')

        # Land cleanly via Player.land_retry on the protected homeworld (no NaN pos) to test real Planet Buster hooks
        ph = Player('host', lane.HOST, 0, 1, time.time() + 600, {})
        home = FIXTURES['space']['home']
        if ph.ctx() == 'system':
            ph.land_retry(home)
        wait(lambda: ph.ctx() == 'planet' and h_planet0 in sp(host, 'star').upper(), 20, 0.5)
        bust_out = sp(host, 'rivals test_buster')
        m_b = re.search(r'protected=(\d) tool_blocked=(\d) blocked_delta=(\d+)', bust_out)
        ck(bool(m_b) and m_b.group(1) == '1' and m_b.group(2) == '1' and int(m_b.group(3)) >= 2,
           f'R7: Planet Buster tool use & DestroyPlanet both blocked on protected homeworld ({bust_out.strip()})')
        ck(int(sr_status(host).get('home_cities', '0')) >= 1,
           'R7: host homeworld cities remain intact after blocked Planet Buster')

        # Return to Neutral via Peace
        guest.call('coop.rivals peace 0')
        wait(lambda: '1->0:peace' in rivals_out(host), 10, 0.3)
        host.call('coop.rivals accept 1')
        peace_ok = wait(
            lambda: '0-1=Neutral' in rivals_out(host) and '0-1=Neutral' in rivals_out(guest)
            and riv_space(host)['rel'].get(1) == 'neutral',
            10,
            0.3,
        )
        ck(bool(peace_ok), 'relations: Peace returned both players to Neutral')

        # ---- 4. Per-player NPC empire relations + Space influence ----
        h_npcs = npc_rels(host)
        g_npcs = npc_rels(guest)
        g_pols = {p for p, _, _ in g_npcs}
        common = [(p, nm, r) for p, nm, r in h_npcs if p in g_pols]
        ck(len(common) >= 2, f'NPC empires present in both galaxies (common={len(common)})')

        if len(common) >= 2:
            npc1 = common[0][0]
            npc2 = common[1][0]

            # 4a. Shared diplomacy is OFF + mission event earns +15 influence for host only
            g_rel1_before = npc_rel_of(guest, npc1)
            h_rel1_before = npc_rel_of(host, npc1)
            host.call('coop.rivals influence add 0 0')  # query baseline
            m_bal0 = re.search(r'bal=\[(\d+),(\d+),', rivals_out(host))
            inf0_before = int(m_bal0.group(1)) if m_bal0 else 0

            sp(host, f'diplomacy apply 0x{npc1} 0x0526E519')
            time.sleep(2.5)
            h_rel1_after = npc_rel_of(host, npc1)
            g_rel1_after = npc_rel_of(guest, npc1)
            m_bal1 = re.search(r'bal=\[(\d+),(\d+),', rivals_out(host))
            inf0_after = int(m_bal1.group(1)) if m_bal1 else 0

            ck(h_rel1_after is not None and h_rel1_before is not None and h_rel1_after > h_rel1_before,
               f'NPC per-player: host mission raised host relation with {npc1} ({h_rel1_before:.2f} -> {h_rel1_after:.2f})')
            ck(g_rel1_after is not None and g_rel1_before is not None and abs(g_rel1_after - g_rel1_before) < 1e-3,
               f'NPC per-player: shared diplomacy OFF, guest relation with {npc1} unchanged ({g_rel1_before:.2f} -> {g_rel1_after:.2f})')
            ck(inf0_after >= inf0_before + 15,
               f'Influence earn: NPC mission granted +15 influence ({inf0_before} -> {inf0_after})')

            # 4b. Buy influence deducts real Space sporebucks (cEmpire::mEmpireMoney)
            money_before = money_of(host)
            buy_out = host.call('coop.rivals influence buy').get('out') or ''
            time.sleep(1.0)
            money_after = money_of(host)
            m_bal2 = re.search(r'bal=\[(\d+),(\d+),', rivals_out(host))
            inf0_bought = int(m_bal2.group(1)) if m_bal2 else 0
            ck('ok' in buy_out and money_before is not None and money_after is not None and money_after < money_before
               and inf0_bought == inf0_after + 25,
               f'Influence buy: deducted real sporebucks ({money_before} -> {money_after}) and added +25 inf ({inf0_after} -> {inf0_bought})')

            # Top up both players' influence for SmoothOver / Protect / TurnAgainst checks
            host.call('coop.rivals influence add 0 100')
            host.call('coop.rivals influence add 1 100')
            wait(lambda: int(re.search(r'bal=\[(\d+),(\d+),', rivals_out(guest)).group(2)) >= 100
                 if re.search(r'bal=\[(\d+),(\d+),', rivals_out(guest)) else False, 10, 0.3)

            # 4c. SmoothOver raises actor's real CalculateRelationship with npc2
            h_rel2_before = npc_rel_of(host, npc2)
            sm_out = host.call(f'coop.rivals influence smooth 0x{npc2}').get('out') or ''
            time.sleep(1.5)
            h_rel2_after = npc_rel_of(host, npc2)
            ck('ok' in sm_out and h_rel2_after is not None and h_rel2_before is not None and h_rel2_after > h_rel2_before,
               f'Influence SmoothOver: raised host relation with {npc2} ({h_rel2_before:.2f} -> {h_rel2_after:.2f})')

            # 4d. Protect by guest blocks host's TurnAgainst on npc1
            guest.call(f'coop.rivals influence protect 0x{npc1}')
            wait(lambda: 'prots=1' in rivals_out(host), 10, 0.3)
            turn_blk = host.call(f'coop.rivals influence turn 0x{npc1} 1').get('out') or ''
            ck('error:' in turn_blk and 'protected' in turn_blk,
               f'Influence Protect: blocked rival TurnAgainst on {npc1} ({turn_blk.strip()})')

            # 4e. TurnAgainst on unprotected npc2 lowers guest's real CalculateRelationship with npc2
            g_rel2_before = npc_rel_of(guest, npc2)
            turn_ok = host.call(f'coop.rivals influence turn 0x{npc2} 1').get('out') or ''
            g_rel2_dropped = wait(
                lambda: npc_rel_of(guest, npc2) is not None and g_rel2_before is not None
                and npc_rel_of(guest, npc2) < g_rel2_before - 0.1,
                10,
                0.4,
            )
            g_rel2_after = npc_rel_of(guest, npc2)
            ck('ok' in turn_ok and bool(g_rel2_dropped),
               f'Influence TurnAgainst: lowered guest relation with {npc2} ({g_rel2_before:.2f} -> {g_rel2_after})')

        # ---- 5. Race progress stats (money, colonies, stars, allied NPCs, dist_to_core) + F9 Rivals UI ----
        prog_ok = wait(
            lambda: 'p0 ' in sp(host, 'rivals race') and 'p1 ' in sp(host, 'rivals race'),
            12,
            0.5,
        )
        race_txt = sp(host, 'rivals race')
        m_p0 = re.search(r'p0 money=(-?\d+) colonies=(\d+) stars=(\d+) allies=(\d+) dist=([\d.]+)', race_txt)
        m_p1 = re.search(r'p1 money=(-?\d+) colonies=(\d+) stars=(\d+) allies=(\d+) dist=([\d.]+)', race_txt)
        ck(bool(prog_ok) and bool(m_p0) and bool(m_p1)
           and int(m_p0.group(2)) >= 1 and int(m_p1.group(2)) >= 1
           and float(m_p0.group(5)) > 0.0 and float(m_p1.group(5)) > 0.0,
           f'Race progress reported for host & guest with dist to core ({race_txt.strip()})')

        # Verify F9 Rivals/Race UI page renders Space Rivals rows (protected homeworld + progress)
        host.call('coop.uistate 1')
        hui = coop_ui.Ui(host, 'host')
        if hui.state().get('open') != '1':
            host.call(f'key {coop_ui.VK_F9}')
        wait(lambda: hui.click('rivals'), 8, 0.4)
        ui_r = wait(lambda: host.call('coop.uistate').get('out')
                    if 'page=rivals' in (host.call('coop.uistate').get('out') or '') else None, 8, 0.3)
        if hui.state().get('open') == '1':
            host.call(f'key {coop_ui.VK_F9}')
        host.call('coop.uistate 0')
        ck(bool(ui_r) and ('protected' in (ui_r or '') or 'защищена' in (ui_r or '')),
           'F9 Rivals UI displays Space Rivals homeworld protection & progress rows')

        # ---- 6. Chronicle Galaxy Core Finale & Winner Summary ----
        # Give host 100 prior points so guest's +250 Galaxy Core bonus makes guest win (250 > 100)
        host.call('coop.rivals chronicle add 0 100 prior_stage')
        time.sleep(0.5)
        sp(guest, 'rivals test_core 1')
        fin_synced = wait(
            lambda: (
                sr_status(host).get('finale_awarded') == '1'
                and sr_status(guest).get('finale_awarded') == '1'
                and 'totals=[100,250,0,0] leader=1' in rivals_out(host)
                and 'totals=[100,250,0,0] leader=1' in rivals_out(guest)
            ),
            15,
            0.5,
        )
        ck(bool(fin_synced),
           f'Chronicle Galaxy Core finale: first player (guest) awarded +250 and synced ({rivals_out(host).splitlines()[-1] if rivals_out(host) else ""})')

        # Second player entering core does NOT receive a duplicate bonus
        sp(host, 'rivals test_core 0')
        time.sleep(1.0)
        ck('totals=[100,250,0,0] leader=1' in rivals_out(host),
           'Chronicle Galaxy Core finale: second core entry does not duplicate bonus')

        hs_fin = sr_status(host)['text']
        gs_fin = sr_status(guest)['text']
        ck(('finale_summary=Chronicle Finale' in hs_fin or 'finale_summary=Финал Хроники' in hs_fin)
           and ('finale_summary=Chronicle Finale' in gs_fin or 'finale_summary=Финал Хроники' in gs_fin),
           'Chronicle Galaxy Core finale: both host and guest display end-screen summary while game continues')
        shot(host, 'rivals_space_finale.png')

    finally:
        try:
            host.call('coop.uistate 0')
            host.call('coop.rivals fasttimers 0')
        except Exception:
            pass
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
