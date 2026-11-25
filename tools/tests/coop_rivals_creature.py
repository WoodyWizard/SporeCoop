#!/usr/bin/env python3
"""WP-R3 (BUG-1547): Creature Rivals mode in real two-game Wine Creature stage.

Exercises:
  1. R2 Host nest & family herd takeover at BaseDistance (Near vs Far) + species swap to guest's own creature.
  2. R1 Guest perspective swap (own assigned nest becomes guest's avatar herd/nest, distinct from host's).
  3. Own DNA & own collected parts isolation (host parts unlock does NOT pool to guest; DNA stays independent).
  4. R4 Per-player wild species relations (guest impress/ally independent from host, shared wild extinction)
     + Creature influence (earned on social win, Buy charges own DNA, Protect blocks TurnAgainst,
     TurnAgainst shifts rival down, SmoothOver shifts actor up).
  5. War-gated PvP & R7 Home Nest Protection (Neutral blocks family hit; War allows family hit;
     killing guest's home species is blocked from extinction and home nest stays extinct=0).
  6. R3/R6 Brain-level win (brain=4) -> catch-up timer -> automatic Creature part draft ->
     guest picks winner's part and unlocks it natively -> winner claims escalating draft payout.
  7. Twin SPORE_FIX_OFF=rivals.creature (`rivals_creature_off`): disabled -> FAIL cleanly.

  SPORE_LANE=3 python3 tools/tests/coop_rivals_creature.py
"""
import re
import time

import twoinst
from twoinst import Checks, start


