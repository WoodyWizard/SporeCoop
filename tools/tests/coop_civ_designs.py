#!/usr/bin/env python3
"""Civilization: the host's new building and vehicle designs reach the guest; a guest's own design is the shared
civilization's (E4, BUG-1157, BUG-1158).

The player civilization's designs (City Hall, house, factory, entertainment, the vehicles of each purpose and
locomotion) are its culture set; the planner's Sporepedia pick and the design editors set an entry through
cCivilization 0xBF4C50, which also re-models the civilization's buildings and vehicles of that design
(docs/re/creations.md#creations-stage-designs). Since BUG-0508 a guest stays in place after the host's editor, and
nothing sent the new design: the guest's cities and the mirrors of the host's vehicles kept the old ones.

Set-up (cold start, as coop_civ_host_building / coop_civ_host_design): the host's My Creations hold a base game
building model A under a test id X and a vehicle model VA under Y; the guest's My Creations other models B / VB under
the same ids (two profiles that both made a creation with that id: the hard case of BUG-0647 / BUG-0670).

1. The host's game sets its City Hall design to X and its military land vehicle design to Y through the game's own
   setter (`coop.designs set`, what a pick in the planner calls): its City Hall and its vehicle are re-modelled.
2. PASS: within 40 s the guest's culture set names X and Y, the guest's City Hall of the host's city is drawn from A
   (the bem hash of the model its building has) and its mirror of the host's vehicle from VA.
3. The guest's own pick (`coop.designs set` on the guest: its planner's Sporepedia pick) of another City Hall goes
   back to the host's X within 10 s, with a line that says why.
4. The guest's own design editor (an editor request of the guest in Civ through the Submit hook, as its planner's
   "new" asks for one) is refused: the guest stays in Civ.

SPORE_FIX_OFF=civ.design_sync: step 2 FAILs (the guest keeps the old City Hall and vehicle);
SPORE_FIX_OFF=editors.guest_designs: step 4 FAILs (the guest's editor opens; the test leaves it with the red X).

  SPORE_LANE=4 python3 tools/tests/coop_civ_designs.py
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

X, Y = 0x2903A64B, 0x2903A64D  # the test's design ids, in both profiles (another creation in each)
BEM = 0x1A99B06B
BGROUP, BTYPE = 0x40636200, 0x2399BE55
VGROUP, VTYPE = 0x40646200, 0x24682294
HALL, MIL_LAND = 0x99E92F05, 0x7D433FAD
HALL_NOUN = '018EA1EB'
CIV = '0x01654C04'
CANCEL = (1380, 1062)


def two_models(group):
    path = os.path.join(lane.game_dir(lane.LANE), 'Data', 'Spore_Content.package')
    out = []
    for e in ps.entries(path):
        if e['type'] == BEM and e['group'] == group:
            d = ps.read(path, e)
            if len(d) > 200 and all(d != o for o in out):
                out.append(d)
            if len(out) == 2:
                return out
    raise AssertionError('fewer than two models of group %08X in Spore_Content.package' % group)


def fnv(data):
    h = 0x811C9DC5
    for c in data:
        h = ((h * 0x1000193) & 0xFFFFFFFF) ^ c
    return h


_orig_restore = twoinst.restore_creations
BMODELS, VMODELS = [], []


def restore_with_designs(guest=True):
    _orig_restore(guest)
    for i, d in enumerate((lane.HOST_DIR, lane.GUEST_DIR)):
        p = os.path.join(d, 'EditorSaves.package')
        base = os.path.join(fixtures_dir(), 'creations-host.package' if i == 0 else 'creations-guest.package')
        recs = [r for r in read_pkg(p if os.path.exists(p) else base) if r[2] not in (X, Y)]
        recs.append((BEM, BGROUP, X, BMODELS[i]))
        recs.append((BEM, VGROUP, Y, VMODELS[i]))
        write_pkg(p, recs)


def mode(con):
    out = con.call('game.mode')['out'].split()
    return out[0] if out else ''


def culture(con):
    out = {}
    for ln in con.call('coop.designs culture')['out'].splitlines():
        m = re.match(r'id=(\w+) key=(\w+)/', ln)
        if m:
            out[int(m.group(1), 16)] = int(m.group(2), 16)
    return out


def hall_model(con):
    """the model key instance of the City Hall of the host's city (city 0, the player's)"""
    for ln in con.call('coop.civ bmodels')['out'].splitlines():
        m = re.match(r'\s+bld city=0 \S+ noun=(\w+) model=(\w+)/', ln)
        if m and m.group(1) == HALL_NOUN:
            return int(m.group(2), 16)
    return None


def vehicle_model(con, purpose=0, loco=0):
    for ln in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'\s+veh #\d+ \S+ pol=800021CA .*purpose=(\d+) loco=(\d+).* model=(\w+)', ln)
        if m and int(m.group(1)) == purpose and int(m.group(2)) == loco:
            return int(m.group(3), 16)
    return None


def model_hash(con, inst, t, g):
    if inst is None:
        return '?'
    m = re.search(r'model \w+ (\w+)', con.call(f'coop.creations modelhash {inst:08X} {t:08X} {g:08X}')['out'])
    return m.group(1) if m else '?'


def main():
    BMODELS[:] = two_models(BGROUP)
    VMODELS[:] = two_models(VGROUP)
    ba, bb = f'{fnv(BMODELS[0]):08X}', f'{fnv(BMODELS[1]):08X}'
    va, vb = f'{fnv(VMODELS[0]):08X}', f'{fnv(VMODELS[1]):08X}'
    print(f'  fixture: City Hall design {X:08X}: host {ba}, guest {bb}; vehicle design {Y:08X}: host {va}, guest {vb}',
          flush=True)
    twoinst.restore_creations = restore_with_designs
    host, guest = start('civ_npc')
    ck = Checks()
    g0 = culture(guest)
    gh0 = hall_model(guest)
    print(f'  before: guest City Hall design {g0.get(HALL, 0):08X}, its City Hall model {gh0 and f"{gh0:08X}"}', flush=True)
    ck(vehicle_model(host) is not None and vehicle_model(guest) is not None,
       'the host has a military land vehicle and the guest its mirror')

    # 1. the host's new designs, through the game's own setter
    for did, k in ((HALL, X), (MIL_LAND, Y)):
        print('  host:', host.call(f'coop.designs set {did:08X} {k:08X}')['out'].strip(), flush=True)
    time.sleep(2)
    hh, hv = hall_model(host), vehicle_model(host)
    ck(hh == X and model_hash(host, hh, BTYPE, BGROUP) == ba,
       f'the host\'s City Hall is re-modelled to its design ({hh and f"{hh:08X}"}: {model_hash(host, hh, BTYPE, BGROUP)})')
    ck(hv == Y and model_hash(host, hv, VTYPE, VGROUP) == va,
       f'the host\'s vehicle is re-modelled to its design ({hv and f"{hv:08X}"}: {model_hash(host, hv, VTYPE, VGROUP)})')

    # 2. the guest
    gc, gh, gv, ghh, gvh = {}, None, None, '?', '?'
    t0 = time.time()
    while time.time() - t0 < 40:
        gc, gh, gv = culture(guest), hall_model(guest), vehicle_model(guest)
        ghh, gvh = model_hash(guest, gh, BTYPE, BGROUP), model_hash(guest, gv, VTYPE, VGROUP)
        if gc.get(HALL) == X and gc.get(MIL_LAND) == Y and ghh == ba and gvh == va:
            break
        time.sleep(1)
    print(f'  guest: designs {gc.get(HALL, 0):08X} / {gc.get(MIL_LAND, 0):08X}, City Hall model '
          f'{gh and f"{gh:08X}"} -> {ghh}, vehicle model {gv and f"{gv:08X}"} -> {gvh}', flush=True)
    print('  guest:', guest.call('coop.designs')['out'].strip().splitlines()[0], flush=True)
    ck(gc.get(HALL) == X and gc.get(MIL_LAND) == Y, 'the guest\'s civilization has the host\'s new designs')
    ck(ghh == ba, f'the guest\'s City Hall shows the host\'s new design ({ghh}, host {ba}, the guest\'s own creation {bb})')
    ck(gvh == va, f'the guest\'s mirror of the host\'s vehicle shows the host\'s new design ({gvh}, host {va}, '
                  f'the guest\'s own creation {vb})')
    twoinst.keyframe('designs', 'after the host\'s new City Hall and vehicle designs: the player\'s City Hall and its '
                                'vehicle look the same on both games, both in Civ play')

    # 3. the guest's own pick goes back to the host's
    other = g0.get(HALL) if g0.get(HALL) not in (None, 0, X) else 0x067CEDE1
    print('  guest:', guest.call(f'coop.designs set {HALL:08X} {other:08X}')['out'].strip(), flush=True)
    back = None
    t0 = time.time()
    while time.time() - t0 < 10:
        if culture(guest).get(HALL) == X:
            back = time.time() - t0
            break
        time.sleep(0.5)
    m = re.search(r'own_back=(\d+)', guest.call('coop.designs')['out'])
    print(f'  guest\'s own pick {other:08X}: back to the host\'s after {back}; {m.group(0) if m else "?"}', flush=True)
    ck(back is not None and m and int(m.group(1)) > 0,  # counter: own_back says the guest was told (a line of the mod)
       'the guest\'s own City Hall pick goes back to the host\'s design, told')

    # 4. the guest's own design editor
    print('  guest:', guest.call(f'coop.designs editor {HALL:08X}')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 10 and mode(guest) == CIV:
        time.sleep(1)
    gm = mode(guest)
    m = re.search(r'refused=(\d+)', guest.call('coop.designs')['out'])
    print(f'  guest mode after its editor request: {gm}; {m.group(0) if m else "?"}', flush=True)
    ck(gm == CIV and m and int(m.group(1)) > 0, 'the guest\'s own design editor is refused (the civilization is the shared one)')
    if gm != CIV:  # the switch off: leave its editor
        guest.call('wait.frames 120 30000')
        guest.call('move %d %d' % CANCEL)
        time.sleep(0.5)
        guest.call('click %d %d' % CANCEL)
        t0 = time.time()
        while time.time() - t0 < 60 and mode(guest) != CIV:
            time.sleep(1)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
