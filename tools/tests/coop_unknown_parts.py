#!/usr/bin/env python3
"""BUG-0452: a guest's game lacks a parts pack (Spore_Pack_03) and loads a host's Creature world whose
creature uses a part from that pack. The guest must not crash (the player report: 0xC0000005 at
0x41F3B, a model bake with a null property list, on every load of the host's Creature world). This is
the generic version of BUG-0341: the unknown part is not on the hard-coded Creepy & Cute list.

Host and Bob: lane A (SPORE_LANE, all packs). Carol: lane B (SPORE_LANE_B, default A + 1) with
`Data/Spore_Pack_03.package` moved aside for the test (put back at the end).
The host's My Creations: the fixture with 'Tiger Horse' (290229B9) changed to use a part from the pack.

  SPORE_LANE=4 SPORE_LANE_B=5 python3 tools/tests/coop_unknown_parts.py

Runs that must FAIL: SPORE_FIX_OFF=creations.unknown_parts (the fix off: Carol registers the species
and faults at 0x41F3B in the load). PASS: Carol does not crash, the unknown id is logged, and the
creature is shown as a fallback (the puppet falls back as for an unknown creation) or with a base
game part substituted."""
import os
import re
import shutil
import subprocess
import sys
import time

import lane
import twoinst
from coop_multi import join_cmd, start_extra
from twoinst import Checks, Console, start

TIGER = 0x290229B9
OLD_PART = bytes.fromhex('69e9add6')  # a part of Tiger Horse (PatchData), once in its model
PACK_PART = bytes.fromhex('d4d2a575')  # 0x75A5D2D4: a detail part only in Data/Spore_Pack_03.package
BEM = 0x1A99B06B
PACK_FILE = 'Spore_Pack_03.package'


def lane_b():
    """Carol's lane: SPORE_LANE_B, else the lab's lane outside its run (SPORE_FREE_LANE), else A + 1"""
    v = os.environ.get('SPORE_LANE_B') or os.environ.get('SPORE_FREE_LANE')
    return int(v) if v else lane.LANE + 1


def pack_path(b):
    return os.path.join(lane.game_dir(b), 'Data', PACK_FILE)


def clean_caches(d):
    """a fresh profile: a stale Sporepedia cache (Pollination.package) or a baked model under the
    host's key answers for 290229B9 and the guest's game then never uses the host's creation"""
    for f in ('Pollination.package', 'GraphicsCache.package', 'EditorSaves.package',
              'Planets.package', 'RigblockInfo.package'):
        try:
            os.remove(os.path.join(d, f))
        except OSError:
            pass
    shutil.rmtree(os.path.join(d, 'SporeCoop'), ignore_errors=True)


def patch_host_creations():
    """the host's My Creations: Tiger Horse with one part of Spore_Pack_03"""
    from coop_nest_species import read_pkg, write_pkg, fixtures_dir
    recs = read_pkg(os.path.join(fixtures_dir(), 'creations-host.package'))
    out, n = [], 0
    for t, g, i, data in recs:
        if t == BEM and i == TIGER:
            n = data.count(b'`b@' + OLD_PART)
            data = data.replace(b'`b@' + OLD_PART, b'`b@' + PACK_PART)
        out.append((t, g, i, data))
    assert n == 1, f'Tiger Horse model: {n} places of the part'
    write_pkg(os.path.join(lane.HOST_DIR, 'EditorSaves.package'), out)


def main():
    if os.environ.get('SPORE_FREE_LANE') == '' and not os.environ.get('SPORE_LANE_B'):
        print('RESULT: SKIP (no second lane free for the guest without the pack)')
        return 0
    b = lane_b()
    os.environ['SPORE_LANE_B'] = str(b)  # coop_multi.start_extra starts Carol on lane B
    assert b > 1 and b != lane.LANE, 'Carol needs a lane of its own (never lane 1: its pack is the real install)'
    pk = pack_path(b)
    off = pk + '.bug0452-off'
    if os.path.exists(off) and not os.path.exists(pk):
        os.rename(off, pk)  # an earlier run that died
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
    ck(h_av and h_av.group(1) == f'{TIGER:08X}', f'the host plays its pack creature ({h_av and h_av.group(1)})')
    time.sleep(5)
    hf = host.call('exc')['out']
    ck('0x0041F3B' not in hf, 'the host (with the pack) builds it without a fault')

    print(f'LAB-WATCH lane={b} since={time.time():.1f}', flush=True)
    os.rename(pk, off)
    try:
        appdata = lane.APPDATA.replace(f'/{lane.BOTTLE}/', f'/SporeLab{b}/' if b > 1 else '/Games/')
        clean_caches(os.path.join(appdata, 'SporeKit-mp1', 'Spore'))
        carol, carol_inst = start_extra(1)['Carol']
        for name in filter(None, os.environ.get('SPORE_FIX_OFF', '').split(',')):  # the run that must FAIL
            print('  carol:', carol.call(f'coop.fix {name.strip()} off')['out'].strip().splitlines()[0])
        carol.call(join_cmd('Carol'))
        t0 = time.time()
        crashed, playing, mode = False, False, ''
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
        time.sleep(8)  # the host's puppet is built on Carol's game (its species: the host's creature)
        out, cparts, bparts = '', '', bob.call(f'coop.creations parts {TIGER:08X}')['out'].strip()
        try:
            out = carol.call('exc')['out']
            mode = carol.call('game.mode')['out'].strip()
            cparts = carol.call(f'coop.creations parts {TIGER:08X}')['out'].strip()
            print('  carol:', carol.call('coop.creations')['out'].splitlines()[0][-90:])
        except (OSError, ValueError):
            crashed = True
        print('  carol faults:', (out.strip().splitlines() or ['-'])[0][:160], '| mode', mode[:40])
        print('  carol:', cparts, '| bob:', bparts)
        # BUG-0452: the guest logs the unknown part id and does not bake it
        log = lane.game_log(carol_inst)  # Carol's SporeKit log
        try:
            logtext = open(log, encoding='utf-8', errors='replace').read()
        except OSError:
            logtext = ''
        known = re.search(r'unknown here', logtext) or re.search(r'part 75A5D2D4', logtext)
        ck(not crashed, 'the guest without the pack did not crash')
        ck('0x0041F3B' not in out, 'no fault in the model bake (0x41F3B) on the guest without the pack')
        ck(playing, f"the guest without the pack plays the host's Creature world ({mode[:30]})")
        ck(known, 'the guest logs the unknown part id (creations: ... unknown here ...)')
        # the game's view: the model the guest's game reads for the host's creature has no unknown part
        # left (a base part substituted) or is not built (the fallback); Bob keeps the original part
        ck(re.search(r'\bspecies=1\b', cparts) or re.search(r'\bn=0\b', cparts) or 'none' in cparts,
           f"the guest's game builds the host's creature or shows the fallback ({cparts})")
        ck('total faults: 0' in out and 'Loading' not in mode, f'the guest without the pack has no fault ({mode[:30]})')
    finally:
        subprocess.run([lane.SPORE, 'kill'], env=dict(os.environ, SPORE_LANE=str(b)), capture_output=True)
        time.sleep(2)
        if os.path.exists(off):
            os.rename(off, pk)
    ck(bob.call('coop.avatar')['ok'], 'Bob (with the pack) still plays in the world')
    ck.faults(host=host, guest=bob)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
