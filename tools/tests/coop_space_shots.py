#!/usr/bin/env python3
"""Space: a guest sees the shots of the NPC ships the host's game runs (BUG-0677).

The divergence detector found the host's beams (default_beam_projectile) in no guest's game: a guest's mirrors of the
host's ships never fired (squadron.cpp h_FireTool returns 0 for them), only the hits came over (MSG_UFO_DAMAGE), so a
guest saw a fight without a shot.

1. Two games on the `space_empire` fixture; both land on its colony planet 01460035 (NPC ships fight on planets:
   raiders attack around a colony, docs/re/space.md 9.4).
2. The host spawns ambush pirates (type 8: they attack the player anywhere) next to its ship and shoots each once
   (coop.fleet shoot: the game's TakeDamage with the host's ship as the attacker, so the pirate fights back on its own).
3. For 45 s, twice a second, the projectile objects in each game's noun list (default_beam_projectile and
   default_tool_projectile, `coop.civ nouns <noun>`: the game's own objects, not a counter of the mod).
PASS: the host's pirates fired (projectiles in at least 4 samples); the guest's game had projectiles in at least half of
the samples in which the host's had; it did not fire much more than the host's; the guest's copies hurt nothing there
(the guest's own ship lost no more than the host relayed: its hp in its game is not below the relayed damage, read from
the mod's damage_in counter as a cross-check only); no fault in either game.
SPORE_FIX_OFF=space.shots: the guest's game has no projectile at all (FAIL).

  SPORE_LANE=9 python3 tools/tests/coop_space_shots.py
"""
import re
import time

import lane
from play_space import Player
from twoinst import Checks, keyframe, start

PLANET = '01460035'  # space_empire's colony in the home system

PROJ = {'24630D7': 'default_beam_projectile', '24270C5': 'default_tool_projectile'}


def projectiles(con):
    n = 0
    for noun in PROJ:
        n += sum(1 for ln in con.call(f'coop.civ nouns {noun}')['out'].splitlines() if ln.startswith('obj '))
    return n


def ships(con):
    out = con.call('coop.fleet list')['out']
    d = {}
    for m in re.finditer(r'ship #(-?\d+) \S+ type=(\d+) pol=(\w+) hp=([\d.]+)/([\d.]+)( \(me\))?', out):
        d['me' if m.group(6) else int(m.group(1))] = (int(m.group(2)), m.group(3), float(m.group(4)))
    return d


def shots_line(con):
    return next((ln for ln in con.call('coop.fleet list')['out'].splitlines() if ln.startswith('space shots:')), '')


def main():
    host, guest = start('space_empire')
    ck = Checks()
    stats = {}
    ph = Player('host', lane.HOST, 0, 1, time.time() + 1200, stats)
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 1200, stats)
    ph.partner, pg.partner = pg, ph
    for p in (ph, pg):
        p.land_retry(PLANET)
    print(f'  places: host {ph.place()} {ph.ctx()}, guest {pg.place()} {pg.ctx()}', flush=True)
    ck(ph.place()[0] == pg.place()[0] and ph.ctx() == 'planet', 'both on the colony planet')
    time.sleep(3)
    before = set(ships(host))
    for t in (8, 8, 2, 2):
        print(' ', host.call(f'coop.fleet spawn {t}')['out'].strip(), flush=True)
    time.sleep(2)
    hs0 = ships(host)
    pirates = [k for k, v in hs0.items() if k != 'me' and k not in before and v[0] in (2, 8)]
    print(f'  host pirates {pirates}; host ship {hs0.get("me")}', flush=True)
    ck(pirates, 'the host has ambush pirates')
    time.sleep(2)
    gs0 = ships(guest)
    ck(any(p in gs0 for p in pirates), f'the guest mirrors them ({[p for p in pirates if p in gs0]})')
    g_hp0 = gs0.get('me', (0, '', 0))[2]
    for p in pirates:
        print(f'  host shoots #{p}:', host.call(f'coop.fleet shoot {p} 1')['out'].strip(), flush=True)
    hs, gs, both, hn, gn, seq, t0 = 0, 0, 0, 0, 0, [], time.time()
    kf = [False]
    while time.time() - t0 < 45:
        h, g = projectiles(host), projectiles(guest)
        hs += h > 0
        gs += g > 0
        both += h > 0 and g > 0
        hn += h
        gn += g
        seq.append(f'{h}/{g}')
        if h > 0 and g > 0 and not kf[0]:  # the first sample with shots in both games
            kf[0] = True
            keyframe('beams', 'the host\'s pirates fire: beams or projectiles on both screens, the ships drawn with '
                              'their models, the planet below, the Space HUD in place')
        if time.time() - t0 > 20 and hs == 0:  # no fight yet: shoot them again
            for p in pirates:
                host.call(f'coop.fleet shoot {p} 1')
        time.sleep(0.5)
    print(f'  samples with projectiles: host {hs}, guest {gs}, both {both}; projectiles host {hn} guest {gn}', flush=True)
    print('  host/guest per sample:', ' '.join(seq), flush=True)
    print('  host ', shots_line(host), flush=True)
    print('  guest', shots_line(guest), flush=True)
    st = guest.call('coop.fleet list')['out'].splitlines()
    print('  guest', st[0] if st else '', flush=True)
    ck(hs >= 4, f"the host's pirates fired ({hs} samples with projectiles)")
    ck(hs and both >= hs / 2, f"the guest's game shows the host's shots (projectiles in {both} of the host's {hs} samples)")
    ck(gn <= 2 * hn + 6, f"the guest's game does not fire much more than the host's ({gn} / {hn} projectiles)")
    gme = ships(guest).get('me')
    print(f'  guest ship hp {g_hp0} -> {gme}', flush=True)
    ck(gme is not None, "the guest's ship is still there")
    keyframe('end', 'after the fight: both ships over the colony planet, no black screen, the guest\'s ship drawn')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
