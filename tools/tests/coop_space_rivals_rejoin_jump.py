#!/usr/bin/env python3
"""BUG-1567: Space Rivals Galaxy-view rejoin emigration fault storm (0x0106CA9F/0x00FDC5FD)
and allied Jump-to EnterSolarSystem crash (0x00BB0F22).

Two-game Wine lab test reproducing both failures from campaign_rivals (All rivals preset, space.allies=0):
  1. Host switches to `all_rivals` preset (`space.allies=0`, Space stage mode Rivals).
     - Guest emigrates from host's original home star `h_star0` to `g_star` and relinquishes `h_star0`.
     - Verify `coop.space rivals star <h_star0>` on guest has `owner=FFFFFFFF`, `star_tech=1`,
       `planet_tech=1` (NOT orphaned tech 5), and `coop.empirecontact` reports `galaxy_hooked=1`
       and `color_hooked=1`.
  2. Bug A (Galaxy-view rejoin emigration + hovering relinquished host home star `h_star0`):
     - Host zooms out to Galaxy view (`ctx=2`, `world=-`).
     - Guest leaves and rejoins; host shares its Galaxy-view save so guest loads directly into `ctx=2`,
       re-emigrates to `g_star`, relinquishes `h_star0`, and teleports in Galaxy view.
     - Guest hovers mouse over `h_star0`'s projected Galaxy screen coordinates (and screen center)
       for ~2.5 s; verify 0 faults at `0x0106CA9F` / `0x00FDC5FD` and `h_star0` stays clean (`star_tech=1`).
  3. Bug B (Allied Jump to host's relinquished home star `h_star0` -> `EnterSolarSystem`):
     - Host and guest form an Alliance (`0-1=Ally`).
     - Guest calls `coop.space jump 0` (exact `ui: click jump` -> `travel_to(h_star0)` ->
       `SpaceTeleportTo` -> `UFO_SnapToDestination` -> `EnterSolarSystem` path).
     - Verify guest arrives in `h_star0`, stays responsive for 2 s, and has 0 crashes / 0 faults
       (no `0x00BB0F22`, no `0x0106CA9F`, no `0x00FDC5FD`).

  Twin (`space_rivals_rejoin_jump_off` with
  `SPORE_FIX_OFF=rivals.space_relinquish_clean,space.null_empire_galaxy,space.empire_color_homestar`):
    - `h_star0` remains orphaned tech-5, Galaxy hover faults at `0x0106CA9F`, and allied Jump
      crashes at `0x00BB0F22` -> FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_space_rivals_rejoin_jump.py
"""
import re
import time

from harness import sp, waiter
import lane
import twoinst
from twoinst import Checks, shot, start

wait = waiter(timeout=25, step=0.5, final=True)


def sr_status(con):
    out = sp(con, 'rivals status')
    d = {}
    for k, v in re.findall(r'(\w+)=([0-9A-Fa-f.-]+)', out):
        d[k] = v
    d['text'] = out
    return d


def sr_star(con, star_id):
    out = sp(con, f'rivals star {star_id}')
    d = {}
    for k, v in re.findall(r'(\w+)=([0-9A-Fa-f]+)', out):
        d[k] = v
    d['text'] = out.strip()
    return d


def emp_contact_status(con):
    out = con.call('coop.empirecontact').get('out') or ''
    d = {}
    for k, v in re.findall(r'(\w+)=([0-9A-Fa-f]+)', out):
        d[k] = v
    d['text'] = out.strip()
    return d


def place_info(con):
    m = re.search(r'place ([0-9A-F]+) star (\d+) active ([0-9A-F]+)', sp(con, 'star'))
    return (int(m.group(1), 16), int(m.group(2)), m.group(3)) if m else (0, 0, '')


def context(con):
    p = place_info(con)[0]
    return 'planet' if p & 0x40000000 else 'system' if p & 0x80000000 else 'galaxy'


def space_star(con):
    return place_info(con)[1]


def no_movie(con, secs=15):
    for i in range(int(secs / 1.5)):
        if 'movie=0' in sp(con, 'hp'):
            return True
        con.call('key enter' if i % 2 == 0 else 'click 1410 1048')
        time.sleep(1.5)
    return 'movie=0' in sp(con, 'hp')


def to_galaxy(con):
    for _ in range(18):
        if context(con) == 'galaxy':
            return True
        con.call('wheel 720 540 -10')
        time.sleep(0.7)
    return context(con) == 'galaxy'


def projected_star(con, star_id):
    m = re.search(r'screen (\d) (-?\d+) (-?\d+)', sp(con, 'map project', star_id))
    return (int(m.group(2)), int(m.group(3))) if m and m.group(1) == '1' else None


def faults_of(con):
    try:
        out = con.call('exc').get('out') or ''
    except Exception as e:
        return None, f'exc failed: {e}'
    m = re.search(r'total faults:\s*(\d+)', out)
    return (int(m.group(1)) if m else -1), out


