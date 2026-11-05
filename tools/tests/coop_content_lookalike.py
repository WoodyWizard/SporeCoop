#!/usr/bin/env python3
"""BUG-0559 (class BUG-0424): a guest whose game lacks the Creepy & Cute pack must see the host's
creature with parts of the same kind that its own game has (an eye stays an eye, a mouth a mouth,
a foot a foot, a detail a detail of a similar size), never as blocks, and with the host's colours:
a paint its own game has (a base game paint of the Cell stage, `skinpaint_cell_stripes`) must stay as
it is, only a paint it lacks is replaced.

Host and Bob: lane A (SPORE_LANE, all packs). Carol: lane B (SPORE_LANE_B / SPORE_FREE_LANE) with
`bp1content/BoosterPack_01.package` moved aside for the test (put back at the end).
The host's My Creations: the fixture's Tiger Horse (290229B9) with an eye, a mouth and eight details of
Creepy & Cute, both feet of Spore_Pack_03 (also moved aside on Carol's lane), its base paint the base game's cell stripes and its detail paint the
pack's "BP1 Skinned Beast Detail".

Checked on Carol (the game's view: `coop.creations partlist`, the part ids and kinds of the model her
game reads for the host's creature, and `coop.creations paints`): every part is one her game has, each
replaced part has the kind of the host's part, the base paint is the host's own, the species is built,
no fault. Screenshots of Carol's view of the host (shots/).

  SPORE_LANE=9 SPORE_LANE_B=10 python3 tools/tests/coop_content_lookalike.py

Runs that must FAIL: SPORE_FIX_OFF=content.lookalike (the 0.7.6 choice: the fixed tables and the
paint lists of the mod: the Spore_Pack_03 foot becomes a detail, the base game's cell paint is replaced)
and SPORE_FIX_OFF=content.species_data (the species data of the host's creature still names the pack's
parts: Carol's game draws them as blocks)."""
import os
import re
import shutil
import subprocess
import sys
import time

import lane
import twoinst
from coop_multi import join_cmd, start_extra
from twoinst import Checks, shot, start

TIGER = 0x290229B9
BEM = 0x1A99B06B
PACK_FILES = (os.path.join('bp1content', 'BoosterPack_01.package'), os.path.join('Data', 'Spore_Pack_03.package'))
# Tiger Horse's own part -> a Creepy & Cute part of the same kind (each replaced everywhere in the model)
SWAPS = {
    0x4B1AE65C: (0x7728945C, 'eye'),      # eye -> ce_sense_eye_horror_02
    0xC9F34440: (0xF6DE5C63, 'mouth'),    # mouth -> ce_mouth_horror_carnivore_02
    0x08504962: (0xF54F0B90, 'foot'),     # both feet -> a foot of Data/Spore_Pack_03.package (not on any table)
    0x34265C0D: (0xB4BD7DA7, 'detail'),   # six details -> ce_detail_cute_01
    0xEEAB640C: (0xA115CFC8, 'detail'),   # two details -> ce_detail_horror_01
    0xA0DE03CF: (0xC61D94A9, 'detail'),   # a detail -> a pack detail
}
# the model's base / coat / detail paint effects (header bytes 26..37)
OLD_PAINTS = bytes.fromhex('1be2d1ee231f8152ce913610')
CELL_STRIPES = 0xE4C02CCE                 # skinpaint_cell_stripes: a base game paint (PatchData effect directory)
CC_DETAIL = 0xC78AAED4                    # BP1 Skinned Beast Detail: the pack's
NEW_PAINTS = CELL_STRIPES.to_bytes(4, 'little') + bytes.fromhex('231f8152') + CC_DETAIL.to_bytes(4, 'little')


def lane_b():
    v = os.environ.get('SPORE_LANE_B') or os.environ.get('SPORE_FREE_LANE')
    return int(v) if v else lane.LANE + 1


def swaps():
    """CONTENT_SWAPS=<hex,hex>: only these of Tiger Horse's parts are swapped (looking for one that is drawn badly)"""
    items = [x for x in os.environ.get('CONTENT_SWAPS', '').split(',') if x]
    pairs = dict((int(a, 16), (int(b, 16), 'given')) for a, b in (x.split('=') for x in items if '=' in x))
    if pairs:  # old=new: any part in place of Tiger Horse's (e.g. a base game part, to see how it is drawn)
        return pairs
    only = [int(x, 16) for x in items]
    return {k: v for k, v in SWAPS.items() if not only or k in only}


def le(v):
    return v.to_bytes(4, 'little')


