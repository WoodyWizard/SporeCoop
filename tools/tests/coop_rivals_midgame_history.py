#!/usr/bin/env python3
"""BUG-1565: Per-player starting evolutionary path / Space philosophy picker for worlds started past Cell.

Space fixture, two players:
  1. Connect host and guest in Space; verify all 10 canonical Space philosophy presets map via Spore's native
     `SpaceArchetype_FromTraits` (`coop.history presets`) to the exact expected Space card ID and archetype index.
  2. Seed host's stages 0..3 if the Space fixture started with empty prior cards so host has a full path.
  3. Verify guest initially inherits host's seeded cards; guest picks a distinct Space philosophy via
     `coop.history pick philosophy <name>` (e.g. zealot, different from host).
  4. Verify in real game state (`cPlayer::GetConsequenceTrait` + native `cEmpire +0x58`):
     - Guest's `cPlayer` cards [0..4] update to the chosen philosophy's 4 prior cards + Space archetype card.
     - Guest's native `cEmpire +0x58` updates to the chosen philosophy's archetype index.
     - Host's `cPlayer` cards and native `cEmpire +0x58` remain completely unchanged.
  5. Verify guest can switch to a second philosophy (e.g. scientist / shaman) and pick an individual prior-stage
     card (`coop.history pick cell ...`), updating `cPlayer` and `cEmpire +0x58` cleanly without duplicating tools.
  6. Guest leaves and rejoins (`coop.leave` / `lane.JOIN`): verify the guest's picked cards and native
     `cEmpire +0x58` archetype persist across rejoin and do not revert to the host's cards.
  7. `SPORE_FIX_OFF=history.midgame_pick` twin (`rivals_midgame_history_off`): pick is refused and guest keeps
     the host's seeded cards and archetype -> FAILS.

  SPORE_LANE=5 python3 tools/tests/coop_rivals_midgame_history.py
"""
import re
import time

import lane
import twoinst
from harness import waiter
from twoinst import Checks, keyframe, start

wait = waiter(timeout=25.0, step=0.5)

CELL_CARN = 'CFB01B93'
CELL_HERB = 'A8EC6F99'
CREAT_AGGR = '17E5EF84'
TRIBE_AGGR = '0CAC124D'
CIV_MIL = '0A35D0F5'

EXPECTED_PRESETS = {
    'warrior':   ('D9C806E4', 9),
    'trader':    ('B7CB9F7A', 10),
    'scientist': ('9CE4639C', 11),
    'shaman':    ('CF6C2AB4', 12),
    'bard':      ('50FEE6BF', 13),
    'zealot':    ('2B35F523', 14),
    'diplomat':  ('B436C0BC', 15),
    'ecologist': ('B40D2187', 16),
    'knight':    ('F966D429', 18),
    'wanderer':  ('A5656E84', 17),
}


def read_history_game(con):
    """Read real game state via `coop.history`: ([5 hex cards], social, combat, empire_arch_idx)."""
    out = con.call('coop.history').get('out') or ''
    m = re.search(r'game cards ((?:[0-9A-F]{8} ?){5}) social=(-?[\d.]+) combat=(-?[\d.]+)', out)
    a = re.search(r'empire archetype=(-?\d+)', out)
    if not m:
        return [], 0.0, 0.0, None
    return m.group(1).split(), float(m.group(2)), float(m.group(3)), (int(a.group(1)) if a else None)


def guest_owns_history(guest):
    return 'owns=1' in (guest.call('coop.history').get('out') or '')


