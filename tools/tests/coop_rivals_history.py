#!/usr/bin/env python3
"""BUG-1557: Rivals per-player Tribe/Civ consequence cards and independent Space philosophy/archetype.

Space fixture, two players:
  1. Connect host and guest in Space; seed full host stage cards [Cell, Creature, Tribe, Civ] if the Space
     fixture started with empty earlier-stage cards so both players start from identical Cell+Creature cards.
  2. Enable Rivals mode for Tribe, Civ, and Space (`coop.rivals mode tribe/civ/space rivals`) and wait for
     Space Rivals guest homeworld emigration (`home_planet` on guest != host's original homeworld) so we verify
     clean coordination between emigration and `SpaceGame_SetArchetype`.
  3. Query Spore's real `SpaceArchetype_FromTraits` (`coop.history space`) to pick a divergent guest
     `(Tribe, Civ)` pair whose Space archetype differs from the host's (while Cell and Creature stay identical).
  4. Record the guest's divergent Tribe (`coop.history tribe <gid> ...`) and Civ (`coop.history civ_start` /
     `coop.history civ <gid> ...`) outcomes on the host.
  5. Verify in real game state (`cPlayer::GetConsequenceTrait` + native `cEmpire +0x58`):
     - Guest's `cPlayer` Tribe card, Civ card, and Space card all diverge from the host's and match expected IDs.
     - Guest's native `cEmpire +0x58` archetype index switches to the guest's own Space archetype (after
       emigration) while the host's `cPlayer` cards and `cEmpire +0x58` remain unchanged.
  6. Verify Civ tie-breaking (`civ_start_spec`) and live recomputation when a second Civ specialty overtakes
     the tally, updating the guest's `cPlayer` Civ card.
  7. Guest leaves and rejoins (`coop.leave` / `lane.JOIN`): verify the guest keeps its own Tribe, Civ, Space
     cards and native `cEmpire +0x58` archetype index across rejoin.
  8. `SPORE_FIX_OFF=rivals.own_history` twin (`rivals_history_off`): guest inherits host's Tribe and Civ
     cards and keeps the host's Space card and archetype index -> FAILS on the divergence assertions.

  SPORE_LANE=2 python3 tools/tests/coop_rivals_history.py
"""
import re
import time

import lane
import twoinst
from harness import sp, waiter
from twoinst import Checks, keyframe, start

wait = waiter(timeout=25.0, step=0.5)

# Consequence card IDs (history_logic.h)
CELL_CARN = 'CFB01B93'
CREAT_AGGR = '17E5EF84'
TRIBE_AGGR = '0CAC124D'
TRIBE_FRIEND = '60A78928'
TRIBE_INDUS = 'C2CA9495'
CIV_MIL = '0A35D0F5'
CIV_ECO = '3360727F'
CIV_REL = '6AEB96A1'


def read_history_game(con):
    """Read real game state via `coop.history`: ([5 hex cards], social, combat, empire_arch_idx)."""
    out = con.call('coop.history').get('out') or ''
    m = re.search(r'game cards ((?:[0-9A-F]{8} ?){5}) social=(-?[\d.]+) combat=(-?[\d.]+)', out)
    a = re.search(r'empire archetype=(-?\d+)', out)
    if not m:
        return [], 0.0, 0.0, None
    return m.group(1).split(), float(m.group(2)), float(m.group(3)), (int(a.group(1)) if a else None)


def guest_id(guest):
    m = re.search(r'client id=(\d+)', guest.call('coop.status').get('out') or '')
    return int(m.group(1)) if m else 1


def guest_owns_history(guest):
    return 'owns=1' in (guest.call('coop.history').get('out') or '')


def sr_home_planet(con):
    out = sp(con, 'rivals status')
    m = re.search(r'home_planet=([0-9A-Fa-f]+)', out)
    return m.group(1).upper() if m else '00000000'


def query_space_card(host, c0, c1, c2, c3):
    out = host.call(f'coop.history space {c0} {c1} {c2} {c3}').get('out') or ''
    m = re.search(r'space card ([0-9A-F]{8}) index (-?\d+)', out)
    return (m.group(1), int(m.group(2))) if m else (None, -1)


def log_state(label, host, guest):
    h, g = read_history_game(host), read_history_game(guest)
    print(f'  [{label}] host cards={h[0]} arch={h[3]} | guest cards={g[0]} arch={g[3]}', flush=True)
    return h, g


