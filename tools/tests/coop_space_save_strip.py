#!/usr/bin/env python3
"""Space: a guest's save holds none of the ships the mod made in its game (BUG-0522).

A Space guest shows the partners as Defender UFOs of its own empire (the puppets, modes.cpp
space_spawn: SpawnUFO, invincible) and the host's NPC ships as mirrors (squadron.cpp guest_state).
They are the session's, not the guest's world (CLAUDE.md 2): a save of the guest must not hold them.

1. The host plays the `space` fixture (Bob joins on the main lane); Carol joins on a second lane and
   loads the host's world. The host's game makes a Defender of its empire next to its ship
   (`coop.fleet spawn 6 own`): Carol mirrors it.
2. Carol has the puppets of the host and Bob (`coop.fleet puppet 0|1`) and the mirror, next to her in
   the home system.
3. Carol saves with the game's own save (`coop.save`, off-planet), leaves the session; her game is
   restarted WITHOUT a co-op session and loads that save.
4. The loaded world: no invincible Defender of the player's empire (a puppet), no ship of the mirror's
   or a puppet's type and model where it stood (5 m), no fault for 10 s. After the save (step 3) the
   puppets and the mirror are still in Carol's session.
PASS: 2 to 4. SPORE_FIX_OFF=space.save_strip: the loaded world has them (FAIL).

  SPORE_LANE=4 SPORE_FREE_LANE=5 python3 tools/tests/coop_space_save_strip.py
"""
import math
import os
import re
import subprocess
import time

import lane
import twoinst
from coop_content_civ import restart_without_session, save_slot, wait_saved
from coop_creature_e84 import close_boxes
from coop_multi import join_cmd, start_extra
from coop_unknown_parts import clean_caches, lane_b
from twoinst import Checks, start

SHIP = re.compile(r'ship #(-?\d+) (\S+) type=(\d+) pol=(\w+) hp=\S+( \(me\))? pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) '
                  r'inv=(\d) model=(\w+)( puppet)?')


def fleet(con):
    """[dict(id, ptr, type, pol, me, pos, inv, model, puppet)] of the game's UFO list"""
    out = []
    for m in SHIP.finditer(con.call('coop.fleet list')['out']):
        out.append(dict(id=int(m.group(1)), ptr=m.group(2), type=int(m.group(3)), pol=m.group(4),
                        me=m.group(5) is not None, pos=tuple(float(m.group(i)) for i in (6, 7, 8)),
                        inv=m.group(9) == '1', model=m.group(10), puppet=m.group(11) is not None))
    return out


def show(f):
    return [(s['id'], s['type'], s['pol'][-3:], 'me' if s['me'] else 'P' if s['puppet'] else 'M' if s['id'] >= 0 else '-',
             tuple(round(v) for v in s['pos'])) for s in f]


def ours(f):
    """Carol's ships of the session: the puppets and the host's mirrors"""
    return [s for s in f if not s['me'] and (s['puppet'] or s['id'] >= 0)]


