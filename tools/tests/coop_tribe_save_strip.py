#!/usr/bin/env python3
"""Tribe: a guest's save holds none of the session's mirrors (BUG-0495).

A Tribe guest does not simulate the host's tribes: it shows mirrors of the host's members (citizens
its game spawns into its tribes, `spawn_member`) and makes the host's NPC tribes its own game did not
form (`SpawnNpcTribe`, BUG-0381). Those went into the guest's saves like its own members and tribes and
came back after a load as members and tribes of that world, with nothing to drive them.

1. The host plays the tribe world (Bob joins on the main lane); Carol joins on a second lane and loads
   the host's world.
2. The host's tribe gets 4 more adults (`coop.tribe spawn 4`) and the host's game forms an NPC tribe of
   3 members 70 m from the village (`coop.tribe npc_tribe 3 70`): Carol mirrors them (her game spawns
   the mirrors and makes the tribe: `ours=1` / `made=1` in `coop.tribe`).
3. Carol saves with the game's own save (`coop.save`), leaves the session; her game is restarted
   WITHOUT a co-op session and loads that save.
4. The loaded world: the player tribe has the members of Carol's own game only (those she had when
   saved minus the mirrors the mod spawned), no tribe where the made tribe stood, no fault for 10 s.
PASS: 2 to 4. SPORE_FIX_OFF=tribe.save_strip: the loaded world has the mirrors and the made tribe (FAIL).

  SPORE_LANE=4 SPORE_FREE_LANE=5 python3 tools/tests/coop_tribe_save_strip.py
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


def tribes(con):
    """[(player, made, (x, y, z), [ours flags of the members])] from the game's tribe list"""
    out = []
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe \d+( \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\): \d+ members .*?made=(\d)', line)
        if m:
            out.append((m.group(1) is not None, m.group(5) == '1', tuple(float(m.group(i)) for i in (2, 3, 4)), []))
            continue
        m = re.match(r'\s+#\d+ .* ours=(\d)', line)
        if m and out:
            out[-1][3].append(m.group(1) == '1')
    return out


def show(ts):
    return [('P' if t[0] else 'M' if t[1] else 'n', tuple(round(v) for v in t[2]), len(t[3]), sum(t[3])) for t in ts]


def main():
    if os.environ.get('SPORE_FREE_LANE') == '' and not os.environ.get('SPORE_LANE_B'):
        print('RESULT: SKIP (no second lane free for the guest that reloads)')
        return 0
    b = lane_b()
    os.environ['SPORE_LANE_B'] = str(b)
    assert b > 1 and b != lane.LANE, 'Carol needs a lane of its own'
    host, bob = start('tribe')
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
                if 'Tribe' in carol.call('game.mode')['out'] and any(t[0] and t[3] for t in tribes(carol)):
                    in_world = True
                    break
            except (OSError, ValueError):
                break
        ck(in_world, "Carol plays the host's tribe")
        time.sleep(5)
        print('  host:', host.call('coop.tribe spawn 4')['out'].strip(), flush=True)
        out = host.call('coop.tribe npc_tribe 3 70')['out'].strip()
        print('  host:', out, flush=True)
        m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
        at = tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None
        ts, t0 = [], time.time()
        while time.time() - t0 < 30:
            ts = tribes(carol)
            if sum(sum(t[3]) for t in ts if t[0]) >= 4 and any(t[1] and t[3] for t in ts):
                break
            time.sleep(1)
        print(f'  Carol tribes: {show(ts)} (P player, M made by the mod; members, mirrors spawned)', flush=True)
        pt = [t for t in ts if t[0]]
        spawned = sum(pt[0][3]) if pt else 0
        own = len(pt[0][3]) - spawned if pt else 0
        made = [t for t in ts if t[1]]
        ck(spawned >= 4, f'Carol\'s game spawned mirrors of the host\'s new members ({spawned})')
        ck(bool(made), 'Carol\'s game made the host\'s new NPC tribe')
        slot, folder, planet = save_slot(carol, carol_dir)
        ck(slot != '' and planet != '', f'Carol plays a save of her own ({slot}/{planet})')
        t_save = time.time()
        print(' ', carol.call('coop.save')['out'].strip()[:80], flush=True)
        close_boxes(carol, 12)
        ck(wait_saved(folder, t_save), f'Carol wrote her save ({folder})')
        st = [ln for ln in carol.call('coop.tribe')['out'].splitlines() if 'save_stripped' in ln]
        print('  ', st[0][-60:] if st else '-', flush=True)
        print(' ', carol.call('coop.savestrip')['out'].strip()[:140], flush=True)
        after = tribes(carol)
        print(f'  Carol tribes after the save: {show(after)}', flush=True)
        back = all(any(math.dist(t[2], a[2]) < 5 and len(a[3]) >= len(t[3]) for a in after) for t in ts)
        ck(back, 'the mirrors and tribes are back in Carol\'s game after the save (the host\'s game may form more)')
        print(' ', carol.call('coop.leave')['out'].strip()[:80], flush=True)
        time.sleep(5)
        re_ = restart_without_session(b, carol_inst)
        print(' ', re_.call(f'coop.load {slot} {planet}')['out'].strip()[:120], flush=True)
        loaded, t0 = False, time.time()
        while time.time() - t0 < 240:
            try:
                if 'Tribe' in re_.call('game.mode')['out'] and any(t[0] for t in tribes(re_)):
                    loaded = True
                    break
            except (OSError, ValueError):
                break
            time.sleep(2)
        ck(loaded, 'Carol loads the save she wrote, alone (no session)')
        time.sleep(10)
        out2 = re_.call('exc')['out']
        lt = tribes(re_)
        print(f'  after the reload: {(out2.strip().splitlines() or ["-"])[0][:100]} | tribes {show(lt)}', flush=True)
        ck('total faults: 0' in out2, f'the reloaded world runs without a fault ({(out2.strip().splitlines() or ["-"])[0][:90]})')
        lp = [t for t in lt if t[0]]
        n = len(lp[0][3]) if lp else -1
        ck(loaded and n == own, f'the reloaded player tribe has Carol\'s own members only ({n}; own {own}, '
           f'mirrors spawned {spawned})')
        near = [t for t in lt if not t[0] and made and math.dist(t[2], made[0][2]) < 20]
        ck(loaded and made and not near, f'no tribe where the made tribe stood ({show(near)})')
    finally:
        subprocess.run([lane.SPORE, 'kill'], env=dict(os.environ, SPORE_LANE=str(b)), capture_output=True)
        time.sleep(2)
    ck('Tribe' in bob.call('game.mode')['out'], 'Bob still plays in the world')
    ck.faults(host=host, guest=bob)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
