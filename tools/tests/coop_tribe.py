#!/usr/bin/env python3
"""Tribe stage co-op ("Chiefs") on two instances: the host simulates every tribe, the guest
mirrors them; the player tribe is split between the players; a player selects and orders only
their own members; guest orders go through the host."""
import math
import re
import time

from twoinst import Checks, shot, start


def members(con):
    """{id: (owner, sel, (x, y, z), tribe)}"""
    out, tribe = {}, None
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)', line)
        if m:
            tribe = int(m.group(1)) if 'player' not in line else 'player'
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) age=\d+ sel=(\d+) hp=[\d.]+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and m.group(1) != '0':
            out[int(m.group(1))] = (int(m.group(2)), int(m.group(3)),
                                    tuple(float(m.group(i)) for i in (4, 5, 6)), tribe)
    return out


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def seg_dist(p, a, b):
    """distance from p to the segment a-b"""
    ab = [y - x for x, y in zip(a, b)]
    n = sum(x * x for x in ab)
    k = max(0.0, min(1.0, sum((q - x) * y for q, x, y in zip(p, a, ab)) / n)) if n > 1e-9 else 0.0
    return dist(p, [x + y * k for x, y in zip(a, ab)])


ROUNDS = 8  # BUG-0407: up to 8 rounds of 1 s, the first 4 always (the best counts)