def parse_presets(host):
    out = host.call('coop.history presets').get('out') or ''
    got = {}
    for line in out.splitlines():
        m = re.match(
            r'^(\w+)\s+cards=([0-9A-F]{8}),([0-9A-F]{8}),([0-9A-F]{8}),([0-9A-F]{8})\s+'
            r'want=([0-9A-F]{8})\s+got=([0-9A-F]{8})\s+idx=(-?\d+)\s+match=(\d)',
            line.strip(),
        )
        if m:
            got[m.group(1)] = {
                'cards': [m.group(2), m.group(3), m.group(4), m.group(5)],
                'want': m.group(6),
                'got': m.group(7),
                'idx': int(m.group(8)),
                'match': m.group(9) == '1',
            }
    return got


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

        # 1. Verify all 10 Space philosophy presets against native SpaceArchetype_FromTraits
        presets = parse_presets(host)
        bad_presets = []
        for name, (want_card, want_idx) in EXPECTED_PRESETS.items():
            row = presets.get(name)
            if not row or not row['match'] or row['got'] != want_card or row['idx'] != want_idx:
                bad_presets.append(f'{name}:{row}')
        print(f'  [presets] verified {len(presets)}/10 native SpaceArchetype_FromTraits presets', flush=True)
        ck(len(presets) == 10 and not bad_presets,
           f'all 10 philosophy presets match native SpaceArchetype_FromTraits (bad={bad_presets})')

        h0, g0 = log_state('initial', host, guest)
        ck(len(h0[0]) == 5 and len(g0[0]) == 5 and h0[3] is not None and g0[3] is not None,
           f'both Space games report 5 cPlayer cards and empire archetype (host={h0[3]} guest={g0[3]})')
        if len(h0[0]) != 5 or len(g0[0]) != 5:
            return ck.result()

        # 2. Seed host stages 0..3 if the Space fixture started with empty earlier cards
        if any(c == '00000000' for c in h0[0][:4]):
            for st_idx, card in enumerate((CELL_CARN, CREAT_AGGR, TRIBE_AGGR, CIV_MIL)):
                if h0[0][st_idx] == '00000000':
                    host.call(f'coop.history set 0 {st_idx} {card}')
            wait(lambda: all(c != '00000000' for c in (read_history_game(host)[0] or ['00000000'] * 5)[:4]), 10.0)
            time.sleep(2.0)
            h0, g0 = log_state('seeded host', host, guest)

        host_cards0 = h0[0]
        host_arch0 = h0[3]

        # 3. Pick two philosophies distinct from host's current Space card
        phil1 = 'zealot' if host_cards0[4] != EXPECTED_PRESETS['zealot'][0] else 'scientist'
        phil2 = 'shaman' if host_cards0[4] != EXPECTED_PRESETS['shaman'][0] and phil1 != 'shaman' else 'diplomat'
        want1_cards = presets[phil1]['cards'] + [EXPECTED_PRESETS[phil1][0]]
        want1_idx = EXPECTED_PRESETS[phil1][1]
        want2_cards = presets[phil2]['cards'] + [EXPECTED_PRESETS[phil2][0]]
        want2_idx = EXPECTED_PRESETS[phil2][1]

        out1 = (guest.call(f'coop.history pick philosophy {phil1}').get('out') or '').strip()
        print(f'  [pick 1] {phil1}: {out1}', flush=True)
        got1_cards = wait(lambda: read_history_game(guest)[0] == want1_cards, 15.0)
        got1_arch = wait(lambda: read_history_game(guest)[3] == want1_idx, 20.0) if got1_cards else False
        time.sleep(1.5)
        h1, g1 = log_state(f'picked {phil1}', host, guest)

        ck(got1_cards and g1[0] == want1_cards,
           f'guest cPlayer cards updated to {phil1} ({g1[0]} == {want1_cards})')
        ck(got1_arch and g1[3] == want1_idx and g1[3] != host_arch0,
           f'guest native cEmpire +0x58 updated to {phil1} (guest={g1[3]}, want={want1_idx}, host={h1[3]})')
        ck(h1[0] == host_cards0 and h1[3] == host_arch0,
           f'host cPlayer cards and archetype stayed unchanged ({h1[0]} arch={h1[3]})')
        keyframe('midgame_pick_phil1',
                 f'Guest picked {phil1} philosophy (arch={g1[3]}) while host kept arch={h1[3]}')

        # 4. Switch to a second philosophy on guest and verify clean transition
        out2 = (guest.call(f'coop.history pick philosophy {phil2}').get('out') or '').strip()
        print(f'  [pick 2] {phil2}: {out2}', flush=True)
        got2_cards = wait(lambda: read_history_game(guest)[0] == want2_cards, 15.0)
        got2_arch = wait(lambda: read_history_game(guest)[3] == want2_idx, 20.0) if got2_cards else False
        time.sleep(1.5)
        h2, g2 = log_state(f'picked {phil2}', host, guest)

        ck(got2_cards and g2[0] == want2_cards,
           f'guest switched cleanly to {phil2} ({g2[0]} == {want2_cards})')
        ck(got2_arch and g2[3] == want2_idx,
           f'guest native cEmpire +0x58 switched to {phil2} ({g2[3]} == {want2_idx})')
        ck(h2[0] == host_cards0 and h2[3] == host_arch0,
           f'host stayed unchanged after second guest pick ({h2[0]} arch={h2[3]})')

        # 5. Individual prior-stage card pick (e.g. Cell -> carnivore/herbivore distinct from phil2's Cell)
        alt_cell_name, alt_cell_id = ('carnivore', CELL_CARN) if want2_cards[0] != CELL_CARN else ('herbivore', CELL_HERB)
        out3 = (guest.call(f'coop.history pick cell {alt_cell_name}').get('out') or '').strip()
        print(f'  [pick cell] {alt_cell_name}: {out3}', flush=True)
        got3_cell = wait(lambda: (read_history_game(guest)[0] or [''])[0] == alt_cell_id, 15.0)
        time.sleep(1.5)
        h3, g3 = log_state(f'picked cell {alt_cell_name}', host, guest)
        ck(got3_cell and g3[0][0] == alt_cell_id and g3[0][1:4] == want2_cards[1:4],
           f'single-stage Cell pick updated only Cell and kept Creature/Tribe/Civ ({g3[0]})')

        # 6. Switch back to phil1 before rejoin so we verify full philosophy + archetype persistence across rejoin
        guest.call(f'coop.history pick philosophy {phil1}')
        wait(lambda: read_history_game(guest)[0] == want1_cards and read_history_game(guest)[3] == want1_idx, 20.0)
        time.sleep(1.0)

        guest.call('coop.leave')
        time.sleep(3.0)
        print('  guest rejoins:', (guest.call(lane.JOIN).get('out') or '').strip()[:120], flush=True)
        rejoined = wait(
            lambda: 'client id=' in (guest.call('coop.status').get('out') or '')
            and guest_owns_history(guest),
            30.0,
        )
        ck(rejoined, 'guest rejoined host Space session with own history active')
        got_rejoin = wait(
            lambda: read_history_game(guest)[0] == want1_cards and read_history_game(guest)[3] == want1_idx,
            25.0,
        )
        time.sleep(1.5)
        h4, g4 = log_state('after_rejoin', host, guest)
        ck(got_rejoin and g4[0] == want1_cards and g4[3] == want1_idx,
           f'guest retained picked {phil1} cards and empire archetype across rejoin ({g4[0]} arch={g4[3]})')
        ck(h4[0] == host_cards0 and h4[3] == host_arch0,
           f'host still kept original cards and archetype after rejoin ({h4[0]} arch={h4[3]})')

        ck.faults(host=host, guest=guest)
    finally:
        twoinst.forget_settings()
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