def patch_host_creations():
    from coop_nest_species import read_pkg, write_pkg, fixtures_dir
    recs = read_pkg(os.path.join(fixtures_dir(), 'creations-host.package'))
    out = []
    for t, g, i, data in recs:
        if t == BEM and i == TIGER:
            for old, (new, _) in swaps().items():
                n = data.count(b'`b@' + le(old))
                assert n >= 1, f'Tiger Horse model: part {old:08X} not found'
                data = data.replace(b'`b@' + le(old), b'`b@' + le(new))
            assert data[26:38] == OLD_PAINTS, 'Tiger Horse model: the effects are not at header byte 26'
            data = data[:26] + NEW_PAINTS + data[38:]
        out.append((t, g, i, data))
    write_pkg(os.path.join(lane.HOST_DIR, 'EditorSaves.package'), out)


def clean_caches(d):
    for f in ('Pollination.package', 'GraphicsCache.package', 'EditorSaves.package',
              'Planets.package', 'RigblockInfo.package'):
        try:
            os.remove(os.path.join(d, f))
        except OSError:
            pass
    shutil.rmtree(os.path.join(d, 'SporeCoop'), ignore_errors=True)


def avatar_pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return [float(v) for v in m.groups()] if m else None


def in_view(con, dist, fx=0.5):
    """a point `dist` m past the own creature along the ray through the screen at fx of its width"""
    m = re.search(r'viewport x=\d+ y=\d+ w=(\d+) h=(\d+)', con.call('coop.cam')['out'])
    w, h = (int(m.group(1)), int(m.group(2))) if m else (1440, 1080)
    o = con.call(f'coop.cam {int(w * fx)} {h * 3 // 5}')['out']
    r = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) dir=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', o)
    if not r:
        return None
    v = [float(x) for x in r.groups()]
    a = avatar_pos(con)
    if a:  # past the own creature: the camera is a few metres behind it
        cam = v[:3]
        dist += sum((a[i] - cam[i]) * v[3 + i] for i in range(3))
    return [v[i] + v[3 + i] * dist for i in range(3)]


def partlist(con, key):
    """{part id: kind} of the model the game reads for key (coop.creations partlist)"""
    o = con.call(f'coop.creations partlist {key:08X}')['out']
    return o.strip(), dict((int(a, 16), k) for a, k in re.findall(r'\b([0-9A-F]{8})=(\w+)', o))