def rivals_out(con):
    return con.call('coop.rivals').get('out') or ''


def offers_recv(con):
    m = re.search(r'world_offers_recv=(\d+)', con.call('coop.status').get('out') or '')
    return int(m.group(1)) if m else 0


def in_space_settled(con):
    st = con.call('coop.status').get('out') or ''
    busy = re.search(r'^busy:\s*(.*)$', st, re.M)
    busy_s = busy.group(1).strip() if busy else ''
    return 'client id=' in st and 'loading' not in busy_s and bool(con.call('coop.avatar').get('ok'))


def main():
    twoinst.forget_settings()
    host, guest = start('space')
    ck = Checks()
    try:
        time.sleep(3.5)

        h_init = sr_status(host)
        h_star0 = int(h_init.get('home_star', '0'))
        h_planet0 = h_init.get('home_planet', '00000000').upper()
        ck(h_star0 != 0 and h_planet0 != '00000000',
           f'initial host homeworld detected (star={h_star0} planet={h_planet0})')

        ec_g = emp_contact_status(guest)
        ck(ec_g.get('galaxy_hooked') == '1' and ec_g.get('color_hooked') == '1',
           f'BUG-1567 engine guards hooked on guest ({ec_g["text"]})')

        # ---- 1. Enable All Rivals preset (space.allies = 0, Space Rivals on) -> guest emigrates ----
        host.call('coop.rivals preset all_rivals')
        host.call('coop.rivals fasttimers 1')

        ok_emig = wait(
            lambda: (
                sr_status(host).get('active') == '1'
                and sr_status(guest).get('active') == '1'
                and sr_status(guest).get('emigrated') == '1'
                and int(sr_status(guest).get('home_star', '0')) not in (0, h_star0)
                and space_star(guest) == int(sr_status(guest).get('home_star', '0'))
            ),
            35,
            0.5,
        )
        gs = sr_status(guest)
        g_star = int(gs.get('home_star', '0'))
        g_planet = gs.get('home_planet', '00000000').upper()
        ck(bool(ok_emig), f'guest emigrated in all_rivals preset (host={h_star0} -> guest={g_star}/{g_planet})')
        if not ok_emig:
            return ck.result()

        # Verify host's original home star was cleanly relinquished (tech lowered from 5 -> 1, not orphaned tech-5)
        st1 = sr_star(guest, h_star0)
        ck(st1.get('owner', '').upper() == 'FFFFFFFF'
           and st1.get('star_tech') == '1'
           and st1.get('planet_tech') == '1',
           f'clean relinquish cleared host home tech 5 -> 1 ({st1["text"]})')

        # ---- 2. Bug A: Host zooms out to Galaxy view (ctx=2), guest leaves and rejoins into ctx=2 ----
        sp(host, 'tool add interstellardrive')
        sp(guest, 'tool add interstellardrive')
        no_movie(host)
        no_movie(guest)
        gal_ok = to_galaxy(host)
        ck(bool(gal_ok), f'host zoomed out to Galaxy view (ctx={context(host)})')
        if not gal_ok:
            return ck.result()

        # Wait briefly so host state settles in Galaxy view before guest rejoin share
        time.sleep(1.5)
        faults_before, _ = faults_of(guest)
        offers0 = offers_recv(guest)

        guest.call('coop.leave')
        wait(lambda: 'connected=0' in (guest.call('coop.status').get('out') or '')
             or 'role=none' in (guest.call('coop.status').get('out') or ''), 10, 0.3)
        time.sleep(1.0)
        guest.call(f'coop.join 127.0.0.1:{lane.PORT}')

        # Host saves/shares its Galaxy-view world; dismiss "game saved" box with Enter if host pauses.
        # Record when the world offer arrives and wait >= 5 s after that (so guest actually leaves to menu and
        # reloads the shared Galaxy-view save) before checking in_space_settled + re-emigration.
        t_rj = time.time()
        saw_offer_at = 0.0
        rejoined = False
        while time.time() - t_rj < 60.0:
            f1 = twoinst.host_frame(host)
            time.sleep(0.6)
            if twoinst.host_frame(host) == f1:
                host.call('key enter')
            if not saw_offer_at and offers_recv(guest) > offers0:
                saw_offer_at = time.time()
            if saw_offer_at and (time.time() - saw_offer_at) >= 5.0 and in_space_settled(guest):
                gs_now = sr_status(guest)
                if (gs_now.get('active') == '1'
                        and int(gs_now.get('home_star', '0')) == g_star
                        and gs_now.get('home_planet', '').upper() == g_planet
                        and gs_now.get('teleported') == '1'):
                    rejoined = True
                    break

        ck(bool(rejoined),
           f'guest rejoined from host Galaxy save and re-emigrated cleanly (ctx={context(guest)}, saw_offer={bool(saw_offer_at)})')
        if not rejoined:
            return ck.result()

        # Dismiss any Galaxy-entry tutorial caption on guest and ensure guest is in Galaxy view (ctx=2),
        # then hover cursor over host's relinquished home star h_star0 (exact 0x0106CA9F / 0x00FDC5FD trigger).
        no_movie(guest, secs=8)
        if context(guest) != 'galaxy':
            sp(guest, 'tool add interstellardrive')
            no_movie(guest, secs=5)
            to_galaxy(guest)
        st2 = sr_star(guest, h_star0)
        ck(st2.get('owner', '').upper() == 'FFFFFFFF'
           and st2.get('star_tech') == '1'
           and st2.get('planet_tech') == '1',
           f'after Galaxy rejoin, relinquished host star stays clean tech=1 ({st2["text"]})')

        pt = projected_star(guest, h_star0)
        print(f'  guest galaxy ctx={context(guest)} projected h_star0={h_star0}: {pt} hp={sp(guest, "hp").strip()}', flush=True)
        pts = []
        if pt and 20 < pt[0] < 1420 and 20 < pt[1] < 1060:
            pts.extend([(pt[0], pt[1]), (pt[0] + 4, pt[1]), (pt[0], pt[1] + 4), (pt[0] - 4, pt[1])])
        pts.append((720, 540))
        for x, y in pts:
            try:
                guest.call(f'move {x} {y}')
                time.sleep(0.8)
            except Exception:
                break
        shot(guest, 'space_rivals_rejoin_galaxy.png')

        faults_after_gal, exc_gal = faults_of(guest)
        gal_new_faults = (faults_after_gal - faults_before) if (faults_after_gal is not None and faults_before is not None) else 999
        has_gal_sig = ('0x0106CA9F' in exc_gal or '0106CA9F' in exc_gal
                       or '0x00FDC5FD' in exc_gal or '00FDC5FD' in exc_gal)
        ck(faults_after_gal is not None and gal_new_faults == 0 and not has_gal_sig,
           f'Bug A: zero Galaxy-view faults after rejoin + hover on {h_star0} (delta={gal_new_faults}, exc={exc_gal.splitlines()[0] if exc_gal else ""})')

        # ---- 3. Bug B: Ally with host and Jump to host's relinquished home star h_star0 (0x00BB0F22 trigger) ----
        # Host dismisses any Galaxy caption and re-enters its home star system h_star0 so guest can Jump to it.
        no_movie(host, secs=8)
        j_host = sp(host, 'jumpto', h_star0, 'force')
        host_back = wait(lambda: context(host) == 'system' and space_star(host) == h_star0, 15, 0.5)
        ck(bool(host_back),
           f'host entered its home star system {h_star0} (jumpto={j_host.strip()!r} ctx={context(host)} star={space_star(host)})')

        host.call('coop.rivals ally 1')
        wait(lambda: '0->1:ally' in rivals_out(guest), 10, 0.3)
        guest.call('coop.rivals accept 0')
        allied_ok = wait(
            lambda: '0-1=Ally' in rivals_out(host) and '0-1=Ally' in rivals_out(guest),
            12,
            0.3,
        )
        ck(bool(allied_ok), 'host and guest formed Alliance in Space Rivals')
        if not allied_ok:
            return ck.result()

        # Give guest a moment to receive host's updated system place (h_star0)
        time.sleep(1.5)

        # Perform the exact Jump to allied host's relinquished home star h_star0 (Alice/Carol crash path).
        # If guest crashes at 0x00BB0F22 inside EnterSolarSystem, guest.call() raises ConnectionError/timeout.
        jump_out = ''
        jump_alive = True
        try:
            jump_out = sp(guest, 'jump 0')
        except Exception as e:
            jump_alive = False
            jump_out = f'exception: {e}'

        arrived = False
        if jump_alive:
            try:
                arrived = bool(wait(lambda: space_star(guest) == h_star0 and context(guest) == 'system', 15, 0.5))
                time.sleep(2.0)
                # Confirm console still responsive after EnterSolarSystem + 2 s of system ticks
                _ = sp(guest, 'star')
            except Exception as e:
                jump_alive = False
                jump_out += f' / post-jump exception: {e}'

        shot(guest, 'space_rivals_jump_host_home.png') if jump_alive else None
        faults_end, exc_end = faults_of(guest)
        end_new_faults = (faults_end - faults_before) if (faults_end is not None and faults_before is not None) else 999
        has_jump_sig = '00BB0F22' in exc_end or '0106CA9F' in exc_end or '00FDC5FD' in exc_end
        ck(jump_alive and arrived and faults_end is not None and end_new_faults == 0 and not has_jump_sig,
           f'Bug B: allied Jump to host relinquished star {h_star0} entered system with 0 crashes/faults '
           f'(alive={jump_alive} arrived={arrived} out={jump_out.strip()!r} delta_faults={end_new_faults})')

    finally:
        pass
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
