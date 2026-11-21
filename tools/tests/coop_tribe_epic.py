#!/usr/bin/env python3
"""Tribe: the co-op epic creature and the chiefs around a save and a leave (BUG-0146, BUG-0121, BUG-0067).

1. BUG-0146: the host spawns the epic; a member of an NPC tribe (owner 254) hits it, then only the
   host's members kill it. The reward is the one-player 30 food (the game's food, `coop.res`), not
   the "together" 80: an NPC tribe's member is no second player.
2. BUG-0121: two more epics are spawned and killed. The game's own object list holds no more epic
   herds than after the first one (one herd is reused, with target size 0) and no
   live epic but the planet's own ones (the fixture has one). A third epic is alive when the host saves through the game (`coop.save`): after the
   save the co-op epic is not in the world, so it did not go into the save.
3. BUG-0067: both players have a chief (1.3x its own size in the game). The guest leaves: on the
   guest no member of the player tribe is bigger than its own size any more (spatial GetScale /
   the citizen's desired scale). The host ends its session too: the same on the host, and the
   epic alive at that moment is gone.

--nofix: `coop.tribe fix epic_players 0`, `fix epic_herd 0`, `fix chiefs 0` (the old code): must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_tribe_epic.py [--nofix]
"""
import argparse
import re
import time

from coop_tribe import members
from twoinst import Checks, save_taken, start


def food(con):
    m = re.search(r'food[= ]+(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def herds(con):
    out = con.call('coop.tribe epic herds')['out']
    v = {k: int(x) for k, x in re.findall(r'(\w+)=(\d+)', out)}
    return v, out.strip()


def scales(con):
    out = con.call('coop.tribe scales')['out']
    m = re.search(r'members=(\d+) big=(\d+) max_ratio=([\d.]+)', out)
    return (int(m.group(1)), int(m.group(2)), float(m.group(3))) if m else (0, -1, 0.0), out.strip()


def settled(con, secs=20):
    """the game grows and shrinks a citizen over a few seconds: wait until no member is big"""
    t0 = time.time()
    while True:
        r = scales(con)
        if r[0][1] == 0 or time.time() - t0 > secs:
            return r
        time.sleep(2)


def kill_epic(host, attacker, npc_first=None):
    """spawn an epic and kill it with `attacker`; `npc_first` hits it once before"""
    out = host.call('coop.tribe epic')['out']
    print('  ', out.strip())
    if 'spawned' not in out or 'not spawned' in out:
        return False
    time.sleep(1)
    if npc_first is not None:
        print('   npc member hit:', host.call(f'coop.tribe epic hit {npc_first} 5')['out'].strip())
    for _ in range(60):
        r = host.call(f'coop.tribe epic hit {attacker} 40')['out']
        if 'no epic' in r or 'dead' in r or 'hp 0.00' in r.split('->')[-1]:
            break
        time.sleep(0.2)
    time.sleep(3)  # epic_tick sees it dead
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            for f in ('epic_players', 'epic_herd', 'chiefs'):
                print(' ', c.call(f'coop.tribe fix {f} 0')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 90:  # the split of the player tribe arrives with the first states
        hm = members(host)
        if any(v[0] == 1 for v in hm.values()):
            break
        time.sleep(3)
    time.sleep(3)
    hm = members(host)
    host_m = [i for i, v in hm.items() if v[0] == 0 and v[3] == 'player']
    npc_m = [i for i, v in hm.items() if v[0] == 254 and v[3] != 'player']
    print('  host members', host_m, 'npc tribe members', npc_m[:5])
    if not ck(host_m and npc_m, 'the host has members and an NPC tribe is there'):
        return ck.result()

    # 1. BUG-0146
    f0 = food(host)
    h0, out0 = herds(host)
    print('  before:', out0)
    ck(kill_epic(host, host_m[-1], npc_first=npc_m[0]), 'an epic was spawned')
    f1 = food(host)
    st = host.call('coop.status')['out']
    got = re.findall(r'epic creature down: \+(\d+) food', st)
    print(f'  food {f0} -> {f1}; notices {got}')
    ck(got and got[-1] == '30', 'one player and an NPC tribe member: the one-player reward (30)')
    ck(f0 is not None and f1 is not None and 20 <= f1 - f0 < 60, f'the game\'s food grew by about 30 ({f0} -> {f1})')

    # 2. BUG-0121
    h1, out1 = herds(host)
    print('  after one epic:', out1)
    for _ in range(2):
        kill_epic(host, host_m[-1])
    h3, out3 = herds(host)
    print('  after three epics:', out3)
    ck(h3.get('herds', 99) <= h1.get('herds', 0), 'no new epic herd per epic (the game\'s object list)')
    base = h0.get('live_epics', 0)  # the game's own epics of the planet (the fixture has one)
    ck(h3.get('live_epics', 99) == base and h3.get('quiet_ours', 0) == h3.get('herds', -1),
       'no live co-op epic left after the kills, every epic herd is ours with target size 0')
    print('  ', host.call('coop.tribe epic')['out'].strip())
    time.sleep(2)
    hs, outs = herds(host)
    print('  epic alive:', outs)
    print('  ', save_taken(host, 30)[0])  # BUG-1398: asked again while the mod refuses it for now
    time.sleep(4)
    hs2, outs2 = herds(host)
    print('  after the save:', outs2)
    ck(hs.get('live_epics', 0) == base + 1 and hs2.get('live_epics', 99) == base,
       'a live epic is out of the world when the host saves (not in the save)')

    # 3. BUG-0067
    print('  ', host.call('coop.tribe epic')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 30:
        (n, big, mx), sout = scales(host)
        if big >= 1:
            break
        time.sleep(2)
    print('  before the leave:', sout, '|', scales(guest)[1])
    ck(big >= 1 and mx > 1.2, 'a chief is 1.3x its size in the game')
    # the guest's session ends: its chiefs (mirrors of the host's members) are plain members again
    print('  ', guest.call('coop.leave')['out'].strip())
    (gn, gbig, gmx), gout = settled(guest)
    print('  guest after its leave:', gout)
    ck(gn > 0 and gbig == 0 and gmx < 1.2, 'guest: no member keeps the chief size after the session')
    # the host plays on alone in its session (its chief stays); its session ends too
    print('  ', host.call('coop.leave')['out'].strip())
    (n2, big2, mx2), sout2 = settled(host)
    he, heout = herds(host)
    print('  host after its leave:', sout2, '|', heout)
    ck(n2 > 0 and big2 == 0 and mx2 < 1.2, 'host: no member keeps the chief size after the session')
    ck(he.get('live_epics', 99) == base, 'the co-op epic ended with the session')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
