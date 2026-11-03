#!/usr/bin/env python3
"""BUG-0469 (a): Civ, a mirror vehicle whose design model names a part this game does not have.

The host plays the civ world; the guest (Carol, lane B, with `Data/Spore_Pack_03.package` moved aside
for the test) mirrors the host's vehicles with the design of its own copy of that civilization
(`CreateVehicle` with the key of `GetDesignKey`). When that design's model names a part the guest's
game does not have, the mirror vehicle's model bake faulted at 0x41F3B on the guest (the fault of
BUG-0452, in the civ mirror path). Forced here: the guest's My Creations get a base game vehicle model
with one part id of that pack (model_fixture.py, instance 0x29030001) and `coop.civ fakeveh` makes
every mirror vehicle the guest creates use it as its design.

PASS: the guest does not fault, logs the unknown id once ("creations: content: vehicle model ..."), the
model is built from a copy with a base game part (creations::ensure_model), and the mirror is shown.
FAIL (`SPORE_FIX_OFF=content.all_kinds`, the switch off: 0.7.3.5): the guest builds the model with the
pack part and faults at 0x41F3B.

  SPORE_LANE=4 SPORE_LANE_B=5 python3 tools/tests/coop_content_civ.py
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

DESIGN = 0x29030001  # the test's own vehicle design: a base game model with one part of the pack
PACK_PART = '75A5D2D4'


def player_city(con):
    """the first city of the player (the one the city menu of the host may use)"""
    for line in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'city (-?\d+) \S+ pol=\w+ \(player\)', line)
        if m:
            return int(m.group(1))
    return None


def buildings(con, city):
    """the buildings of that city (the game's own list)"""
    for line in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'city (-?\d+) \S+ pol=\w+(?: \(player\))? owner=-?\d+ buildings=(\d+)', line)
        if m and int(m.group(1)) == city:
            return int(m.group(2))
    return -1


def mirrors(con):
    """(mirrors, created) of the guest's civ status: the vehicles it mirrors and how many it made"""
    out = con.call('coop.civ')['out']
    m = re.search(r'mirrors=(\d+) created=(\d+)', out)
    return (int(m.group(1)), int(m.group(2))) if m else (-1, -1)


def status(con, key):
    m = re.search(key + r'=(\d+)', con.call('coop.civ tables')['out'])
    return int(m.group(1)) if m else -1


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
    host, bob = start('civ')
    ck = Checks()
    time.sleep(3)
    appdata = lane.APPDATA.replace(f'/{lane.BOTTLE}/', f'/SporeLab{b}/' if b > 1 else '/Games/')
    carol_dir = os.path.join(appdata, 'SporeKit-mp1', 'Spore')
    clean_caches(carol_dir)  # a fresh profile: no cache of the host's world answers for it
    data = mf.install(carol_dir, 'vehicle', DESIGN)  # Carol's My Creations: a vehicle model with the pack part
    print(f'  fixture: vehicle model {DESIGN:08X}, {len(data)} bytes, one part {PACK_PART} of the pack')
    city = player_city(host)
    ck(city is not None, f'the host plays a civilization with its own city ({city})')

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
                if 'Civ' in carol.call('game.mode')['out'] and carol.call('coop.avatar').get('ok'):
                    in_world = True
                    break
            except (OSError, ValueError):
                break
        ck(in_world, "Carol without the pack plays the host's civilization")
        print(' ', carol.call(f'coop.civ fakeveh {DESIGN:08X}')['out'].strip())
        before, after = (-1, -1), (-1, -1)
        out, content, designs, crashed = '', '', 0, False
        b_before, b_after, designd = -1, -1, ''
        try:
            before = mirrors(carol)
            # BUG-0469: a building the host's game makes, built here from the civ's own building design
            # (SetBuildingModel reads that key itself, so the guest checks it before it builds)
            b_before = buildings(carol, city) if city is not None else -1
            if city is not None:
                print('  host:', host.call(f'coop.civ build {city} house')['out'].strip())
            time.sleep(4)
            b_after = buildings(carol, city) if city is not None else -1
            designd = carol.call(f'coop.civ design {city} house')['out'].strip() if city is not None else ""
            # the host buys vehicles: with the design forced the guest binds no local vehicle, it creates
            # a mirror of each one (with the forced design key: the model with the pack part)
            for _ in range(5):
                if city is not None:
                    print('  host:', host.call(f'coop.civ buy {city} 0')['out'].strip())
                time.sleep(3)
                designs = status(carol, 'mirror_designs')
                if designs > 0:
                    break
            time.sleep(4)
            out = carol.call('exc')['out']
            content = carol.call('coop.creations content')['out'].strip()
            after = mirrors(carol)
        except (OSError, ValueError):
            crashed = True
            after = before
        print('  carol faults:', (out.strip().splitlines() or ['-'])[0][:160])
        for line in out.splitlines()[1:4]:  # the faults themselves (an address and the data)
            print('   ', line[:160])
        print('  carol content:', content, '| mirror designs', designs, '| mirrors created', before, '->', after,
              '| buildings', b_before, '->', b_after)
        print('  carol design:', ' | '.join(designd.splitlines())[:200])
        log = lane.game_log(carol_inst)  # Carol's SporeKit log
        try:
            logtext = open(log, encoding='utf-8', errors='replace').read()
        except OSError:
            logtext = ''
        ck(not crashed, 'the guest without the pack did not crash')
        ck('0x0041F3B' not in out and 'total faults: 0' in out,
           f"no fault in the model bake (0x41F3B) on the guest without the pack ({(out.strip().splitlines() or ['-'])[0][:80]})")
        ck(re.search(rf'content: vehicle model {DESIGN:08X} part {PACK_PART} unknown here', logtext) is not None,
           'the guest logs the unknown part id once (creations: content: vehicle model ... part ...)')
        ck(re.search(r'substituted=[1-9]', content) is not None,
           f'the model was built from a copy with a substitute (coop.creations content: {content})')
        ck(re.search(r'refused=0', content) is not None, f'nothing was refused ({content})')
        ck(designs > 0, f'the guest built a mirror vehicle from the design (mirror_designs={designs})')
        ck(b_after > b_before, f"the guest built the host's building from its own design ({b_before} -> {b_after})")
        ck(b_after >= 0 and 'refused=0' in content, f'nothing was refused ({content})')
        # the guest's civ has the design of the building it just built (the shared world's) or the game
        # generates it here: the first is checked with its model, the second is no other game's content
        if designd and 'no design here' not in designd:
            ck('model ok' in designd, f'the guest checked the building design it has ({designd.splitlines()[-1][:120]})')
        else:
            print('  (the guest had no design for houses: its game generates one, nothing to check)')
        ck(after[1] > before[1], f'the guest made a new mirror vehicle ({before} -> {after})')
        ck(after[0] > before[0], f'the guest shows its mirror vehicles ({before} -> {after})')
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
