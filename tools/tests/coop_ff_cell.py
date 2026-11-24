#!/usr/bin/env python3
"""BUG-0899: friendly fire (all.friendly_fire) in the Cell stage.

Cell fixture, two players:
  1. Both players borrow an electric part (`coop.pool borrow electric graft`) and swim next to each other
     (`coop.pool swimto partner tp`).
  2. With all.friendly_fire off (default): neither player's attacks hurt the other (pvp_out == 0, partner hp stays
     intact).
  3. Host enables all.friendly_fire on (`coop.settings force all.friendly_fire on`): the host's electric attack hurts
     the guest's cell in the guest's game, and the guest's electric attack hurts the host's cell in the host's game
     (pvp_out > 0 and pvp_in > 0 / damage_taken increases on both).
  4. SPORE_FIX_OFF=cell.friendly_fire: even with all.friendly_fire on, neither player hurts the other -> FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_ff_cell.py
"""
import re
import time

import settings_lib as sl
import twoinst
from twoinst import Checks, start

KEYS = ('all.friendly_fire', 'cell.regen')


def pool_stats(con):
    out = con.call('coop.pool')['out']
    def num(pat):
        m = re.search(pat, out)
        return int(m.group(1)) if m else None
    return dict(
        hp=num(r'\bavatar hp=(-?\d+)'),
        dmg=num(r'\bdamage_taken=(\d+)'),
        pvp_out=num(r'\bpvp_out=(\d+)'),
        pvp_in=num(r'\bpvp_in=(\d+)'),
        on=num(r'cell_ff: on=(\d+)'),
        raw=out,
    )


def duel_window(host, guest, secs=8.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        host.call('coop.pool swimto partner tp')
        guest.call('coop.pool swimto partner tp')
        time.sleep(1.0)


def main():
    twoinst.forget_settings()
    host, guest = start('cell')
    ck = Checks()
    try:
        time.sleep(3)
        # Disable cell.regen so partner proximity healing does not mask small hits
        sl.force(host, [guest], 'cell.regen', 'off')
        sl.force(host, [guest], 'all.friendly_fire', 'off')
        # Give both players a spike part (part type 7) so they can stab each other on contact even when
        # sharing the fixture's cell species (electric cells are immune to electricity in Spore).
        hb = host.call('coop.pool borrow spike graft')['out'].strip()
        gb = guest.call('coop.pool borrow spike graft')['out'].strip()
        print('  borrow host:', hb, '| guest:', gb, flush=True)
        ck(hb.startswith('borrowed') and gb.startswith('borrowed'),
           f'both players borrowed spike part (host={hb[:32]}, guest={gb[:32]})')

        # 1. Friendly fire OFF: contact deals no PvP hits in either direction
        h0, g0 = pool_stats(host), pool_stats(guest)
        duel_window(host, guest, secs=6.0)
        h1, g1 = pool_stats(host), pool_stats(guest)
        print(f'  FF off: host pvp_out {h0["pvp_out"]}->{h1["pvp_out"]} pvp_in {h0["pvp_in"]}->{h1["pvp_in"]} | '
              f'guest pvp_out {g0["pvp_out"]}->{g1["pvp_out"]} pvp_in {g0["pvp_in"]}->{g1["pvp_in"]}', flush=True)
        ck(h1['pvp_out'] == h0['pvp_out'] and g1['pvp_out'] == g0['pvp_out'],
           f'friendly fire off: no PvP hits sent (host +{h1["pvp_out"] - h0["pvp_out"]}, guest +{g1["pvp_out"] - g0["pvp_out"]})')

        # 2. Friendly fire ON: both players ram each other with spikes and deal real PvP damage
        ck(sl.force(host, [guest], 'all.friendly_fire', 'on'), 'guest received all.friendly_fire on')
        time.sleep(1.0)
        h2, g2 = pool_stats(host), pool_stats(guest)
        t0 = time.time()
        while time.time() - t0 < 14.0:
            host.call('coop.pool swimto partner tp')
            guest.call('coop.pool swimto partner tp')
            time.sleep(0.8)
            hc, gc = pool_stats(host), pool_stats(guest)
            if (hc['pvp_out'] > h2['pvp_out'] and gc['pvp_in'] > g2['pvp_in'] and
                    gc['pvp_out'] > g2['pvp_out'] and hc['pvp_in'] > h2['pvp_in'] and
                    hc['dmg'] > h2['dmg'] and gc['dmg'] > g2['dmg']):
                break
        h3, g3 = pool_stats(host), pool_stats(guest)
        print(f'  FF on: host pvp_out +{h3["pvp_out"] - h2["pvp_out"]} pvp_in +{h3["pvp_in"] - h2["pvp_in"]} dmg +{h3["dmg"] - h2["dmg"]} | '
              f'guest pvp_out +{g3["pvp_out"] - g2["pvp_out"]} pvp_in +{g3["pvp_in"] - g2["pvp_in"]} dmg +{g3["dmg"] - g2["dmg"]}', flush=True)
        ck(h3['pvp_out'] > h2['pvp_out'] and g3['pvp_in'] > g2['pvp_in'],
           f'friendly fire on: host attack hurt guest (host pvp_out +{h3["pvp_out"] - h2["pvp_out"]}, '
           f'guest pvp_in +{g3["pvp_in"] - g2["pvp_in"]})')
        ck(g3['pvp_out'] > g2['pvp_out'] and h3['pvp_in'] > h2['pvp_in'],
           f'friendly fire on: guest attack hurt host (guest pvp_out +{g3["pvp_out"] - g2["pvp_out"]}, '
           f'host pvp_in +{h3["pvp_in"] - h2["pvp_in"]})')
        ck(g3['dmg'] > g2['dmg'] and h3['dmg'] > h2['dmg'],
           f'friendly fire on: both players took real damage in their own games (host +{h3["dmg"] - h2["dmg"]}, '
           f'guest +{g3["dmg"] - g2["dmg"]})')
    finally:
        for con in (host, guest):
            try:
                con.call('coop.pool borrow off')
            except OSError:
                pass
        sl.reset([host], KEYS)
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
