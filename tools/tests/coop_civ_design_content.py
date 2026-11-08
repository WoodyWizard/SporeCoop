#!/usr/bin/env python3
"""BUG-0725 / BUG-0726 (class BUG-0424): Civ, a host's vehicle or building design names a part the guest lacks.

The host's civilization names its designs by the keys of the host's own My Creations; the guest builds its mirrors
of them from the host's records (BUG-0647 vehicles, BUG-0670 buildings) through creations::ensure_model, which shows
a part this game lacks as a substitute. Two gaps:
 - BUG-0725: the designs went out without the descriptions of their parts and ensure_model substituted without any:
   the part became one fixed base game part (kBasePartFallbacks), not the most similar part this game has (the
   lookalike of BUG-0559, which creature bundles get).
 - BUG-0726: a building design with such a part was refused (cCity::SetBuildingModel reads the design key itself,
   allow_copy false): the host's house was missing from the guest's city.

One lane has one install for both games, so the guest is made to lack a part: `coop.creations lack <id>` makes the
mod's checks, substitutes and lookalike search treat that part as one this game does not have (the game itself
still has it: no bake fault can show here; the pack-moving tests content_civ / content_lookalike cover the fault).
Set-up (cold start): both profiles' My Creations hold the same base game vehicle model under VX and the same base
game building model under BX (one creation of the host's that the guest also has under that id); both games'
player civilization name VX as its military land vehicle design and BX as its house design. The guest lacks one part
of each model (the first part the model names). The host spawns a vehicle of VX and builds a house of BX.

PASS: the guest's mirror vehicle is drawn from a model (the bem record the guest's game answers for the mirror's own
model key) that names no lacked part and names, in its place, the part the lookalike chose for it from the host's
description (`coop.creations looks`), not a fixed fallback; the guest's city has the host's house, drawn from a model
without the lacked part.
FAIL: SPORE_FIX_OFF=content.design_lookalike (the fallback part in the vehicle), SPORE_FIX_OFF=content.building_copy
(no house on the guest).

  SPORE_LANE=7 python3 tools/tests/coop_civ_design_content.py
"""
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import part_substitutes as ps  # noqa: E402

import lane  # noqa: E402
import twoinst  # noqa: E402
from coop_nest_species import fixtures_dir, read_pkg, write_pkg  # noqa: E402
from twoinst import Checks, start  # noqa: E402

VX, BX = 0x2903A657, 0x2903A658  # the test's design ids (the same creation in both profiles)
BEM = 0x1A99B06B
VGROUP, BGROUP = 0x40646200, 0x40636200
VPARTS, BPARTS = '40646000', '40636000'
BTYPE = 0x2399BE55
PLAYER_POL = '800021CA'  # the player civilization of the civ_npc fixture
# content_packs.h kBasePartFallbacks of the vehicle kind: the fixed parts of 0.7.10
FALLBACKS = {0x00046BAE, 0x011015FB, 0x01568CD8, 0x0AE2FEE9, 0x1ED67DFE}


def a_model(group, min_parts=3):
    """a base game model record of that group with at least min_parts parts of its part group"""
    path = os.path.join(lane.game_dir(lane.LANE), 'Data', 'Spore_Content.package')
    for e in ps.entries(path):
        if e['type'] == BEM and e['group'] == group:
            d = ps.read(path, e)
            if len(d) > 400:
                return d
    raise AssertionError(f'no model of group {group:08X} in Spore_Content.package')


MODELS = {}
_orig_restore = twoinst.restore_creations


def restore_with_designs(guest=True):
    _orig_restore(guest)
    for d in (lane.HOST_DIR, lane.GUEST_DIR):
        p = os.path.join(d, 'EditorSaves.package')
        base = os.path.join(fixtures_dir(), 'creations-host.package' if d == lane.HOST_DIR else 'creations-guest.package')
        recs = [r for r in read_pkg(p if os.path.exists(p) else base) if r[2] not in (VX, BX)]
        recs.append((BEM, VGROUP, VX, MODELS['v']))
        recs.append((BEM, BGROUP, BX, MODELS['b']))
        write_pkg(p, recs)


def mparts(con, kind, inst):
    out = con.call(f'coop.creations mparts {kind} {inst:08X}')['out'].split()
    return [(int(x[:8], 16), x[8:]) for x in out[2:] if re.match(r'[0-9A-F]{8}', x)]


def looks(con):
    out = {}
    for x in con.call('coop.creations looks')['out'].split()[1:]:
        m = re.match(r'(\w+)/(\w+)=(\w+)', x)
        if m:
            out[int(m.group(2), 16)] = int(m.group(3), 16)
    return out


