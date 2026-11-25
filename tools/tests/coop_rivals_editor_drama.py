#!/usr/bin/env python3
"""WP-081-5 (BUG-1566): Rivals Drama Manager isolation & no guest world reload on host Creature Editor.

Exercises in real two-game Wine Creature Rivals:
  1. Part A (rivals.drama_isolate):
     - Guest Summon Flock (`coop.rules roar 33` -> K_FLOCK) is isolated on host: does NOT spawn a flock
       into the host's pack (`rivals_flock_isolated >= 1`, `relayed=0`, host pack unchanged).
     - Host meteor (kind 3) and rain (kind 5) run locally on host without relaying to rival guest
       (`relayed_tx=0`, `rivals_blocked` increases, guest stays `active != 1`, `relayed_rx=0`).
  2. Part B (rivals.editor_no_reload):
     - Host gains DNA to trigger brain growth + migration (`CRG_SpeciesMigrate`), enters Creature Editor,
       adds a part, exits editor, and confirms the migration caption.
     - With fix ON: host skips the post-editor world share (`editor_share_skipped >= 1`,
       `world_offers_sent` unchanged); guest NEVER leaves Creature stage (no Galaxy Menu, no load,
       `world_offers_recv` unchanged), keeps its own rival nest and DNA intact, and sees the host's
       evolved puppet live.
     - Twin OFF (`SPORE_FIX_OFF=rivals.drama_isolate,rivals.editor_no_reload`):
       guest flock joins host's pack, host meteor/rain relay onto guest, and host post-editor migration
       forces a full MSG_WORLD reload on guest -> MUST FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_rivals_editor_drama.py
"""
import re
import time

from coop_party_defence import pack
from coop_ui import Ui
from harness import field_dotall as field, mode, waiter
import twoinst
from twoinst import Checks, start

DONE = (1416, 1062)
CANCEL = (1383, 1062)
wait = waiter(step=0.5)


def crivals_out(con, sub=''):
    cmd = 'coop.crivals' + (f' {sub}' if sub else '')
    return con.call(cmd).get('out') or ''


def drama_out(con):
    return con.call('coop.drama').get('out') or ''


def drama_summary(con):
    out = drama_out(con)
    m = re.search(
        r'flock_relay=(\d+) relayed=(\d+) refused=(\d+) updates=(\d+) '
        r'flock_event=(\S+) forgotten=(\d+) shared=(\d+) host_sends=(\d+) '
        r'rivals_isolate=(\d+) rivals_blocked=(\d+) rivals_flock_isolated=(\d+)',
        out,
    )
    if not m:
        return None
    return {
        'relayed': int(m.group(2)),
        'refused': int(m.group(3)),
        'flock_event': m.group(5),
        'rivals_isolate': int(m.group(9)),
        'rivals_blocked': int(m.group(10)),
        'rivals_flock_isolated': int(m.group(11)),
    }


def drama_kind(con, name):
    out = drama_out(con)
    m = re.search(
        rf'^\s*{name}\s+started=(\d+) refused=(\d+) event=\S+ active=(-?\d+) relayed_tx=(\d+) relayed_rx=(\d+)',
        out,
        re.M,
    )
    if not m:
        return None
    return {
        'started': int(m.group(1)),
        'refused': int(m.group(2)),
        'active': int(m.group(3)),
        'relayed_tx': int(m.group(4)),
        'relayed_rx': int(m.group(5)),
    }


def world_stats(con):
    st = con.call('coop.status').get('out') or ''
    m_off = re.search(r'world_offers_sent=(\d+) world_offers_recv=(\d+)', st)
    m_skip = re.search(r'rivals_editor_no_reload: fix=(\d+) editor_share_skipped=(\d+)', st)
    return {
        'offers_sent': int(m_off.group(1)) if m_off else -1,
        'offers_recv': int(m_off.group(2)) if m_off else -1,
        'fix': int(m_skip.group(1)) if m_skip else -1,
        'skipped': int(m_skip.group(2)) if m_skip else -1,
    }


def world_loads(con):
    return (con.call('log 400').get('out') or '').count('world: loading ')


def cinematic(con):
    m = re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status').get('out') or '')
    return m.group(1) if m else None


def in_editor(con):
    return 'Editor' in mode(con)


def editor_ui(con):
    return (con.call('coop.ui tree 4').get('out') or '').count('Window') > 15


def avatar_key(con):
    return field(con.call('coop.avatar').get('out') or '', r'key=0x([0-9A-F]{8})')


def host_puppet_info(guest):
    st = guest.call('coop.status').get('out') or ''
    m = re.search(
        r"remote 0 '[^']*' puppet=(\S+) state=\S+ [^\n]*\n\s+puppet pos=[^\n]*species=([0-9A-F]{8})",
        st,
    )
    return (m.group(1), m.group(2)) if m else (None, None)