def main():
    host, guest = start('tribe')
    ck = Checks()
    t0 = time.time()
    while True:  # the guest may still be finishing the load of the host world
        hm, gm = members(host), members(guest)
        if (hm and set(hm) == set(gm)) or time.time() - t0 > 60:
            break
        time.sleep(2)
    if not gm:
        print('  guest state:', guest.call('game.mode')['out'], guest.call('coop.avatar')['out'][:200])
        print(guest.call('coop.tribe')['out'][:800])
    print('  host :', {k: (v[0], v[3]) for k, v in hm.items()})
    print('  guest:', {k: (v[0], v[3]) for k, v in gm.items()})
    ck(len(hm) > 0 and set(hm) == set(gm), f'guest mirrors every citizen ({len(hm)} host, {len(gm)} guest)')
    # walking citizens move between the samples: take the best of a few rounds. BUG-0407: a member is judged by its
    # distance from the host's path over the round (host samples before and after the guest's, the segments between
    # them), not from the nearer end: the guest draws a walking member ~0.1-0.2 s behind the host (interpolation),
    # and on a loaded machine one console call took 0.1-0.4 s, so a member at 9 m/s was 3.7-9.6 m from both ends
    # while on the host's path (lab 20261101-135924). A member that stands is judged as before.
    errs, worst = [], []
    for _ in range(ROUNDS):
        h0, h1, g1, h2 = members(host), members(host), members(guest), members(host)
        es = {}
        for i in h1:
            if i in g1 and i in h0 and i in h2:
                es[i] = min(seg_dist(g1[i][2], h0[i][2], h1[i][2]), seg_dist(g1[i][2], h1[i][2], h2[i][2]))
        e = max(es.values(), default=99)
        errs.append(e)
        if es:
            i = max(es, key=es.get)
            worst.append(f'#{i} {es[i]:.1f} m (moved {dist(h0[i][2], h2[i][2]):.1f} m on the host)')
        if len(errs) >= 4 and min(errs) < 3:
            break
        time.sleep(1)
    err = min(errs)
    print(f'  worst member per round: {worst}', flush=True)
    ck(err < 3, f'mirrors are where the host has them (max error {err:.2f} m; rounds {[round(e, 1) for e in errs]})')
    mine = [i for i, v in gm.items() if v[0] == 1 and v[3] == 'player']
    theirs = [i for i, v in gm.items() if v[0] == 0 and v[3] == 'player']
    ck(mine and theirs, f'player tribe split: guest owns {mine}, host owns {theirs}')
    if not (mine and theirs):
        return ck.result()

    # selection: only own members
    r = guest.call(f'coop.tribe select {theirs[0]} 1')['out']
    ck('selected=0' in r, f'guest cannot select a host member ({r})')
    r = host.call(f'coop.tribe select {mine[0]} 1')['out']
    ck('selected=0' in r, f'host cannot select a guest member ({r})')
    r = guest.call(f'coop.tribe select {mine[0]} 1')['out']
    ck('selected=1' in r, f'guest selects its own member ({r})')

    # walk order: guest -> host -> the member walks on both machines
    start_pos = hm[mine[0]][2]
    others = [v[2] for i, v in hm.items() if v[3] == 'player' and i != mine[0]]
    dest = others[0] if others else start_pos
    dest = tuple(a + (b - a) * 0.6 for a, b in zip(start_pos, dest))
    print('  walk', start_pos, '->', dest)
    print(' ', guest.call('coop.tribe walk %.2f %.2f %.2f x' % dest)['out'])
    time.sleep(8)
    hm2, gm2 = members(host), members(guest)
    moved = dist(hm2[mine[0]][2], start_pos)
    ck(moved > 1.5, f'the host walked the guest member {moved:.1f} m')
    ck(dist(hm2[mine[0]][2], gm2[mine[0]][2]) < 3, 'the guest sees the walk')
    st = host.call('coop.status')['out']
    ck(re.search(r'cmds_done=[1-9]', st), 'host executed the order: ' + re.search(r'tribe\(host\).*', st).group(0))

    # action on an object: attack a member of the other tribe (CitizenAction 7)
    npcs = [i for i, v in hm2.items() if v[3] != 'player']
    if npcs:
        print(' ', guest.call(f'coop.tribe act 7 {npcs[0]}')['out'])
        time.sleep(2)
        st = host.call('coop.status')['out']
        m = re.search(r'cmds_done=(\d+)', st)
        ck(m and int(m.group(1)) >= 2, 'host executed the action order: ' + re.search(r'tribe\(host\).*', st).group(0))
    # animals are shared too: hunt (CitizenAction 10) a mirrored animal
    gl = guest.call('coop.npc list')['out']
    animals = re.findall(r'#(\d+) .*spawned hp=([\d.]+)', gl)
    ck(len(animals) > 0, f'guest mirrors the host animals ({len(animals)})')
    if animals:
        m0 = re.search(r'cmds_done=(\d+)', host.call('coop.status')['out'])
        for i, v in members(guest).items():  # every own member hunts the weakest animal
            if v[0] == 1 and v[3] == 'player':
                guest.call(f'coop.tribe select {i} 1')
        animals.sort(key=lambda a: float(a[1]))
        print(' ', guest.call(f'coop.tribe act_npc 10 {animals[0][0]}')['out'])
        time.sleep(2)
        st = host.call('coop.status')['out']
        m1 = re.search(r'cmds_done=(\d+)', st)
        ck(m0 and m1 and int(m1.group(1)) > int(m0.group(1)),
           'host found the animal and executed the hunt: ' + re.search(r'tribe\(host\).*', st).group(0))
        # the hunted animal dies: its body lies at the same place on both machines (the guest's
        # body used to shoot off into the sky, ~80 m up while driven)
        from coop_sync import sample
        key, dead = 'n' + animals[0][0], False
        for _ in range(45):
            dead = any(re.match(rf'\s*#{animals[0][0]} .*DEAD', ln) for ln in host.call('coop.npc list')['out'].splitlines())
            if dead:
                break
            time.sleep(1)
        if dead:
            time.sleep(3)
            hs, gs = sample(host), sample(guest)
            if key in hs and key in gs:
                d = dist(hs[key][0], gs[key][0])
                ck(d < 3, f'the hunted animal\'s body lies where the host has it ({d:.1f} m)')
        else:
            print('  (the hunted animal did not die within 45 s)')
    # gathering from a fruit tree by a real right-click: trees are cTreeData (no game object), the
    # host finds its own tree by position (before 2026-10-23 every such order was rejected)
    gm = members(guest)
    mine = [i for i, v in gm.items() if v[0] == 1 and v[3] == 'player']
    if mine:
        # a real box selection: object orders take the game's selection set (the console select
        # only sets the flag, enough for walks but not for orders on objects)
        import random
        from play_tribe import Game, Player, tribe_centres, tribe_state
        for i in members(guest):
            guest.call(f'coop.tribe select {i} 0')
        pl = Player('guest', guest, 1, random.Random(1), [], 1440, 1080)
        pl.calibrate()  # the minimap takes the view to the member, then to the village
        g = Game.__new__(Game)
        g.w, g.h = 1440, 1080
        sel = g.select_idle(pl, tribe_state(guest), mine, False)
        ck(sel, f'the guest box-selects its member ({sel})')
        pl.look(tribe_centres(host)['p'])
        rej0 = int(re.search(r'rejected=(\d+)', host.call('coop.status')['out']).group(1))
        sent = ''
        guest.call('move 720 540')  # distances of coop.targets objs: from the point under the mouse
        time.sleep(0.4)
        objs = [ln.split() for ln in guest.call('coop.targets objs')['out'].splitlines() if ln.startswith(('obj', 'member'))]
        # in the open: not behind animals, citizens, the hut or the fire (they would take the click)
        front = [(int(f[3]), int(f[4])) for f in objs if f[1][0] in 'tnh' and f[1] != 'tree' or f[0] == 'member'
                 or f[1] in ('o18C8F0C', 'o18C6D19', 'o629BAFE')]
        trees = sorted((float(f[2]), int(f[3]), int(f[4])) for f in objs if f[1] == 'fruittree' and float(f[2]) < 90
                       and 150 < int(f[4]) < 900 and all(abs(int(f[3]) - x) + abs(int(f[4]) - y) > 80 for x, y in front))[:8]
        trees += sorted((float(f[2]), int(f[3]), int(f[4])) for f in objs if f[1] == 'fruittree' and float(f[2]) < 150
                        and 150 < int(f[4]) < 900)[:8]
        print(f'  {len(trees)} fruit trees to try')
        for _, x, y in trees:
            for dy in (0, -10, -20):
                guest.call(f'move {x} {y + dy}')  # hover: the game picks under the cursor per frame
                time.sleep(0.4)
                guest.call(f'click {x} {y + dy} right')
                time.sleep(0.8)
                sent = re.search(r'tribe last order: (.*)', guest.call('coop.status')['out'])
                sent = sent.group(1) if sent else ''
                if 'target=3/' in sent:
                    break
            if 'target=3/' in sent:
                break
        time.sleep(1)
        st = host.call('coop.status')['out']
        got = re.search(r'tribe last order: (.*)', st)
        print('  tree order: guest', sent, '| host', got and got.group(1))
        ck('target=3/' in sent, 'a right-click on a fruit tree gives a gather order on the tree')
        ck(int(re.search(r'rejected=(\d+)', st).group(1)) == rej0 and got and 'rejected' not in got.group(1),
           'the host finds the tree and executes the gather order')
    # a gift: the guest gives one of its selected members to the host
    gm = members(guest)
    mine = [i for i, v in gm.items() if v[0] == 1 and v[3] == 'player']
    if mine:
        for i in gm:
            guest.call(f'coop.tribe select {i} 0')
        guest.call(f'coop.tribe select {mine[0]} 1')
        print(' ', guest.call('coop.give')['out'])
        time.sleep(2)
        hm = members(host)
        ck(hm.get(mine[0], (None,))[0] == 0, f'the host owns the gifted member #{mine[0]} now')
        time.sleep(1)
        ck(members(guest).get(mine[0], (None,))[0] == 0, 'the guest sees the new owner')
        # the giver must not keep the gifted member selected (it could order the partner's member)
        ck(members(guest).get(mine[0], (0, 1))[1] == 0, 'the gifted member is no longer selected by the giver')
        ck('gave 1 tribe member' in host.call('coop.status')['out'], 'everybody is told about the gift')
    # a crowded planet: snapshots of more than 24 citizens come in several chunks; the first chunk
    # did not fit a packet and was dropped, so the guest lost the player tribe (until 2026-10-23)
    print(' ', host.call('coop.tribe spawn 20')['out'])
    time.sleep(5)
    hm, gm = members(host), members(guest)
    print(f'  {len(hm)} citizens on the host, {len(gm)} on the guest')
    ck(len(hm) > 24 and set(hm) == set(gm), f'the guest mirrors every citizen of a crowded planet ({len(hm)} / {len(gm)})')
    hp = {i for i, v in hm.items() if v[3] == 'player'}
    ck(hp and hp == {i for i, v in gm.items() if v[3] == 'player'}, 'the guest has the whole player tribe')
    shot(host, 'tribe_host.png')
    shot(guest, 'tribe_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