def main():
    twoinst.forget_settings()
    host, guest = start('space')
    ck = Checks()
    try:
        time.sleep(4.0)
        ck(wait(lambda: guest_owns_history(guest), 30.0),
           "guest's own stage history is active (record arrived from host)")
        time.sleep(1.5)

        h0, g0 = log_state('initial', host, guest)
        ck(len(h0[0]) == 5 and len(g0[0]) == 5 and h0[3] is not None and g0[3] is not None,
           f'both Space games report 5 cPlayer cards and empire archetype (host={h0[3]} guest={g0[3]})')
        if len(h0[0]) != 5 or len(g0[0]) != 5:
            return ck.result()

        # Seed host stages 0..3 if the Space fixture started with empty earlier cards
        if any(c == '00000000' for c in h0[0][:4]):
            for st_idx, card in enumerate((CELL_CARN, CREAT_AGGR, TRIBE_AGGR, CIV_MIL)):
                if h0[0][st_idx] == '00000000':
                    host.call(f'coop.history set 0 {st_idx} {card}')
            wait(lambda: all(c != '00000000' for c in (read_history_game(host)[0] or ['00000000'] * 5)[:4]), 10.0)
            time.sleep(2.0)
            h0, g0 = log_state('seeded host', host, guest)

        ck(all(c != '00000000' for c in h0[0]), f'host has all 5 stage cards ({h0[0]})')
        host_cards0 = h0[0]
        guest_cards0 = g0[0]
        host_arch_idx0 = h0[3]
        h_planet0 = sr_home_planet(host)

        # Enable Rivals for Tribe, Civ, and Space -> wait for Space Rivals guest homeworld emigration
        host.call('coop.rivals mode tribe rivals')
        host.call('coop.rivals mode civ rivals')
        host.call('coop.rivals mode space rivals')
        emigrated = wait(
            lambda: sr_home_planet(guest) not in ('00000000', h_planet0),
            30.0,
        )
        g_planet1 = sr_home_planet(guest)
        print(f'  [emigration] host_home={h_planet0} guest_home={g_planet1}', flush=True)
        ck(emigrated, f'Space Rivals guest emigrated to its own homeworld ({h_planet0} -> {g_planet1})')

        # Choose a (Tribe, Civ) outcome plan that differs from host's Tribe & Civ cards and yields a
        # different Space archetype via the game's real SpaceArchetype_FromTraits for the guest's Cell & Creature.
        candidates = [
            (TRIBE_FRIEND, ['ally', 'ally'], CIV_REL, 'rel', ['rel', 'rel'], CIV_ECO, 'eco'),
            (TRIBE_INDUS, ['ally', 'conquer'], CIV_ECO, 'eco', ['eco', 'eco'], CIV_REL, 'rel'),
            (TRIBE_FRIEND, ['ally', 'ally'], CIV_ECO, 'eco', ['eco', 'eco'], CIV_REL, 'rel'),
            (TRIBE_AGGR, ['conquer', 'conquer'], CIV_MIL, 'mil', ['mil', 'mil'], CIV_ECO, 'eco'),
        ]
        chosen = None
        for t_card, t_acts, c_card, c_start, c_acts, alt_c_card, alt_c_arg in candidates:
            if t_card == host_cards0[2] or c_card == host_cards0[3]:
                continue
            sp_card, sp_idx = query_space_card(host, guest_cards0[0], guest_cards0[1], t_card, c_card)
            if sp_card and sp_idx >= 0 and sp_card != host_cards0[4] and sp_idx != host_arch_idx0:
                chosen = (t_card, t_acts, c_card, c_start, c_acts, sp_card, sp_idx, alt_c_card, alt_c_arg)
                break

        ck(chosen is not None,
           f'found divergent (Tribe, Civ) pair with distinct Space archetype from host ({host_cards0[4]} idx={host_arch_idx0})')
        if not chosen:
            return ck.result()

        want_t, t_acts, want_c, c_start, c_acts, want_sp, want_arch_idx, alt_c_card, alt_c_arg = chosen
        gid = guest_id(guest)
        print(f'  [plan] gid={gid} want tribe={want_t} civ={want_c} -> space={want_sp} (idx={want_arch_idx})', flush=True)

        # Record guest's own Tribe and Civ Rivals outcomes on host
        for act in t_acts:
            print('  ', (host.call(f'coop.history tribe {gid} {act}').get('out') or '').strip(), flush=True)
        print('  ', (host.call(f'coop.history civ_start {gid} {c_start}').get('out') or '').strip(), flush=True)
        for act in c_acts:
            print('  ', (host.call(f'coop.history civ {gid} {act}').get('out') or '').strip(), flush=True)

        # Wait for guest cPlayer Tribe/Civ/Space cards and native cEmpire +0x58 archetype index to diverge
        got_cards = wait(
            lambda: (read_history_game(guest)[0] or [''] * 5)[2:5] == [want_t, want_c, want_sp],
            15.0,
        )
        got_arch = wait(lambda: read_history_game(guest)[3] == want_arch_idx, 20.0) if got_cards else False
        time.sleep(1.5)
        h1, g1 = log_state('diverged', host, guest)

        ck(g1[0][2] == want_t and g1[0][2] != h1[0][2],
           f"guest cPlayer Tribe card diverged from host (guest={g1[0][2]}, want={want_t}, host={h1[0][2]})")
        ck(g1[0][3] == want_c and g1[0][3] != h1[0][3],
           f"guest cPlayer Civ card diverged from host (guest={g1[0][3]}, want={want_c}, host={h1[0][3]})")
        ck(g1[0][4] == want_sp and g1[0][4] != h1[0][4],
           f"guest cPlayer Space card diverged from host (guest={g1[0][4]}, want={want_sp}, host={h1[0][4]})")
        ck(got_arch and g1[3] == want_arch_idx and g1[3] != h1[3],
           f"guest native cEmpire +0x58 archetype diverged (guest={g1[3]}, want={want_arch_idx}, host={h1[3]})")
        ck(h1[0] == host_cards0 and h1[3] == host_arch_idx0,
           f"host kept its own cards and archetype (cards={h1[0]} arch={h1[3]})")
        keyframe('rivals_archetype',
                 f'Space Rivals: guest emigrated with independent archetype {want_sp} (idx={want_arch_idx}) vs host idx={h1[3]}')

        # Verify Civ start-spec tie-breaking (2 vs 2 keeps start_spec) and overtake (3 vs 2 flips Civ card)
        for _ in range(2):
            host.call(f'coop.history civ {gid} {alt_c_arg}')
        time.sleep(1.5)
        _, g_tie = log_state('civ_tie', host, guest)
        ck(g_tie[0][3] == want_c,
           f'Civ tie (2 vs 2) preserves guest start_spec card ({g_tie[0][3]} == {want_c})')

        host.call(f'coop.history civ {gid} {alt_c_arg}')
        want_sp_after, want_arch_after = query_space_card(host, guest_cards0[0], guest_cards0[1], want_t, alt_c_card)
        flipped = wait(
            lambda: (read_history_game(guest)[0] or [''] * 5)[3] == alt_c_card
            and (not want_sp_after or (read_history_game(guest)[0] or [''] * 5)[4] == want_sp_after)
            and (want_arch_after < 0 or read_history_game(guest)[3] == want_arch_after),
            15.0,
        )
        _, g_flip = log_state('civ_overtake', host, guest)
        ck(flipped and g_flip[0][3] == alt_c_card,
           f'Civ overtake (3 vs 2) flips guest cPlayer Civ card ({g_flip[0][3]} == {alt_c_card}, space={g_flip[0][4]} arch={g_flip[3]})')

        # Rejoin persistence: guest leaves and rejoins, retains its own Tribe/Civ/Space cards & archetype
        want_sp_after = g_flip[0][4]
        want_arch_after = g_flip[3]
        guest.call('coop.leave')
        time.sleep(3.0)
        print('  guest rejoins:', (guest.call(lane.JOIN).get('out') or '').strip()[:120], flush=True)
        rejoined = wait(
            lambda: 'client id=' in (guest.call('coop.status').get('out') or '')
            and guest_owns_history(guest),
            30.0,
        )
        ck(rejoined, 'guest rejoined host Space session with own history active')
        ck(wait(lambda: guest_owns_history(guest), 30.0), 'guest own history active after rejoin')
        wait(
            lambda: (read_history_game(guest)[0] or [''] * 5)[2:5] == [want_t, alt_c_card, want_sp_after]
            and read_history_game(guest)[3] == want_arch_after,
            25.0,
        )
        time.sleep(1.5)
        h2, g2 = log_state('after_rejoin', host, guest)
        ck(g2[0][2] == want_t and g2[0][3] == alt_c_card and g2[0][4] == want_sp_after,
           f'after rejoin guest retained its own Tribe/Civ/Space cards ({g2[0][2:5]})')
        ck(g2[3] == want_arch_after and g2[3] != h2[3],
           f'after rejoin guest retained its own empire archetype ({g2[3]} != host {h2[3]})')

        ck.faults(host=host, guest=guest)
    finally:
        twoinst.forget_settings()
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