def add_part(con, y):
    con.call(f'move 86 {y}')
    time.sleep(0.3)
    con.call(f'button 86 {y} down')
    time.sleep(0.3)
    for x, yy in ((300, 250), (600, 400), (850, 500), (930, 560)):
        con.call(f'move {x} {yy}')
        time.sleep(0.2)
    time.sleep(0.5)
    con.call('button 930 560 up')
    time.sleep(1.0)


def clear_cinematics(con, secs=10.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        c = cinematic(con)
        if not c:
            return True
        con.call('key enter')
        time.sleep(1.0)
    return cinematic(con) is None


def main():
    twoinst.forget_settings()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(2.5)

    # 1. Enable Creature Rivals and wait for guest nest assignment + perspective swap
    host.call('coop.rivals mode creature rivals')
    host.call('coop.rivals distance near')
    h_st = wait(lambda: crivals_out(host) if 'active=1' in crivals_out(host) else None, 12.0)
    g_st = wait(
        lambda: crivals_out(guest) if ('active=1' in crivals_out(guest) and 'persp=1' in crivals_out(guest)) else None,
        15.0,
    )
    ck(bool(h_st) and 'active=1' in (h_st or ''), 'host Creature Rivals active')
    ck(bool(g_st) and 'persp=1' in (g_st or ''), 'guest Creature Rivals nest & perspective ready')
    if not h_st or not g_st:
        return ck.result()

    # Set distinct guest DNA so we verify guest progress survives host editor untouched
    guest.call('coop.crivals dna 175')
    time.sleep(1.0)
    m_nest0 = re.search(r'target=\(([^)]+)\) avatar_herd=\S+ avatar_nest=\(([^)]+)\)', crivals_out(guest))
    guest_nest0 = m_nest0.group(1) if m_nest0 else None
    print(f'  baseline: guest_nest=({guest_nest0}), host_drama={drama_summary(host)}', flush=True)

    # ---------------- Part A: Drama Manager isolation in Creature Rivals ----------------
    # A1: Guest Summon Flock (K_FLOCK) must NOT join the host's pack in Creature Rivals
    p0 = {m['ptr'] for m in pack(host) if not m['dead']}
    ds0 = drama_summary(host)
    r_flock = (guest.call('coop.rules roar 33').get('out') or '').strip()
    print(f'  A1 guest Summon Flock trigger: {r_flock}', flush=True)
    time.sleep(4.0)
    p1 = {m['ptr'] for m in pack(host) if not m['dead']}
    ds1 = drama_summary(host)
    print(f'  A1 host pack {len(p0)} -> {len(p1)}, drama {ds0} -> {ds1}', flush=True)
    ck(bool(ds1) and ds1['rivals_flock_isolated'] >= 1 and ds1['relayed'] == (ds0['relayed'] if ds0 else 0),
       f'A1 rival guest Summon Flock isolated on host (flock_isolated={ds1["rivals_flock_isolated"] if ds1 else -1}, '
       f'relayed={ds1["relayed"] if ds1 else -1})')
    ck(not (p1 - p0), f'A1 host pack did not absorb rival guest flock (added={len(p1 - p0)})')

    # A2: Host meteor (3) and rain (5) stay host-local and do NOT relay to rival guest
    blk_before = ds1['rivals_blocked'] if ds1 else 0
    for k, kname in ((3, 'meteor'), (5, 'rain')):
        host.call(f'coop.drama start {k}')
        he = wait(lambda kn=kname: drama_kind(host, kn) if (drama_kind(host, kn) or {}).get('started', 0) > 0 else None, 4.0)
        time.sleep(2.5)
        ge = drama_kind(guest, kname)
        print(f'  A2 host {kname}: host={he}, guest={ge}', flush=True)
        ck(bool(he) and he['started'] >= 1 and he['relayed_tx'] == 0,
           f'A2 host {kname} ran locally without relaying (host={he})')
        ck(ge is None or (ge['active'] != 1 and ge['relayed_rx'] == 0),
           f'A2 rival guest did not receive/start host {kname} (guest={ge})')
    ds2 = drama_summary(host)
    ck(bool(ds2) and ds2['rivals_blocked'] >= blk_before + 2,
       f'A2 host rivals_blocked incremented for meteor+rain ({blk_before} -> {ds2["rivals_blocked"] if ds2 else -1})')

    # ---------------- Part B: Host Creature Editor does NOT reload rival guest ----------------
    ws_h0 = world_stats(host)
    ws_g0 = world_stats(guest)
    loads_g0 = world_loads(guest)
    h_sp0 = avatar_key(host)
    print(f'  B before editor: host_sp={h_sp0}, host_ws={ws_h0}, guest_ws={ws_g0}, guest_loads={loads_g0}', flush=True)

    # Give host enough DNA to reach next brain level so Creature Editor triggers CRG_SpeciesMigrate
    host.call('coop.dna add 250')
    time.sleep(2.5)
    clear_cinematics(host, 15.0)

    # Open host Creature Editor (immediate under core.editor_now; vote fallback handled cleanly)
    v_out = (host.call('coop.vote evolve').get('out') or '').strip()
    print(f'  B host vote evolve: {v_out}', flush=True)
    if 'vote active' in (host.call('coop.vote').get('out') or ''):
        gu = Ui(guest, 'guest')
        if gu.wait(lambda s: s['ready']['vis'], 8, 'ready button'):
            gu.click('ready')

    ck(wait(lambda: in_editor(host), 45.0), 'B host entered Creature Editor')
    if not in_editor(host):
        ck.faults(host=host, guest=guest)
        return ck.result()

    if wait(lambda: in_editor(guest), 4.0):
        # If a vote opened guest's editor too, cancel it immediately
        wait(lambda: editor_ui(guest), 30.0)
        guest.call('click %d %d' % CANCEL)

    ck(wait(lambda: editor_ui(host), 60.0), 'B host Creature Editor UI loaded')
    host.call('wait.frames 150 60000')
    time.sleep(2.0)

    # Add a part on host so host gets a genuinely new species key
    dna_in = float(field(host.call('coop.dna').get('out') or '', r'dna=(-?[\d.]+)') or 0)
    add_part(host, 780)
    if float(field(host.call('coop.dna').get('out') or '', r'dna=(-?[\d.]+)') or 0) == dna_in:
        add_part(host, 170)
    time.sleep(1.5)
    host.call('click %d %d' % DONE)

    # Monitor guest continuously while host exits editor, plays/confirms migration caption, and settles
    modes_seen = []
    back_at = None
    cine_seen = False
    t0 = time.time()
    while time.time() - t0 < 120.0:
        gm, hm = mode(guest), mode(host)
        if not modes_seen or modes_seen[-1] != gm:
            modes_seen.append(gm)
            print(f'  B {time.time() - t0:5.1f} s guest mode={gm}, host mode={hm}', flush=True)
        hc = cinematic(host)
        if hc:
            cine_seen = True
            host.call('key enter')
        if back_at is None and 'Creature' in hm and host.call('coop.avatar')['ok'] and not hc:
            back_at = time.time()
            print(f'  B {time.time() - t0:5.1f} s host back in Creature stage (cine_seen={cine_seen})', flush=True)
        if back_at and time.time() - back_at > 20.0:
            break
        time.sleep(0.5)

    ck(back_at is not None, 'B host returned from Creature Editor to Creature stage')

    ws_h1 = world_stats(host)
    ws_g1 = world_stats(guest)
    loads_g1 = world_loads(guest)
    h_sp1 = avatar_key(host)
    g_st_after = crivals_out(guest)
    g_dna_m = re.search(r'dna=(\d+)', g_st_after)
    g_dna_after = int(g_dna_m.group(1)) if g_dna_m else -1
    m_nest1 = re.search(r'target=\(([^)]+)\) avatar_herd=\S+ avatar_nest=\(([^)]+)\)', g_st_after)
    guest_nest1 = m_nest1.group(2) if m_nest1 else None
    left_stage = [m for m in modes_seen if 'Creature' not in m and 'Editor' not in m]

    print(
        f'  B after editor: host_sp {h_sp0}->{h_sp1}, host_ws={ws_h1}, guest_ws={ws_g1}, '
        f'guest_loads {loads_g0}->{loads_g1}, guest_modes={modes_seen}, guest_dna={g_dna_after}, '
        f'guest_nest=({guest_nest1})',
        flush=True,
    )

    ck(bool(h_sp1) and h_sp1 != h_sp0, f'B host evolved to new species ({h_sp0} -> {h_sp1})')
    ck(ws_h1['skipped'] > ws_h0['skipped'],
       f'B host skipped post-editor world share in Creature Rivals (editor_share_skipped {ws_h0["skipped"]} -> {ws_h1["skipped"]})')
    ck(ws_h1['offers_sent'] == ws_h0['offers_sent'],
       f'B host sent no MSG_WORLD_OFFER after editor ({ws_h0["offers_sent"]} -> {ws_h1["offers_sent"]})')
    ck(ws_g1['offers_recv'] == ws_g0['offers_recv'] and loads_g1 == loads_g0 and not left_stage,
       f'B guest did not reload world or leave Creature stage (offers {ws_g0["offers_recv"]}->{ws_g1["offers_recv"]}, '
       f'loads {loads_g0}->{loads_g1}, left={left_stage})')
    ck(guest_nest1 == guest_nest0 and g_dna_after == 175,
       f'B guest kept own rival nest ({guest_nest0} -> {guest_nest1}) and own DNA ({g_dna_after})')

    # Verify guest still sees host puppet live (and receives host's evolved species bundle)
    pup = wait(
        lambda: host_puppet_info(guest)
        if (host_puppet_info(guest)[0] and host_puppet_info(guest)[0] != '00000000')
        else None,
        15.0,
    )
    print(f'  B guest view of host puppet: {pup}', flush=True)
    ck(bool(pup) and pup[0] != '00000000', f'B guest still sees host puppet live after editor ({pup})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
