#!/usr/bin/env python3
"""WP-081-1 (BUG-1562): Rivals designs.cpp isolation and Space Jump-to ally guard.

Two-game Wine lab test covering both parts of BUG-1562:
  Part 1 (Space Rivals):
    - Enable Space Rivals and wait for guest homeworld emigration (guest in a different star).
    - In Neutral relation: both `coop.space jump 1` (explicit rival) and `coop.space jump` (any partner)
      are refused with "cannot jump to a rival", host stays at its own star, and `jump_rivals_blocked` rises.
    - In War relation: `coop.space jump 1` (host) and `coop.space jump 0` (guest) are both refused,
      both ships stay in their own star systems, and `jump_rivals_blocked` rises on both games.
    - After Peace -> Ally (`0-1=Ally`): `coop.space jump 1` succeeds and host's ship moves into guest's star.
  Part 2 (Tribe Rivals):
    - Enable Tribe Rivals so host takes over an NPC tribe for guest and guest perspective-swaps to it.
    - Guest's `player_tribe()` has a distinct species profile key from host's `player_tribe()`.
    - Across wsync ticks, `designs.cpp` skips applying host's `tribe_outfit` onto guest's rival tribe:
      guest's tribe profile key stays untouched (`!= host_key`), `applied=0`, `waits=0`, and `rivals_skipped > 0`.

  Twin (`rivals_designs_jump_off` with `SPORE_FIX_OFF=rivals.designs_isolate,rivals.space_jump_ally`):
    - Neutral jump succeeds instead of being blocked, and Tribe Rivals outfit sync does not skip -> FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_rivals_designs_jump.py
"""
import re
import time

from harness import sp, waiter
import twoinst
from twoinst import Checks, start

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


def space_star(con):
    out = sp(con, 'star')
    m = re.search(r'\bstar\s+(\d+)', out)
    return int(m.group(1)) if m else 0


def space_stat(con, key):
    out = con.call('coop.space').get('out') or ''
    m = re.search(rf'\b{re.escape(key)}=(\d+)', out)
    return int(m.group(1)) if m else 0


def tribe_rivals_out(con):
    return con.call('coop.tribe rivals').get('out') or ''


def designs_out(con):
    return con.call('coop.designs').get('out') or ''


def designs_val(con, key):
    out = designs_out(con)
    m = re.search(rf'\b{re.escape(key)}=(\d+)', out)
    return int(m.group(1)) if m else 0


def tribe_profile_key(con):
    out = designs_out(con)
    m = re.search(r'tribe profile \S+ key=([0-9A-Fa-f]{8})/', out)
    return m.group(1).upper() if m else ''