def vehicles(con):
    out = {}
    for ln in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'\s+veh #(\d+) \S+ pol=(\w+) .*purpose=(\d+) loco=(\d+).* model=(\w+)', ln)
        if m:
            out[int(m.group(1))] = (m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5), 16))
    return out


def buildings(con):
    out = []
    for ln in con.call('coop.civ bmodels')['out'].splitlines():
        m = re.match(r'\s+bld city=(\d+) (\S+) noun=(\w+) model=(\w+)/\w+/\w+ own=(\w+)', ln)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3), int(m.group(4), 16), int(m.group(5), 16)))
    return out


def main():
    MODELS['v'] = a_model(VGROUP)
    MODELS['b'] = a_model(BGROUP)
    twoinst.restore_creations = restore_with_designs
    host, guest = start('civ_npc')
    ck = Checks()
    vp, bp = mparts(guest, 'vehicle', VX), mparts(guest, 'building', BX)
    print(f'  vehicle {VX:08X} parts {[f"{i:08X}" for i, _ in vp]}\n  building {BX:08X} parts {[f"{i:08X}" for i, _ in bp]}',
          flush=True)
    ck(vp and bp, 'the guest\'s game reads both test designs')
    if not (vp and bp):
        return ck.result()
    lv = next(i for i, _ in vp if i not in FALLBACKS)
    lb = bp[0][0]
    for p in (lv, lb):
        print('  guest:', guest.call(f'coop.creations lack {p:08X}')['out'].strip(), flush=True)
    for c, who in ((host, 'host'), (guest, 'guest')):
        print(f'  {who}:', c.call(f'coop.civ setdesign {PLAYER_POL} 0 0 {VX:08X}')['out'].strip(),
              c.call(f'coop.civ bdesign {PLAYER_POL} house {BX:08X}')['out'].strip(), flush=True)
    city = None
    for ln in host.call('coop.civ')['out'].splitlines():
        m = re.match(r'city (\d+) \S+ pol=\w+ \(player\)', ln)
        if m:
            city = int(m.group(1))
            break
    ck(city is not None, f'the host has a city ({city})')
    before = set(vehicles(host))
    hb0 = len([b for b in buildings(guest) if BX in (b[3], b[4])])
    print('  host:', host.call(f'coop.civ spawnveh {city} 0 0')['out'].strip(), '|',
          host.call(f'coop.civ build {city} house nopay')['out'].strip(), flush=True)
    new, gv, house = None, {}, None
    t0 = time.time()
    while time.time() - t0 < 45:
        hv, gv = vehicles(host), vehicles(guest)
        fresh = [i for i in hv if i not in before and hv[i][3] == VX]
        if fresh and fresh[0] in gv and gv[fresh[0]][3]:
            new = fresh[0]
        gb = [b for b in buildings(guest) if BX in (b[3], b[4])]
        if len(gb) > hb0:
            house = gb[-1]
        if new is not None and house is not None:
            break
        time.sleep(1)
    time.sleep(2)
    gv = vehicles(guest)
    print('  guest:', guest.call('coop.creations designs')['out'].strip(), '|',
          guest.call('coop.creations content')['out'].strip().replace('\n', ' | '), flush=True)
    lk = looks(guest)
    print(f'  guest lookalikes: { {f"{k:08X}": f"{v:08X}" for k, v in lk.items()} }', flush=True)
    ck(new is not None, f'the host\'s vehicle of design {VX:08X} is mirrored on the guest ({new})')
    if new is not None:
        mk = gv[new][3]
        parts = [i for i, _ in mparts(guest, 'vehicle', mk)]
        print(f'  guest mirror #{new}: model {mk:08X} parts {[f"{i:08X}" for i in parts]}', flush=True)
        ck(parts and lv not in parts, f'the guest\'s mirror model names no part its game lacks ({lv:08X})')
        sub = lk.get(lv)
        ck(sub is not None and sub != 0, f'the guest chose a lookalike for {lv:08X} from the host\'s description ({sub})')
        ck(sub is not None and sub in parts, f'the mirror shows that lookalike in its place ({sub and f"{sub:08X}"})')
        ck(not (set(parts) & FALLBACKS - {i for i, _ in vp}),
           f'no fixed fallback part replaced it ({[f"{i:08X}" for i in set(parts) & FALLBACKS]})')
    ck(house is not None, f'the guest\'s city has the host\'s house of design {BX:08X} ({house})')
    if house is not None:
        parts = [i for i, _ in mparts(guest, 'building', house[3])]
        print(f'  guest house: model {house[3]:08X} parts {[f"{i:08X}" for i in parts]}', flush=True)
        ck(parts and lb not in parts, f'the guest\'s house model names no part its game lacks ({lb:08X})')
    guest.call('coop.creations lack clear')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
