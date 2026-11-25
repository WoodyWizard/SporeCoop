#!/usr/bin/env python3
"""BUG-1558: automatic default Settings presets on Rivals vs Together switch and live all.friendly_fire toggle.

Cell fixture, two players:
  1. Initial Together defaults on host and guest (all.friendly_fire=0, all.dna_pace=1, civ.own_money=0,
     cell.regen=1, creature.pack_rescue=1, tribe.pincer=1, civ.roles=1, space.contracts=1, core.switch_vote=1).
  2. Host sets a user/lobby setting (all.max_players=3) and forces space.rivals=0.
  3. Host runs `coop.rivals preset all_rivals`: host and guest automatically receive the Rivals Settings preset
     (all.friendly_fire=1, all.dna_pace=0, civ.own_money=1, space.rivals=1, co-op bonuses 0 in raw settings list),
     while preserving all.max_players=3.
  4. Host runs `coop.rivals preset all_together`: host and guest automatically restore Together defaults
     (all.friendly_fire=0, all.dna_pace=1, civ.own_money=0, co-op bonuses 1), still preserving all.max_players=3.
  5. Direct `coop.settings preset rivals|together` works on host and is refused on guest.
  6. In a live started world, host toggles `coop.settings set all.friendly_fire on` and `off` (no `force`):
     accepted and synced to guest, while guest cannot set it and Session settings (all.dna_pace) remain refused.
  7. SPORE_FIX_OFF=rivals.settings_preset (`rivals_presets_off`): preset switch leaves settings untouched and live
     all.friendly_fire toggle is refused -> FAIL.

  SPORE_LANE=3 python3 tools/tests/coop_rivals_presets.py
"""
import re
import time

import settings_lib as sl
import twoinst
from twoinst import Checks, start


def wait_for(fn, timeout=10.0, step=0.25):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


def raw_map(con):
    """Parse raw (non-masked) key=value pairs from `coop.settings list`."""
    out = con.call('coop.settings list').get('out') or ''
    res = {}
    for line in out.splitlines():
        m = re.match(r'^([a-z0-9_.]+)=(-?\d+)\b', line)
        if m:
            res[m.group(1)] = int(m.group(2))
    return res


def has_values(con, expected):
    cur = raw_map(con)
    return all(cur.get(k) == v for k, v in expected.items())


RIVALS_EXPECTED = {
    'all.friendly_fire': 1,
    'all.dna_pace': 0,
    'civ.own_money': 1,
    'space.rivals': 1,
    'core.switch_vote': 0,
    'creature.mode': 0,
    'cell.regen': 0,
    'all.partner_arrows': 0,
    'cell.knockout': 0,
    'cell.respawn_small': 0,
    'creature.pack_rescue': 0,
    'creature.revive': 0,
    'creature.pack_heal': 0,
    'creature.joint_hunt': 0,
    'tribe.pincer': 0,
    'tribe.chief_aura': 0,
    'tribe.duet': 0,
    'tribe.epic': 0,
    'civ.roles': 0,
    'civ.joint_siege': 0,
    'civ.superweapon_vote': 0,
    'space.disable': 0,
    'space.contracts': 0,
    'space.diplomacy': 0,
    'space.allies': 0,
    'space.finale': 0,
    'adventure.shared': 0,
    'all.max_players': 3,
}

TOGETHER_EXPECTED = {
    'all.friendly_fire': 0,
    'all.dna_pace': 1,
    'civ.own_money': 0,
    'space.rivals': 1,
    'core.switch_vote': 1,
    'creature.mode': 0,
    'cell.regen': 1,
    'all.partner_arrows': 1,
    'cell.knockout': 1,
    'cell.respawn_small': 1,
    'creature.pack_rescue': 1,
    'creature.revive': 1,
    'creature.pack_heal': 1,
    'creature.joint_hunt': 1,
    'tribe.pincer': 1,
    'tribe.chief_aura': 1,
    'tribe.duet': 1,
    'tribe.epic': 1,
    'civ.roles': 1,
    'civ.joint_siege': 1,
    'civ.superweapon_vote': 1,
    'space.disable': 1,
    'space.contracts': 1,
    'space.diplomacy': 1,
    'space.allies': 1,
    'space.finale': 1,
    'adventure.shared': 1,
    'all.max_players': 3,
}


def diff_expected(con, expected):
    cur = raw_map(con)
    return {k: (cur.get(k), v) for k, v in expected.items() if cur.get(k) != v}