def main():
    if os.environ.get('SPORE_FREE_LANE') == '' and not os.environ.get('SPORE_LANE_B'):
        print('RESULT: SKIP (no second lane free for the guest without the pack)')
        return 0
    b = lane_b()
    os.environ['SPORE_LANE_B'] = str(b)
    assert b > 1 and b != lane.LANE, 'Carol needs a lane of its own (never lane 1: its pack is the real install)'
    packs = [os.path.join(lane.game_dir(b), f) for f in PACK_FILES]
    for pk in packs:
        if os.path.exists(pk + '.bug0559-off') and not os.path.exists(pk):
            os.rename(pk + '.bug0559-off', pk)  # an earlier run that died
    orig = twoinst.restore_creations

    def restore(guest=True):
        orig(guest)
        patch_host_creations()
    twoinst.restore_creations = restore
    clean_caches(lane.HOST_DIR)
    clean_caches(lane.GUEST_DIR)
    host, bob = start('creature')
    ck = Checks()
    print(' ', host.call(f'coop.creations choose creature {TIGER:08X}')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 30 and f'key=0x{TIGER:08X}' not in host.call('coop.avatar')['out']:
        time.sleep(1)
    h_av = re.search(r'key=0x([0-9A-F]{8})', host.call('coop.avatar')['out'])
    ck(h_av and h_av.group(1) == f'{TIGER:08X}', f'the host plays its creature with the pack\'s parts ({h_av and h_av.group(1)})')
    hl, hparts = partlist(host, TIGER)
    print('  host:', hl)
    print(f'LAB-WATCH lane={b} since={time.time():.1f}', flush=True)
    for pk in packs:
        os.rename(pk, pk + '.bug0559-off')
    try:
        appdata = lane.APPDATA.replace(f'/{lane.BOTTLE}/', f'/SporeLab{b}/' if b > 1 else '/Games/')
        clean_caches(os.path.join(appdata, 'SporeKit-mp1', 'Spore'))
        carol, carol_inst = start_extra(1)['Carol']
        for name in filter(None, os.environ.get('SPORE_FIX_OFF', '').split(',')):  # the run that must FAIL
            print('  carol:', carol.call(f'coop.fix {name.strip()} off')['out'].strip().splitlines()[0])
        carol.call(join_cmd('Carol'))
        t0 = time.time()
        crashed, playing = False, False
        while time.time() - t0 < 150:
            f1 = twoinst.host_frame(host)
            time.sleep(1)
            if twoinst.host_frame(host) == f1:
                host.call('key enter')  # the "game saved" box of the share
            try:
                mode = carol.call('game.mode')['out'].strip()
                av = carol.call('coop.avatar')
            except (OSError, ValueError):
                crashed = True
                break
            if 'Creature' in mode and av.get('ok') and 'key=0x' in av.get('out', '') and time.time() - t0 > 20:
                playing = True
                break
        ck(not crashed, 'the guest without the pack did not crash')
        ck(playing, "the guest without the pack plays the host's Creature world")
        time.sleep(8)
        cl, cparts = partlist(carol, TIGER)
        paints = carol.call(f'coop.creations paints {TIGER:08X}')['out'].strip()
        species = carol.call(f'coop.creations parts {TIGER:08X}')['out'].strip()
        print('  carol:', cl)
        print('  carol:', paints, '|', species)
        # what each guest shows of the host: the host walks into the middle of that guest's view
        for name, con in (('carol', carol), ('bob', bob)):
            p, q = in_view(con, 3, 0.35), in_view(con, 3, 0.65)
            if p and q:
                print(f'  host goto ({name}):', host.call(f'coop.abilities goto {p[0]} {p[1]} {p[2]}')['out'].strip())
                if con is carol:
                    print(f'  bob goto ({name}):', bob.call(f'coop.abilities goto {q[0]} {q[1]} {q[2]}')['out'].strip())
                time.sleep(4)
                shot(con, f'{name}_sees_host.png')
        # BUG-0559, the blocks: the species data Carol's game builds the host's species from (the host's
        # own creation of its world, under Carol's key for it) names no part her game lacks
        m = re.search(r'host species %08X = ([0-9A-F]{8}) here' % TIGER, carol.call('coop.creations')['out'])
        local = int(m.group(1), 16) if m else 0
        sl, _ = partlist(carol, local) if local else ('none', {})
        print('  carol (the host\'s species):', sl)
        ck(local and re.search(r'species_data=[1-9]', sl) and re.search(r'species_data_unknown=0\b', sl),
           f"the species data of the host's species on Carol names only parts her game has ({sl[-50:]})")
        # the game's view on Carol: no part her game lacks, each replaced part of the host's kind
        ck(re.search(r'\bunknown=0\b', species) and re.search(r'\bspecies=1\b', species),
           f"Carol's game builds the host's creature with parts it has ({species})")
        for old, (new, kind) in swaps().items():
            ck(new not in cparts, f'the pack part {new:08X} is not in the model Carol\'s game reads')
        kinds = {}
        for pid, kind in cparts.items():
            kinds[kind] = kinds.get(kind, 0) + 1
        hk = {}
        for pid, kind in hparts.items():
            hk[kind] = hk.get(kind, 0) + 1
        for kind in ('eye', 'mouth', 'foot'):
            ck(hk.get(kind, 0) >= 1 and kinds.get(kind, 0) == hk.get(kind, 0),
               f"the host's {kind} is a {kind} on Carol (host {hk.get(kind, 0)} kinds, Carol {kinds.get(kind, 0)})")
        ck(f' {CELL_STRIPES:08X} ' in ' ' + paints + ' ',
           f"Carol's game paints the host's creature with the host's own base paint, a base game paint ({paints})")
        ck(f' {CC_DETAIL:08X} ' not in ' ' + paints + ' ', f'the pack\'s detail paint is replaced on Carol ({paints})')
        if os.environ.get('CONTENT_HOLD'):  # manual looks: keep the games for that many seconds
            print(f'  holding: carol instance {carol_inst}', flush=True)
            time.sleep(float(os.environ['CONTENT_HOLD']))
        out = carol.call('exc')['out']
        ck('total faults: 0' in out, f'the guest without the pack has no fault ({out.strip().splitlines()[:1]})')
    finally:
        subprocess.run([lane.SPORE, 'kill'], env=dict(os.environ, SPORE_LANE=str(b)), capture_output=True)
        time.sleep(2)
        for pk in packs:
            if os.path.exists(pk + '.bug0559-off'):
                os.rename(pk + '.bug0559-off', pk)
    ck(bob.call('coop.avatar')['ok'], 'Bob (with the pack) still plays in the world')
    ck.faults(host=host, guest=bob)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
