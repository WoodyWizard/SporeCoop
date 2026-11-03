#!/usr/bin/env python3
"""BUG-0469 (b): Space, a host ship whose model names a part this game does not have.

The host mirrors its NPC ships on the guest with the model key the host's ship carries. BUG-0323
checked that key only: a key the guest's game has, whose model names a part of a parts pack the guest
does not have (a ship design of another profile's Sporepedia, a pack), was baked on the guest and
faulted at 0x41F3B. Forced here: the guest's (Carol, lane B, `Data/Spore_Pack_03.package` moved aside
for the test) My Creations get a base game UFO model with one part id of that pack (model_fixture.py,
instance 0x29030002), and `coop.fleet fakemodel 2 29030002` makes the host send that model key for
every ship (as if its Sporepedia held it).

PASS: the guest does not fault, logs the unknown id once ("creations: content: ufo model ..."), builds
the mirror from a copy with a base game part (creations::ensure_model) and shows it.
FAIL (`SPORE_FIX_OFF=content.all_kinds`, the switch off: 0.7.3.5): the guest spawns the ship with the
pack part and faults.

  SPORE_LANE=4 SPORE_LANE_B=5 python3 tools/tests/coop_content_space.py
"""
import os
import re
import subprocess
import time

import lane
import model_fixture as mf
import twoinst
from coop_multi import join_cmd, start_extra
from coop_unknown_parts import clean_caches, lane_b, pack_path
from twoinst import Checks, start

SHIP = 0x29030002  # the test's own ship model: a base game UFO model with one part of the pack
PACK_PART = '75A5D2D4'


def fleet(con):
    """(mirrors, created, unknown_models, content_checked) of the guest's space status"""
    out = con.call('coop.fleet list')['out']
    m = re.search(r'mirrors=(\d+) created=(\d+) .*unknown_models=(\d+) content_checked=(\d+)', out)
    if not m:
        return (-1, -1, -1, -1)
    return tuple(int(x) for x in m.groups())


def main():
    if os.environ.get('SPORE_FREE_LANE') == '' and not os.environ.get('SPORE_LANE_B'):
        print('RESULT: SKIP (no second lane free for the guest without the pack)')
        return 0
    b = lane_b()
    os.environ['SPORE_LANE_B'] = str(b)  # coop_multi.start_extra starts Carol on lane B
    assert b > 1 and b != lane.LANE, 'Carol needs a lane of its own (never lane 1: its pack is the real install)'
    pk = pack_path(b)
    off = pk + '.bug0469-off'
    if os.path.exists(off) and not os.path.exists(pk):
        os.rename(off, pk)  # an earlier run that died
    orig = twoinst.restore_creations

    def restore(guest=True):
        # Bob (lane A, with the pack) gets the same ship model: his game has the part, so he mirrors the
        # host's ships with that key as they are and the sync-check finds nothing missing (the fixture
        # model is one test data set, both guests have the key)
        orig(guest)
        mf.install(lane.GUEST_DIR, 'ufo', SHIP)
    twoinst.restore_creations = restore
    host, bob = start('space')
    ck = Checks()
    time.sleep(3)
    appdata = lane.APPDATA.replace(f'/{lane.BOTTLE}/', f'/SporeLab{b}/' if b > 1 else '/Games/')
    carol_dir = os.path.join(appdata, 'SporeKit-mp1', 'Spore')
    clean_caches(carol_dir)  # a fresh profile: no cache of the host's world answers for it
    data = mf.install(carol_dir, 'ufo', SHIP)  # Carol's My Creations: a ship model with the pack part
    print(f'  fixture: ship model {SHIP:08X}, {len(data)} bytes, one part {PACK_PART} of the pack')

    print(f'LAB-WATCH lane={b} since={time.time():.1f}', flush=True)
    os.rename(pk, off)  # Carol's game does not have the pack now
    try:
        carol, carol_inst = start_extra(1)['Carol']
        for name in filter(None, os.environ.get('SPORE_FIX_OFF', '').split(',')):  # the run that must FAIL
            print('  carol:', carol.call(f'coop.fix {name.strip()} off')['out'].strip().splitlines()[0])
        carol.call(join_cmd('Carol'))
        t0 = time.time()
        in_world = False
        while time.time() - t0 < 180:
            f1 = twoinst.host_frame(host)
            time.sleep(1)
            if twoinst.host_frame(host) == f1:
                host.call('key enter')  # the "game saved" box of the share
            try:
                if 'Space' in carol.call('game.mode')['out'] and carol.call('coop.avatar').get('ok'):
                    in_world = True
                    break
            except (OSError, ValueError):
                break
        ck(in_world, "Carol without the pack plays the host's space world")
        print(' ', host.call(f'coop.fleet fakemodel 2 {SHIP:08X}')['out'].strip())
        before, after = (-1, -1, -1, -1), (-1, -1, -1, -1)
        out, content, crashed = '', '', False
        try:
            before = fleet(carol)
            for args in ('2', '3 own', '2'):  # NPC ships next to the host: its mirrors on the guest
                print('  host:', host.call(f'coop.fleet spawn {args}')['out'].strip())
                time.sleep(4)
            time.sleep(6)
            out = carol.call('exc')['out']
            content = carol.call('coop.creations content')['out'].strip()
            after = fleet(carol)
        except (OSError, ValueError):
            crashed = True
        print('  carol faults:', (out.strip().splitlines() or ['-'])[0][:160])
        for line in out.splitlines()[1:4]:  # the faults themselves (an address and the data)
            print('   ', line[:160])
        print('  carol content:', content, '| fleet', before, '->', after)
        log = lane.game_log(carol_inst)  # Carol's SporeKit log
        try:
            logtext = open(log, encoding='utf-8', errors='replace').read()
        except OSError:
            logtext = ''
        ck(not crashed, 'the guest without the pack did not crash')
        # the bake of a model with a part this game does not have: the creature bake faults at
        # 0x00441F3B (BUG-0452), the vehicle and UFO bake at 0x0046C82A (FUN_0046C4D0, BUG-0469)
        ck('0x0041F3B' not in out and '0x0046C82A' not in out and 'total faults: 0' in out,
           f"no fault in the model bake (0x41F3B / 0x46C82A) on the guest without the pack "
           f"({(out.strip().splitlines() or ['-'])[0][:80]})")
        ck(re.search(rf'content: ufo model {SHIP:08X} part {PACK_PART} unknown here', logtext) is not None,
           'the guest logs the unknown part id once (creations: content: ufo model ... part ...)')
        ck(re.search(r'substituted=[1-9]', content) is not None,
           f'the model was built from a copy with a substitute (coop.creations content: {content})')
        ck(re.search(r'refused=0', content) is not None, f'nothing was refused ({content})')
        ck(after[3] > 0, f'the guest checked the parts of the ship model (content_checked={after[3]})')
        ck(after[1] > before[1] and after[0] > 0, f'the guest shows the mirror ships ({before} -> {after})')
        ck(after[2] == 0, f'the model key was known here (no unknown model: {after})')
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
