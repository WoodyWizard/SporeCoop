#!/usr/bin/env python3
"""BUG-0341: a guest whose game lacks the Creepy & Cute parts pack (Steam sells it apart) joins a
host that plays an own creature with a part of that pack. The guest's game must not crash (the
player report: 0xC0000005 at 0x441F3B, a model bake with a null property list, on every load of
the host's Creature world).

Host and Bob: lane A (SPORE_LANE, both packs). Carol: lane B (SPORE_LANE_B, default A + 1) with
`bp1content/BoosterPack_01.package` moved aside for the test (put back at the end).
The host's My Creations: the fixture with 'Tiger Horse' (290229B9) changed to use a C&C part.

  SPORE_LANE=5 SPORE_LANE_B=6 python3 tools/tests/coop_guest_missing_pack.py

Since the owner's choice (fix/w24-parts) Carol's game shows the creature's pack parts as similar base
game parts (content_packs.h kCreepyCuteSubstitutes) and plays the world: checked with the game's view
of the model (`coop.creations parts`: no pack part left, the species profile built) on Carol and the
original part on Bob.

Runs that must FAIL: SPORE_FIX_OFF=creations.part_substitute (the refusal of BUG-0341: Carol is told
and stays in the galaxy menu); SPORE_FIX_OFF=creations.missing_parts,creations.part_substitute (0.7.3.1:
Carol faults at 0x441F3B in the load and stays in it under Wine; Windows crashes)."""
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
CC_PART = bytes.fromhex('7f0aa617')  # 0x17A60A7F: a part only in bp1content/BoosterPack_01.package
BEM = 0x1A99B06B


def lane_b():
    """Carol's lane: SPORE_LANE_B, else the lab's lane outside its run (SPORE_FREE_LANE), else A + 1"""
    v = os.environ.get('SPORE_LANE_B') or os.environ.get('SPORE_FREE_LANE')
    return int(v) if v else lane.LANE + 1


def pack_path(b):
    return os.path.join(lane.game_dir(b), 'bp1content', 'BoosterPack_01.package')


def patch_host_creations():
    """the host's My Creations: Tiger Horse with one part of the C&C pack"""
    from coop_nest_species import read_pkg, write_pkg, fixtures_dir
    recs = read_pkg(os.path.join(fixtures_dir(), 'creations-host.package'))
    out, n = [], 0
    for t, g, i, data in recs:
        if t == BEM and i == TIGER:
            n = data.count(b'`b@' + OLD_PART)
            data = data.replace(b'`b@' + OLD_PART, b'`b@' + CC_PART)
        out.append((t, g, i, data))
    assert n == 1, f'Tiger Horse model: {n} places of the part'
    write_pkg(os.path.join(lane.HOST_DIR, 'EditorSaves.package'), out)


def main():
    if os.environ.get('SPORE_FREE_LANE') == '' and not os.environ.get('SPORE_LANE_B'):
        print('RESULT: SKIP (no second lane free for the guest without the pack)')
        return 0
    b = lane_b()
    os.environ['SPORE_LANE_B'] = str(b)  # coop_multi.start_extra starts Carol on lane B
    assert b > 1 and b != lane.LANE, 'Carol needs a lane of her own (never lane 1: its pack is the real install)'
    pk = pack_path(b)
    off = pk + '.bug0341-off'
    if os.path.exists(off) and not os.path.exists(pk):
        os.rename(off, pk)  # an earlier run that died
    orig = twoinst.restore_creations

    def restore(guest=True):
        orig(guest)
        patch_host_creations()
    twoinst.restore_creations = restore
    host, bob = start('creature')
    ck = Checks()
    print(' ', host.call(f'coop.creations choose creature {TIGER:08X}')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 30 and f'key=0x{TIGER:08X}' not in host.call('coop.avatar')['out']:
        time.sleep(1)
    h_av = re.search(r'key=0x([0-9A-F]{8})', host.call('coop.avatar')['out'])
    ck(h_av and h_av.group(1) == f'{TIGER:08X}', f'the host plays its C&C creature ({h_av and h_av.group(1)})')
    time.sleep(5)
    hf = host.call('exc')['out']
    ck('0x00441F3B' not in hf, 'the host (with the pack) builds it without a fault')

    print(f'LAB-WATCH lane={b} since={time.time():.1f}', flush=True)
    os.rename(pk, off)
    try:
        carol = start_extra(1)['Carol'][0]
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
        ck(not crashed, 'the guest without the pack did not crash')
        ck('0x00441F3B' not in out, 'no fault in the model bake (0x441F3B) on the guest without the pack')
        ck(playing, f"the guest without the pack plays the host's Creature world ({mode[:30]})")
        # the game's view: the model the guest's game reads for the host's creature has no pack part
        # left (shown with base parts) and its species profile is built
        ck(re.search(r'\bcc=0\b', cparts) and re.search(r'\bspecies=1\b', cparts) and not re.search(r'\bn=0\b', cparts),
           f"the guest's game builds the host's creature with base parts only ({cparts})")
        ck(re.search(r'\bcc=1\b', bparts) and re.search(r'\bspecies=1\b', bparts),
           f"Bob (with the pack) keeps the original Creepy & Cute part ({bparts})")
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