def main():
    twoinst.forget_settings()
    host, guest = start('cell')
    ck = Checks()
    try:
        time.sleep(2.0)

        # 1. Verify initial Together defaults on host & guest
        init_want = {
            'all.friendly_fire': 0,
            'all.dna_pace': 1,
            'civ.own_money': 0,
            'cell.regen': 1,
            'creature.pack_rescue': 1,
            'tribe.pincer': 1,
            'civ.roles': 1,
            'space.contracts': 1,
            'core.switch_vote': 1,
            'all.max_players': 4,
        }
        ck(has_values(host, init_want), f'host initial Together defaults (diff={diff_expected(host, init_want)})')
        ck(bool(wait_for(lambda: has_values(guest, init_want), 8.0)),
           f'guest initial Together defaults (diff={diff_expected(guest, init_want)})')

        # 2. Customize a non-preset lobby setting (all.max_players=3) and turn off space.rivals to prove Rivals restores it
        r_mp = host.call('coop.settings set all.max_players 3').get('out') or ''
        ck(r_mp.startswith('all.max_players=3'), f'host set all.max_players=3 ({r_mp.strip()})')
        ck(sl.force(host, [guest], 'space.rivals', 'off'), 'guest received space.rivals=0 before preset switch')
        ck(bool(wait_for(lambda: sl.get(guest, 'all.max_players') == 3, 6.0)), 'guest synced all.max_players=3')

        # 3. Switch to All rivals -> host & guest receive full Rivals Settings preset, preserving all.max_players=3
        r_ar = host.call('coop.rivals preset all_rivals').get('out') or ''
        print('  host all_rivals:', r_ar.strip(), flush=True)
        ck('All rivals' in r_ar, f'host switched to All rivals ({r_ar.strip()})')
        ck(has_values(host, RIVALS_EXPECTED),
           f'host applied Rivals Settings preset (diff={diff_expected(host, RIVALS_EXPECTED)})')
        ck(bool(wait_for(lambda: has_values(guest, RIVALS_EXPECTED), 8.0)),
           f'guest synced Rivals Settings preset (diff={diff_expected(guest, RIVALS_EXPECTED)})')

        # 4. Switch back to All together -> host & guest restore Together defaults, preserving all.max_players=3
        r_at = host.call('coop.rivals preset all_together').get('out') or ''
        print('  host all_together:', r_at.strip(), flush=True)
        ck('All together' in r_at, f'host switched to All together ({r_at.strip()})')
        ck(has_values(host, TOGETHER_EXPECTED),
           f'host restored Together Settings preset (diff={diff_expected(host, TOGETHER_EXPECTED)})')
        ck(bool(wait_for(lambda: has_values(guest, TOGETHER_EXPECTED), 8.0)),
           f'guest synced Together Settings preset (diff={diff_expected(guest, TOGETHER_EXPECTED)})')

        # 5. Direct `coop.settings preset` subcommand: refused on guest, works on host
        r_gp = guest.call('coop.settings preset rivals')
        ck(not r_gp.get('ok') and 'refused' in (r_gp.get('out') or ''),
           f'guest refused coop.settings preset ({(r_gp.get("out") or "").strip()})')
        r_hp1 = host.call('coop.settings preset rivals')
        ck(r_hp1.get('ok') and has_values(host, RIVALS_EXPECTED),
           f'host coop.settings preset rivals applied ({(r_hp1.get("out") or "").strip()})')
        ck(bool(wait_for(lambda: has_values(guest, RIVALS_EXPECTED), 8.0)),
           'guest synced direct coop.settings preset rivals')
        r_hp2 = host.call('coop.settings preset together')
        ck(r_hp2.get('ok') and has_values(host, TOGETHER_EXPECTED),
           f'host coop.settings preset together restored ({(r_hp2.get("out") or "").strip()})')
        ck(bool(wait_for(lambda: has_values(guest, TOGETHER_EXPECTED), 8.0)),
           'guest synced direct coop.settings preset together')

        # 6. Live all.friendly_fire toggle in a started world (NO force!)
        hl = host.call('coop.settings list').get('out') or ''
        ck('all.friendly_fire=0 off [live,' in hl, 'all.friendly_fire listed as [live] in started world')

        r_on = host.call('coop.settings set all.friendly_fire on')
        out_on = (r_on.get('out') or '').strip()
        print('  host live FF on:', out_on, flush=True)
        ck(r_on.get('ok') and out_on.startswith('all.friendly_fire=1') and sl.get(host, 'all.friendly_fire') == 1,
           f'host toggled all.friendly_fire on live without force ({out_on})')
        ck(bool(wait_for(lambda: sl.get(guest, 'all.friendly_fire') == 1, 8.0)),
           'guest received live all.friendly_fire=1')

        r_off = host.call('coop.settings set all.friendly_fire off')
        out_off = (r_off.get('out') or '').strip()
        print('  host live FF off:', out_off, flush=True)
        ck(r_off.get('ok') and out_off.startswith('all.friendly_fire=0') and sl.get(host, 'all.friendly_fire') == 0,
           f'host toggled all.friendly_fire off live without force ({out_off})')
        ck(bool(wait_for(lambda: sl.get(guest, 'all.friendly_fire') == 0, 8.0)),
           'guest received live all.friendly_fire=0')

        # Guest still cannot toggle all.friendly_fire; Session setting (all.dna_pace) still refused when started
        r_gff = guest.call('coop.settings set all.friendly_fire on')
        ck(not r_gff.get('ok') and 'refused' in (r_gff.get('out') or '') and sl.get(guest, 'all.friendly_fire') == 0,
           f'guest refused live all.friendly_fire set ({(r_gff.get("out") or "").strip()})')
        r_sess = host.call('coop.settings set all.dna_pace off')
        ck(not r_sess.get('ok') and 'refused' in (r_sess.get('out') or '') and sl.get(host, 'all.dna_pace') == 1,
           f'session setting all.dna_pace still refused in started world ({(r_sess.get("out") or "").strip()})')

    finally:
        try:
            host.call('coop.rivals preset all_together')
            host.call('coop.settings set all.max_players 4')
            host.call('coop.settings set all.friendly_fire off')
        except Exception:
            pass
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