def wait_for(fn, timeout=15.0, step=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


def crivals_out(con, sub=''):
    cmd = 'coop.crivals' + (f' {sub}' if sub else '')
    return con.call(cmd).get('out') or ''


def rivals_out(con):
    return con.call('coop.rivals').get('out') or ''


def parts_count(con):
    out = con.call('coop.parts').get('out') or ''
    m = re.search(r'parts=(\d+):([0-9A-F]{8})', out)
    return (int(m.group(1)), m.group(2)) if m else (None, None)


def parse_nest(text, pid):
    m = re.search(
        rf'nest p={pid} pos=\(([^)]+)\) dist_from_host=([0-9.]+) sp=([0-9A-F]+) orig=([0-9A-F]+) '
        rf'swapped=(\d+) herd=([0-9A-Fx]+) members=(\d+) av_own=(\d+) extinct=(\d+)',
        text,
    )
    if not m:
        return None
    return {
        'pos': m.group(1),
        'dist': float(m.group(2)),
        'sp': m.group(3),
        'orig': m.group(4),
        'swapped': int(m.group(5)),
        'herd': m.group(6),
        'members': int(m.group(7)),
        'av_own': int(m.group(8)),
        'extinct': int(m.group(9)),
    }


def parse_rel(text, sp_hex):
    m = re.search(rf'sp={sp_hex} rel=(-?\d+) imp=(\d+) kill=(\d+)', text)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else None


def pick_wild_species(host, exclude_set):
    out = host.call('coop.social herds').get('out') or ''
    found = []
    for m in re.finditer(r'species=([0-9A-F]{8})', out):
        sp = m.group(1)
        if sp != '00000000' and sp not in exclude_set and sp not in found:
            found.append(sp)
    if len(found) < 3:
        out2 = host.call('coop.npc creatures').get('out') or ''
        for m in re.finditer(r'species=([0-9A-F]{8})', out2):
            sp = m.group(1)
            if sp != '00000000' and sp not in exclude_set and sp not in found:
                found.append(sp)
    return found


def main():
    twoinst.forget_settings()
    host, guest = start('creature')
    ck = Checks()
    try:
        time.sleep(2.5)

        # 1. Enable Creature Rivals + fast timers
        host.call('coop.rivals mode creature rivals')
        host.call('coop.rivals fasttimers 1')
        host.call('coop.rivals distance middle')

        # Wait for R2 host assignment & R1 guest perspective swap
        h_st = wait_for(lambda: crivals_out(host) if 'active=1' in crivals_out(host) else None, 10.0)
        g_st = wait_for(
            lambda: crivals_out(guest) if ('active=1' in crivals_out(guest) and 'persp=1' in crivals_out(guest)) else None,
            15.0,
        )
        ck(bool(h_st) and 'fix=1 active=1' in (h_st or ''),
           f'host Creature Rivals active ({(h_st or crivals_out(host)).splitlines()[0]})')
        ck(bool(g_st) and 'assigned=1' in (g_st or '') and 'persp=1' in (g_st or ''),
           f'guest received nest assignment & swapped perspective ({(g_st or crivals_out(guest)).strip()})')
        if not h_st or not g_st:
            return ck.result()

        # Verify Near vs Far BaseDistance selection on host
        host.call('coop.rivals distance near')
        time.sleep(0.8)
        n_near = parse_nest(crivals_out(host, 'nests'), 1)
        host.call('coop.rivals distance far')
        time.sleep(0.8)
        n_far = parse_nest(crivals_out(host, 'nests'), 1)
        ck(bool(n_near) and bool(n_far) and n_far['dist'] > n_near['dist'] + 10.0,
           f'BaseDistance Near ({n_near["dist"] if n_near else -1:.1f} m) < Far ({n_far["dist"] if n_far else -1:.1f} m)')

        # Use Near nest so guest family & avatar are well loaded for familyhit check
        host.call('coop.rivals distance near')
        time.sleep(1.5)
        nests_h = crivals_out(host, 'nests')
        n0 = parse_nest(nests_h, 0)
        n1 = parse_nest(nests_h, 1)
        ck(bool(n0) and bool(n1) and n1['dist'] >= 20.0 and n1['extinct'] == 0,
           f'host assigned distinct guest nest (dist={n1["dist"] if n1 else -1:.1f} m, sp={n1["sp"] if n1 else "?"}, members={n1["members"] if n1 else 0})')

        # Verify guest perspective swap landed avatar_nest on its own assigned target nest
        g_st2 = wait_for(
            lambda: crivals_out(guest) if 'persp=1' in crivals_out(guest) else None,
            10.0,
        )
        m_own = re.search(r'target=\(([^)]+)\) avatar_herd=([0-9A-Fx]+) avatar_nest=\(([^)]+)\)', g_st2 or '')
        ck(bool(m_own) and m_own.group(1) == m_own.group(3) and m_own.group(1) != (n0['pos'] if n0 else ''),
           f'guest avatar_nest matches own assigned nest ({m_own.group(3) if m_own else "?"}) != host nest ({n0["pos"] if n0 else "?"})')

        # 2. Own DNA & own collected parts isolation (never pooled in Creature Rivals)
        hp0, _ = parts_count(host)
        gp0, _ = parts_count(guest)
        host.call('coop.parts unlock 2')
        time.sleep(3.0)
        hp1, _ = parts_count(host)
        gp1, _ = parts_count(guest)
        ck(hp0 is not None and hp1 == hp0 + 2 and gp1 == gp0,
           f'parts isolated in Creature Rivals (host {hp0}->{hp1}, guest stayed {gp0}->{gp1})')

        host.call('coop.crivals dna 260')
        guest.call('coop.crivals dna 115')
        time.sleep(2.5)
        h_dna_m = re.search(r'dna=(\d+)', crivals_out(host))
        g_dna_m = re.search(r'dna=(\d+)', crivals_out(guest))
        ck(bool(h_dna_m) and bool(g_dna_m) and int(h_dna_m.group(1)) == 260 and int(g_dna_m.group(1)) == 115,
           f'own DNA isolated (host={h_dna_m.group(1) if h_dna_m else "?"}, guest={g_dna_m.group(1) if g_dna_m else "?"})')

        # 3. R4 Per-player wild species relations & Creature influence
        exclude = {n0['sp'], n1['sp'], n1['orig']} if n0 and n1 else set()
        wild = pick_wild_species(host, exclude)
        # Fallback synthetic wild species keys if planet scan returned fewer than 3
        while len(wild) < 3:
            wild.append(f'06AA000{len(wild) + 1}')
        sp1, sp2, sp3 = wild[0], wild[1], wild[2]

        # Guest impresses sp1 x3 -> Allied (rel=6) for guest ONLY, earns influence (+10+10+35 = +55)
        for _ in range(3):
            host.call(f'coop.crivals impress 1 {sp1}')
        g_rel_ally = wait_for(
            lambda: parse_rel(crivals_out(guest, 'rels'), sp1)
            if (parse_rel(crivals_out(guest, 'rels'), sp1) or (0,))[0] == 6
            else None,
            10.0,
        )
        h_rel_sp1 = parse_rel(crivals_out(host, 'rels 0'), sp1)
        ck(g_rel_ally is not None and g_rel_ally[0] == 6 and (h_rel_sp1 is None or h_rel_sp1[0] != 6),
           f'per-player wild species independence: guest allied {sp1} ({g_rel_ally}), host untouched ({h_rel_sp1})')

        # Influence earned on guest + Buy influence charges host's own DNA (260 -> 210)
        inf_after_imp = wait_for(
            lambda: rivals_out(host) if re.search(r'bal=\[\d+,([1-9]\d*),', rivals_out(host)) else None,
            6.0,
        )
        ck(bool(inf_after_imp), 'guest earned Creature influence on social successes')

        buy_out = host.call('coop.rivals influence buy').get('out') or ''
        h_dna_after_buy = re.search(r'dna=(\d+)', crivals_out(host))
        ck('ok' in buy_out and h_dna_after_buy and int(h_dna_after_buy.group(1)) == 210,
           f'Buy influence charged host own DNA 260->210 ({buy_out.strip()}, dna={h_dna_after_buy.group(1) if h_dna_after_buy else "?"})')

        # Give host enough influence to test TurnAgainst vs Protect
        host.call('coop.rivals influence add 0 100')
        guest.call(f'coop.rivals influence protect 0x{sp1}')
        wait_for(lambda: 'prots=1' in rivals_out(host), 6.0)
        turn_prot = host.call(f'coop.rivals influence turn 0x{sp1} 1').get('out') or ''
        ck('error:' in turn_prot and 'protected' in turn_prot,
           f'TurnAgainst blocked on protected wild species {sp1} ({turn_prot.strip()})')

        # Impress guest on unprotected sp2 to Allied (6), then host TurnAgainst -> Friendly (4), then guest SmoothOver -> Allied (6)
        for _ in range(3):
            host.call(f'coop.crivals impress 1 {sp2}')
        wait_for(lambda: (parse_rel(crivals_out(guest, 'rels'), sp2) or (0,))[0] == 6, 8.0)
        turn_ok = host.call(f'coop.rivals influence turn 0x{sp2} 1').get('out') or ''
        g_after_turn = wait_for(
            lambda: parse_rel(crivals_out(guest, 'rels'), sp2)
            if (parse_rel(crivals_out(guest, 'rels'), sp2) or (0,))[0] == 4
            else None,
            8.0,
        )
        ck('ok' in turn_ok and g_after_turn is not None and g_after_turn[0] == 4,
           f'TurnAgainst shifted rival guest {sp2} from 6 -> 4 ({turn_ok.strip()}, guest={g_after_turn})')

        guest.call(f'coop.rivals influence smooth 0x{sp2}')
        g_after_smooth = wait_for(
            lambda: parse_rel(crivals_out(guest, 'rels'), sp2)
            if (parse_rel(crivals_out(guest, 'rels'), sp2) or (0,))[0] == 6
            else None,
            8.0,
        )
        ck(g_after_smooth is not None and g_after_smooth[0] == 6,
           f'SmoothOver shifted guest {sp2} back up 4 -> 6 (guest={g_after_smooth})')

        # Shared wild extinction across players: 2 kills by host + 1 kill by guest -> rel=5 on BOTH
        host.call(f'coop.crivals kill 0 {sp3}')
        host.call(f'coop.crivals kill 0 {sp3}')
        k3 = host.call(f'coop.crivals kill 1 {sp3}').get('out') or ''
        g_ext = wait_for(
            lambda: parse_rel(crivals_out(guest, 'rels'), sp3)
            if (parse_rel(crivals_out(guest, 'rels'), sp3) or (0,))[0] == 5
            else None,
            8.0,
        )
        h_ext = parse_rel(crivals_out(host, 'rels 0'), sp3)
        ck('extinct' in k3 and h_ext and h_ext[0] == 5 and g_ext and g_ext[0] == 5,
           f'shared wild extinction synced to both host ({h_ext}) and guest ({g_ext})')

        # 4. War-gated PvP & R7 Home Nest Protection
        # Neutral: host cannot hurt guest's family member
        fh_neu = host.call('coop.crivals familyhit 1 3').get('out') or ''
        m_neu = re.search(r'hp ([0-9.]+) -> ([0-9.]+)', fh_neu)
        ck(bool(m_neu) and abs(float(m_neu.group(1)) - float(m_neu.group(2))) < 0.05,
           f'Neutral blocks rival family damage ({fh_neu.strip()})')

        # Declare War -> host CAN hurt guest's family member
        host.call('coop.rivals war 1')
        wait_for(lambda: '0-1=War' in rivals_out(host) and '0-1=War' in rivals_out(guest), 8.0)
        fh_war = host.call('coop.crivals familyhit 1 3').get('out') or ''
        m_war = re.search(r'hp ([0-9.]+) -> ([0-9.]+)', fh_war)
        ck(bool(m_war) and float(m_war.group(2)) < float(m_war.group(1)) - 0.5,
           f'War allows rival family damage ({fh_war.strip()})')

        # R7 Home Nest Protection: attempting to make guest's home species extinct is blocked
        ext_home = host.call(f'coop.crivals kill 0 {n1["sp"]}').get('out') or ''
        n1_after = parse_nest(crivals_out(host, 'nests'), 1)
        ck('blocked' in ext_home and n1_after and n1_after['extinct'] == 0,
           f'R7 home nest species extinction blocked ({ext_home.strip()}, extinct={n1_after["extinct"] if n1_after else "?"})')

        # 5. R3/R6 Brain-level win -> catch-up -> automatic Creature part draft -> guest copies & unlocks part -> payout
        host.call('coop.crivals brain 4')
        win_cu = wait_for(
            lambda: rivals_out(guest) if ('winner=0' in rivals_out(guest) and 'catchup=1' in rivals_out(guest)) else None,
            10.0,
        )
        ck(bool(win_cu), 'brain_level=4 triggered Creature race win and catch-up timer on both host and guest')

        # Wait for fast catch-up (3 s) to end and automatically launch Creature part draft
        draft_up = wait_for(
            lambda: rivals_out(guest) if ('draft: active=1 winner=0' in rivals_out(guest)) else None,
            10.0,
        )
        ck(bool(draft_up), 'catch-up expiry automatically started Creature part draft on host and guest')

        gp_before, _ = parts_count(guest)
        guest.call('coop.rivals draft pick 1')
        picked_ok = wait_for(
            lambda: ('total_picks=1 payout=100' in rivals_out(host)
                     and 'draft_unlock=1' in crivals_out(guest)),
            10.0,
        )
        gp_after, _ = parts_count(guest)
        ck(bool(picked_ok) and gp_before is not None and gp_after == gp_before + 1,
           f'guest draft pick unlocked copied Creature part natively ({gp_before}->{gp_after}) and credited winner payout=100')

        payout_out = host.call('coop.rivals draft payout 0').get('out') or ''
        ck('payout=100' in payout_out, f'winner claimed Creature draft payout ({payout_out.strip()})')

        host.call('coop.rivals fasttimers 0')
    finally:
        pass
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
