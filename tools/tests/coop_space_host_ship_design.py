#!/usr/bin/env python3
"""BUG-0724 (class BUG-0424): Space, the guest's mirror of a host ship shows the host's own ship design.

The class of BUG-0647 (Civ vehicles): the host's empire's own ships (its escorts, its colonies' defenders) carry the
keys of the host's own ship creations (My Creations, ids sequential per profile); the guest's game resolves the same
key to the guest's OWN creation of that id (or to nothing).

Set-up (cold start): the host's My Creations get a base game UFO model A under a test id X, the guest's another base
game UFO model B under the same X (two profiles that both made a ship with that id). `coop.fleet fakemodel 2 X` makes
the host send X as the model of its ships (as if its empire flew its own design X), and the host spawns NPC ships
next to both players.

PASS: the guest's mirrors are drawn from a model whose record is the host's A (the bem record the guest's game
answers for the mirror's own model key, cGameDataUFO +0x780, hashed the same as A on the host).
FAIL (`SPORE_FIX_OFF=fleet.host_designs`): the mirrors show B, the guest's own creation.

  SPORE_LANE=7 python3 tools/tests/coop_space_host_ship_design.py
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

X = 0x2903A659  # the test's ship id, in both profiles (another creation in each)
BEM, UGROUP, UTYPE = 0x1A99B06B, 0x40656200, 0x476A98C7


def two_ufo_models():
    path = os.path.join(lane.game_dir(lane.LANE), 'Data', 'Spore_Content.package')
    out = []
    for e in ps.entries(path):
        if e['type'] == BEM and e['group'] == UGROUP:
            d = ps.read(path, e)
            if len(d) > 200 and all(d != o for o in out):
                out.append(d)
            if len(out) == 2:
                return out
    raise AssertionError('fewer than two UFO models in Spore_Content.package')


def fnv(data):
    h = 0x811C9DC5
    for c in data:
        h = ((h * 0x1000193) & 0xFFFFFFFF) ^ c
    return h


MODELS = []
_orig_restore = twoinst.restore_creations


def restore_with_ship(guest=True):
    _orig_restore(guest)
    for d, data in ((lane.HOST_DIR, MODELS[0]), (lane.GUEST_DIR, MODELS[1])):
        p = os.path.join(d, 'EditorSaves.package')
        base = os.path.join(fixtures_dir(), 'creations-host.package' if d == lane.HOST_DIR else 'creations-guest.package')
        recs = [r for r in read_pkg(p if os.path.exists(p) else base) if r[2] != X]
        recs.append((BEM, UGROUP, X, data))
        write_pkg(p, recs)


def model_hash(con, inst):
    m = re.search(r'model \w+ (\w+)', con.call(f'coop.creations modelhash {inst:08X} {UTYPE:08X} {UGROUP:08X}')['out'])
    return m.group(1) if m else '?'


def mirrors(con):
    out = con.call('coop.fleet list')['out']
    return [(int(i), t, pol, int(mk, 16)) for i, t, pol, mk in
            re.findall(r'ship #(\d+) \S+ type=(\d+) pol=(\w+) .* model=(\w+)', out) if int(i) >= 0]


def main():
    MODELS[:] = two_ufo_models()
    ha, hb = f'{fnv(MODELS[0]):08X}', f'{fnv(MODELS[1]):08X}'
    print(f'  fixture: ship {X:08X}: host model {ha}, guest model {hb}', flush=True)
    twoinst.restore_creations = restore_with_ship
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    ck(model_hash(host, X) == ha, f'the host\'s game answers its own model for {X:08X} ({model_hash(host, X)})')
    ck(model_hash(guest, X) == hb, f'the guest\'s game answers its own other model for {X:08X} ({model_hash(guest, X)})')
    print(' ', host.call(f'coop.fleet fakemodel 2 {X:08X}')['out'].strip(), flush=True)
    for args in ('3 own', '2', '2'):
        print('  host:', host.call(f'coop.fleet spawn {args}')['out'].strip(), flush=True)
        time.sleep(4)
    ms = []
    t0 = time.time()
    while time.time() - t0 < 30:
        ms = mirrors(guest)
        if len(ms) >= 2:
            break
        time.sleep(1)
    time.sleep(3)
    ms = mirrors(guest)
    hashes = [(m, model_hash(guest, m[3])) for m in ms]
    print('  guest mirrors:', hashes, flush=True)
    print('  guest:', guest.call('coop.creations designs')['out'].strip(), '|',
          re.search(r'zero_keys=\S+ design_waits=\S+ designs_used=\S+', guest.call('coop.fleet list')['out']), flush=True)
    host.call('coop.fleet fakemodel 0')
    ck(len(hashes) >= 2, f'the guest mirrors the host\'s ships ({len(hashes)})')
    ck(hashes and all(h == ha for _, h in hashes),
       f'every mirror shows the host\'s ship design ({[h for _, h in hashes]}, host {ha}, the guest\'s own {hb})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
