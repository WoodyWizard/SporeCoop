#!/usr/bin/env python3
"""Tribe: a guest playing another Tribe world is not stood by the host's game-over caption (BUG-1495, audit TR-9
part 2).

The host's game-over wait (MSG_TRIBE_ANIM kind 20, BUG-0960) and its theme and party music (kinds 4-8) reached a guest
that plays a world of its own in Tribe (the message is class Module: the module gates it per place, and the tribe
module did not): the guest's own game stood with "The tribe is out: waiting for <host>".

1. The tribe fixture; the guest leaves and loads a copy of the fixture under another name (Otherworld, as
   tribe_other_world), joins the host again and stays there (`coop.status` elsewhere=1).
2. The host's game knocks out every member of its player tribe (`coop.tribe pincer`, an NPC member the attacker)
   until its game-over caption comes.
3. For 10 s after the caption the guest's game time runs (`busy:` of coop.status has no `paused`: the game's own
   cGameTimeManager) and the guest is still in Otherworld. A key frame of both.
4. The host answers the caption (Enter).
SPORE_FIX_OFF=tribe.elsewhere_anims must FAIL (the guest's time stands within 6 s of the caption).

  SPORE_LANE=5 python3 tools/tests/coop_tribe_gameover_elsewhere.py
"""
import glob
import os
import shutil
import time

from harness import busy_list as busy
from harness import world_of
import lane
from play_tribe import caption_up, tribe_state
from twoinst import Checks, game_runs, keyframe, start


def to_otherworld(guest):
    """the guest leaves, loads a copy of the fixture planet named Otherworld and joins again (coop_tribe_other_world)"""
    print(' ', guest.call('coop.leave')['out'].strip(), flush=True)
    guest.call('coop.worldfix autoload 0')
    games = os.path.join(lane.GUEST_DIR, 'Games')
    src = next(os.path.dirname(p) for p in glob.glob(os.path.join(games, '*', 'Zorispe.spo')))
    dst = os.path.join(games, 'Game9')
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    if os.path.exists(os.path.join(dst, 'sporecoop.txt')):
        os.remove(os.path.join(dst, 'sporecoop.txt'))
    os.rename(os.path.join(dst, 'Zorispe.spo'), os.path.join(dst, 'Otherworld.spo'))
    print(' ', guest.call('coop.load Game9 Otherworld')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 150:
        if world_of(guest) == 'Otherworld.spo' and 'Tribe' in guest.call('game.mode')['out']:
            break
        time.sleep(2)
    guest.call('wait.frames 300 60000')
    time.sleep(6)
    print(' ', guest.call(lane.JOIN)['out'].strip(), flush=True)
    t0, st = time.time(), ''
    while time.time() - t0 < 30:
        st = guest.call('coop.status')['out']
        if 'elsewhere=1' in st:
            break
        time.sleep(1)
    return world_of(guest) == 'Otherworld.spo' and 'elsewhere=1' in st


def main():
    host, guest = start('tribe')
    ck = Checks()
    try:
        if not ck(to_otherworld(guest), f'the guest plays a world of its own and knows it ({world_of(guest)})'):
            return ck.result()
        game_runs(guest, 60)
        time.sleep(4)
        hs = tribe_state(host)
        members = [i for i, v in hs.items() if v['tribe'] == 'p']
        enemy = [i for i, v in hs.items() if v['tribe'] != 'p' and v['hp'] > 0]
        print(f'  host: player tribe {members}, an enemy {enemy[:1]}; guest busy {busy(guest)}', flush=True)
        if not ck(members and enemy, 'the host\'s player tribe and another tribe\'s member'):
            return ck.result()
        if not ck('paused' not in busy(guest), f'the guest\'s game runs before ({busy(guest)})'):
            return ck.result()
        t0, k = time.time(), 0
        while time.time() - t0 < 120 and not caption_up(host):
            hs = tribe_state(host)
            for i in [i for i in members if i in hs and hs[i]['hp'] > 0]:
                out = host.call(f'coop.tribe pincer {i} {enemy[0]} 5000')['out'].strip()
                if k % 8 == 0:
                    print(f'    {time.time() - t0:3.0f} s: #{i}: {out[:80]}', flush=True)
            k += 1
            time.sleep(1.5)
        t_cap = time.time()
        if not caption_up(host):
            ck.faults(host=host, guest=guest)
            return ck.inconclusive('no game-over caption on the host in 120 s')
        print(f'  the host\'s caption after {t_cap - t0:.0f} s', flush=True)
        stood, gb = None, []
        while time.time() - t_cap < 10:
            gb = busy(guest)
            if 'paused' in gb and stood is None:
                stood = time.time() - t_cap
            time.sleep(0.5)
        wait = next((ln.strip() for ln in guest.call('coop.status')['out'].splitlines() if 'game over wait' in ln), '')
        print(f'  guest busy {gb}; {wait[:160]}', flush=True)
        keyframe('caption_elsewhere', 'the host in its game-over caption; the guest plays on in its own world '
                 '(no "waiting for" note)', settle=0)
        ck(stood is None, f'the guest\'s game in its own world did not stand for the host\'s caption '
           f'({stood and round(stood, 1)} s after it)')
        ck(world_of(guest) == 'Otherworld.spo', f'the guest is still in its own world ({world_of(guest)})')
        host.call('key enter')
        time.sleep(4)
        ck.faults(host=host, guest=guest)
    finally:
        guest.call('coop.worldfix autoload 1')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