def run_space_jump_phase(ck):
    twoinst.forget_settings()
    host, guest = start('space')
    try:
        time.sleep(3.5)
        h_init = sr_status(host)
        h_star0 = int(h_init.get('home_star', '0'))
        ck(h_star0 != 0, f'space: initial host home star detected ({h_star0})')

        host.call('coop.rivals preset all_together')
        host.call('coop.rivals fasttimers 1')
        host.call('coop.rivals mode space rivals')

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
        g_star = int(sr_status(guest).get('home_star', '0'))
        ck(bool(ok_emig), f'space: guest emigrated to distinct star (host={h_star0}, guest={g_star})')
        if not ok_emig:
            return

        # Wait briefly for host to receive guest's Space place at g_star and Neutral relation sync
        wait(
            lambda: '0-1=Neutral' in rivals_out(host)
            and '0-1=Neutral' in rivals_out(guest)
            and riv_space(host)['rel'].get(1) == 'neutral',
            12,
            0.3,
        )
        time.sleep(2.0)

        # 1. Neutral rival: explicit jump(1) and any-partner jump(0xFF) must both be blocked
        h_blk0 = space_stat(host, 'jump_rivals_blocked')
        h_jmp0 = space_stat(host, 'jumps')
        res_n_exp = sp(host, 'jump 1').strip()
        res_n_any = sp(host, 'jump').strip()
        time.sleep(2.0)
        h_star_n = space_star(host)
        h_blk1 = space_stat(host, 'jump_rivals_blocked')
        h_jmp1 = space_stat(host, 'jumps')

        ck('cannot jump to a rival' in res_n_exp and 'cannot jump to a rival' in res_n_any and h_star_n == h_star0,
           f'space Neutral: Jump to rival refused and host stayed at star {h_star0} (exp={res_n_exp!r}, any={res_n_any!r}, star={h_star_n})')
        ck(h_blk1 >= h_blk0 + 2 and h_jmp1 == h_jmp0,  # counter: confirms rival jump block counter alongside unchanged host star
           f'space Neutral: jump_rivals_blocked rose ({h_blk0}->{h_blk1}) with zero jumps ({h_jmp0}->{h_jmp1})')

        # 2. War rival: both host->guest and guest->host jumps must be blocked
        host.call('coop.rivals war 1')
        war_ok = wait(
            lambda: '0-1=War' in rivals_out(host)
            and '0-1=War' in rivals_out(guest)
            and riv_space(host)['rel'].get(1) == 'war'
            and riv_space(guest)['rel'].get(0) == 'war',
            12,
            0.3,
        )
        ck(bool(war_ok), 'space: players transitioned to War')

        g_blk0 = space_stat(guest, 'jump_rivals_blocked')
        res_w_h = sp(host, 'jump 1').strip()
        res_w_g = sp(guest, 'jump 0').strip()
        time.sleep(2.0)
        h_star_w = space_star(host)
        g_star_w = space_star(guest)
        h_blk2 = space_stat(host, 'jump_rivals_blocked')
        g_blk1 = space_stat(guest, 'jump_rivals_blocked')

        ck('cannot jump to a rival' in res_w_h and h_star_w == h_star0,
           f'space War: host Jump to rival refused and host stayed at {h_star0} (res={res_w_h!r}, star={h_star_w})')
        ck('cannot jump to a rival' in res_w_g and g_star_w == g_star,
           f'space War: guest Jump to rival refused and guest stayed at {g_star} (res={res_w_g!r}, star={g_star_w})')
        ck(h_blk2 >= h_blk1 + 1 and g_blk1 >= g_blk0 + 1,  # counter: confirms War jump block counters alongside unchanged star IDs
           f'space War: jump_rivals_blocked rose on both (host {h_blk1}->{h_blk2}, guest {g_blk0}->{g_blk1})')

        # 3. Peace -> Ally: Jump to allied partner succeeds and moves ship to ally's star
        guest.call('coop.rivals peace 0')
        wait(lambda: '1->0:peace' in rivals_out(host), 10, 0.3)
        host.call('coop.rivals accept 1')
        wait(lambda: '0-1=Neutral' in rivals_out(host) and '0-1=Neutral' in rivals_out(guest), 10, 0.3)

        host.call('coop.rivals ally 1')
        wait(lambda: '0->1:ally' in rivals_out(guest), 10, 0.3)
        guest.call('coop.rivals accept 0')
        ally_ok = wait(
            lambda: '0-1=Ally' in rivals_out(host)
            and '0-1=Ally' in rivals_out(guest)
            and riv_space(host)['rel'].get(1) == 'ally',
            12,
            0.3,
        )
        ck(bool(ally_ok), 'space: players became Allies')

        if ally_ok and space_star(host) != g_star:
            res_a = sp(host, 'jump 1').strip()
            arrived = wait(lambda: space_star(host) == g_star, 20, 0.5)
            ck('jumping' in res_a and bool(arrived),
               f'space Ally: host Jump to allied guest succeeded (res={res_a!r}, star={space_star(host)}, target={g_star})')
    finally:
        try:
            host.call('coop.rivals fasttimers 0')
        except Exception:
            pass


def run_tribe_designs_phase(ck):
    twoinst.forget_settings()
    host, guest = start('tribe')
    try:
        time.sleep(2.5)
        h_key0 = tribe_profile_key(host)
        ck(bool(h_key0) and h_key0 != '00000000', f'tribe: initial host tribe species key detected ({h_key0})')

        host.call('coop.rivals fasttimers 1')
        host.call('coop.rivals mode tribe rivals')

        h_ready = wait(
            lambda: 'active=1' in tribe_rivals_out(host) and 'assign pid=1 ' in tribe_rivals_out(host),
            15,
            0.4,
        )
        g_ready = wait(
            lambda: 'active=1 swapped=1' in tribe_rivals_out(guest),
            15,
            0.4,
        )
        ck(bool(h_ready) and bool(g_ready),
           f'tribe: Tribe Rivals active and guest perspective swapped (host={bool(h_ready)} guest={bool(g_ready)})')
        if not (h_ready and g_ready):
            return

        g_key0 = tribe_profile_key(guest)
        ck(bool(g_key0) and g_key0 != '00000000' and g_key0 != h_key0,
           f'tribe: guest rival tribe has distinct species profile key (host={h_key0}, guest={g_key0})')

        # Wait across multiple wsync outfit_sync ticks: with fix ON rivals_skipped increments immediately;
        # with fix OFF outfit_sync attempts to overwrite guest's rival tribe with host's outfit.
        skip_ok = wait(lambda: designs_val(guest, 'rivals_skipped') >= 2, 12, 0.5)
        g_out = designs_out(guest)
        g_key_after = tribe_profile_key(guest)
        g_applied = designs_val(guest, 'applied')
        g_waits = designs_val(guest, 'waits')
        g_skipped = designs_val(guest, 'rivals_skipped')

        ck(g_key_after == g_key0 and g_key_after != h_key0,
           f'tribe: guest rival tribe profile key stayed isolated from host outfit sync (before={g_key0}, after={g_key_after}, host={h_key0})')
        ck(bool(skip_ok) and g_skipped >= 2 and g_applied == 0 and g_waits == 0,  # counter: verifies outfit_sync skipped in Tribe Rivals alongside preserved profile key
           f'tribe: designs.cpp skipped host outfit sync cleanly (rivals_skipped={g_skipped}, applied={g_applied}, waits={g_waits}; {g_out.splitlines()[0] if g_out else ""})')
    finally:
        try:
            host.call('coop.rivals fasttimers 0')
        except Exception:
            pass


def main():
    ck = Checks()
    run_space_jump_phase(ck)
    run_tribe_designs_phase(ck)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