def main():
    if os.environ.get('SPORE_FREE_LANE') == '' and not os.environ.get('SPORE_LANE_B'):
        print('RESULT: SKIP (no second lane free for the guest that reloads)')
        return 0
    b = lane_b()
    os.environ['SPORE_LANE_B'] = str(b)
    assert b > 1 and b != lane.LANE, 'Carol needs a lane of its own'
    host, bob = start('space')
    ck = Checks()
    time.sleep(3)
    appdata = lane.APPDATA.replace(f'/{lane.BOTTLE}/', f'/SporeLab{b}/' if b > 1 else '/Games/')
    carol_dir = os.path.join(appdata, 'SporeKit-mp1', 'Spore')
    clean_caches(carol_dir)
    print(f'LAB-WATCH lane={b} since={time.time():.1f}', flush=True)
    try:
        carol, carol_inst = start_extra(1)['Carol']
        for name in filter(None, os.environ.get('SPORE_FIX_OFF', '').split(',')):
            print('  carol:', carol.call(f'coop.fix {name.strip()} off')['out'].strip().splitlines()[0])
        carol.call(join_cmd('Carol'))
        t0, in_world = time.time(), False
        while time.time() - t0 < 180:
            f1 = twoinst.host_frame(host)
            time.sleep(1)
            if twoinst.host_frame(host) == f1:
                host.call('key enter')  # the "game saved" box of the share
            try:
                if 'Space' in carol.call('game.mode')['out'] and 'place 8' in carol.call('coop.space star')['out']:
                    in_world = True
                    break
            except (OSError, ValueError):
                break
        ck(in_world, "Carol plays the host's world in its star system")
        close_boxes(host, 4)
        print('  host:', host.call('coop.fleet spawn 6 own')['out'].strip(), flush=True)
        f, t0 = [], time.time()
        while time.time() - t0 < 60:
            f = fleet(carol)
            if sum(s['puppet'] for s in f) >= 2 and any(s['id'] >= 0 and s['type'] == 6 for s in f):
                break
            time.sleep(1)
        print(f'  Carol ships: {show(f)} (P puppet, M mirror)', flush=True)
        mine = ours(f)
        pups = [s for s in mine if s['puppet']]
        mirrors = [s for s in mine if not s['puppet']]
        ck(len(pups) >= 2, f'Carol\'s game shows the host and Bob as puppets ({len(pups)})')
        ck(bool(mirrors), f'Carol\'s game mirrors the host\'s Defender ({len(mirrors)} mirrors)')
        own_pol = next((s['pol'] for s in f if s['me']), '')
        slot, folder, planet = save_slot(carol, carol_dir)
        ck(slot != '' and planet != '', f'Carol plays a save of her own ({slot}/{planet})')
        t_save = time.time()
        print(' ', carol.call('coop.save')['out'].strip()[:80], flush=True)
        close_boxes(carol, 12)
        ck(wait_saved(folder, t_save), f'Carol wrote her save ({folder})')
        print(' ', carol.call('coop.savestrip')['out'].strip()[:160], flush=True)
        st = [ln for ln in carol.call('coop.fleet list')['out'].splitlines() if 'save_stripped' in ln]
        print('  ', st[0][-80:] if st else '-', flush=True)
        time.sleep(2)
        after = fleet(carol)
        ptrs = {s['ptr'] for s in after}
        back = all(s['ptr'] in ptrs for s in mine)
        ck(back, f'the puppets and mirrors are still in Carol\'s game after the save ({show(ours(after))})')
        print(' ', carol.call('coop.leave')['out'].strip()[:80], flush=True)
        time.sleep(5)
        re_ = restart_without_session(b, carol_inst)
        print(' ', re_.call(f'coop.load {slot} {planet}')['out'].strip()[:120], flush=True)
        loaded, t0 = False, time.time()
        while time.time() - t0 < 240:
            try:
                if 'Space' in re_.call('game.mode')['out'] and any(s['me'] for s in fleet(re_)):
                    loaded = True
                    break
            except (OSError, ValueError):
                break
            time.sleep(2)
        ck(loaded, 'Carol loads the save she wrote, alone (no session)')
        time.sleep(10)
        out2 = re_.call('exc')['out']
        lf = fleet(re_)
        print(f'  after the reload: {(out2.strip().splitlines() or ["-"])[0][:100]} | ships {show(lf)}', flush=True)
        ck('total faults: 0' in out2, f'the reloaded world runs without a fault ({(out2.strip().splitlines() or ["-"])[0][:90]})')
        left_pups = [s for s in lf if not s['me'] and s['type'] == 6 and s['inv'] and s['pol'] == own_pol]
        ck(loaded and not left_pups, f'no invincible Defender of Carol\'s empire (a puppet) in the reloaded world ({show(left_pups)})')
        near = [s for s in lf if not s['me'] and any(
            s['type'] == o['type'] and s['model'] == o['model'] and math.dist(s['pos'], o['pos']) < 5 for o in mine)]
        ck(loaded and not near, f'no ship of ours where a puppet or mirror stood ({show(near)})')
    finally:
        subprocess.run([lane.SPORE, 'kill'], env=dict(os.environ, SPORE_LANE=str(b)), capture_output=True)
        time.sleep(2)
    ck('Space' in bob.call('game.mode')['out'], 'Bob still plays in the world')
    ck.faults(host=host, guest=bob)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
