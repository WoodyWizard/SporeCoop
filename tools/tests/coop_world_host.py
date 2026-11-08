#!/usr/bin/env python3
"""World objects that exist differently in the two games: the host's game is the truth (0.7.9 L2, mods/coop/worldobj.cpp,
docs/re/creature.md#creature-host-places). Every case asserts the game's own object list on both machines (exists,
position), not the mod's bookkeeping:

  --case fruit      (BUG-0497, creature) a host fruit whose copy the guest's game lost (gone there only, as its flora's
                    recycling does), and a fruit the guest's game rolled elsewhere (moved 8.5 m there only): both lie
                    at the host's places in the guest's game (within 1.5 m), the rolled one not where it rolled to.
                    SPORE_FIX_OFF=wobj.fruit_place fails.

  --case ornament   (BUG-0541, space) both players land on the home planet: the ornaments (noun 0x18C88E4: the
                    cities' decorations, 9 per city) are the same on both planets (every one of each game has one of
                    the other's within 1.5 m), in 3 landings (a takeoff between: the cities are set up anew; their
                    layouts differed in 2 of 3 loads). SPORE_FIX_OFF=wobj.city_ornament fails.

  --case bundle_drop (BUG-0661, tribe) a host member carrying food dies: the food lies on the ground at the host's place
                    in the guest's game too, no copy is still carried (SPORE_FIX_OFF=wobj.bundle_reclass fails)
  --case bundle_civ (BUG-0548, civ) food on the ground by the host's city (as a fight's loot): the host's lies at
                    its place in the guest's game, one only the guest's game dropped is gone from it.
                    SPORE_FIX_OFF=wobj.bundle fails.

  --case cultural   (BUG-0540, civ) every city's cultural targets near the guest stand at the host's places in the
                    guest's game (within 1 m, as many as on the host). SPORE_FIX_OFF=wobj.cultural_target fails.

  coop_world_host.py --case fruit
"""
import argparse
import math
import time

from coop_world_objects import near, objs, pairs, wait
from twoinst import Checks, start


def fruit_at(con, pos, d=1.5):
    return [f for f in near(objs(con, 'fruit', 4, pos), pos, d) if f['st'] in (1, 2)]


def fruit(host, guest, ck):
    # 1. a host fruit whose copy the guest's game lost (its flora recycled it for another drop, Flora_TakeFruitForDrop):
    #    gone from the guest's game only, without a request to the host
    ps = [(h, g) for h, g in pairs(host, guest, 'fruit', 1.0, 60) if h['st'] == 2 and g['st'] == 2]
    print('  fruit on the ground on both games (host id, guest id, pos):', [(h['id'], g['id'], h['pos']) for h, g in ps][:4], flush=True)
    if not ck(len(ps) >= 2, 'two fruit on the ground at the same places on both games'):
        return
    h, g = ps[0]
    print('  guest:', guest.call(f'coop.wobj eat {g["p"]} local')['out'].strip(), f'(the guest\'s copy at {g["pos"]} gone, the host\'s stays)',
          flush=True)
    time.sleep(10)
    hf = next((f for f in objs(host, 'fruit', 4, h['pos']) if f['id'] == h['id']), None)
    if not ck(hf, 'the host\'s fruit is still there (the host\'s game)'):
        return
    # fruit lie in heaps: the same number within 1.5 m of it on both games
    n_host = len(fruit_at(host, hf['pos']))
    got = wait(lambda: len(fruit_at(guest, hf['pos'])) >= n_host, 10)
    print(f'  host fruit at {hf["pos"]}: {n_host} within 1.5 m on the host, {len(fruit_at(guest, hf["pos"]))} on the guest', flush=True)
    ck(got, 'the host\'s fruit whose copy the guest\'s game lost lies at its place in the guest\'s game again '
            '(as many fruit there as on the host)')
    # 2. a fruit the guest's game rolled elsewhere
    ps = [(h, g) for h, g in pairs(host, guest, 'fruit', 1.0, 60) if h['st'] == 2 and g['st'] == 2
          and math.dist(h['pos'], hf['pos']) > 3 and h['id'] != hf['id']]
    print('  fruit on the ground on both games:', [(h['id'], g['id'], h['pos']) for h, g in ps][:4], flush=True)
    if not ck(ps, 'a fruit on the ground at the same place on both games'):
        return
    h2, g2 = ps[0]
    away = (g2['pos'][0] + 6, g2['pos'][1], g2['pos'][2] + 6)
    print('  guest:', guest.call('coop.wobj move %s %.2f %.2f %.2f' % ((g2['p'],) + away))['out'].strip(),
          '(the guest\'s fruit rolled 8.5 m away)', flush=True)
    time.sleep(8)
    hf2 = next((f for f in objs(host, 'fruit', 6, h2['pos']) if f['id'] == h2['id']), None)
    if not ck(hf2, 'the host\'s fruit is still there (the host\'s game)'):
        return
    got = fruit_at(guest, hf2['pos'])
    extra = [f for f in near(objs(guest, 'fruit', 6, away), away, 1.5)]
    print(f'  guest fruit at the host\'s place: {got}; at the rolled place: {extra}', flush=True)
    ck(got, 'the fruit lies at the host\'s place in the guest\'s game')
    ck(not extra, 'no fruit of the guest\'s own at the place it rolled to')
    # the fruit the guest's game made came from a tree slot whose mask was put back: the trees keep their fruit
    time.sleep(2)
    tp = pairs(host, guest, 'tree', 0.5, 70)
    diff = [(h['pos'], h['st'], g['st']) for h, g in tp if h['st'] != g['st']]
    print(f'  trees within 70 m on both games: {len(tp)}, with different fruit: {diff[:4]}', flush=True)
    ck(tp and not diff, 'every tree near the players has the same fruit on both games (the made fruit took none)')


def bundles(con, r=200, at=None):
    return objs(con, 'game_bundle', r, at)


def bundle(host, guest, ck):
    import re
    me = re.search(r'me pos=\((\S+) (\S+) (\S+)\)', host.call('coop.census all 1')['out'])
    at = tuple(float(x) for x in me.groups())
    spot = (at[0] + 4, at[1], at[2] + 4)
    out = host.call('coop.wobj bundle ground %.2f %.2f %.2f 5' % spot)['out'].strip()
    print('  host:', out, '(food dropped on the ground)', flush=True)
    m = re.search(r'pos=\((\S+) (\S+) (\S+)\)', out)
    if not ck('bundle 0' not in out and m, 'the host\'s game dropped a food bundle on the ground'):
        return
    hpos = tuple(float(x) for x in m.groups())
    got = wait(lambda: near(bundles(guest, 10, hpos), hpos, 1.5), 10)
    print(f'  guest bundles at the host\'s place {hpos}: {got}', flush=True)
    ck(got, 'the host\'s ground food lies at its place in the guest\'s game')
    # carried: a member of the host's tribe near the village with an empty hand
    rows = re.findall(r'([0-9A-F]{8}) alive=1 pos=\((\S+) (\S+) (\S+)\) dist=(\S+)', host.call('coop.tribe objs 18EB4B7')['out'])
    carried = None
    # the farthest member within 60 m first: it carries the food longer on its way to the mat (a near one put it
    # down within a second)
    rows = sorted([r for r in rows if float(r[4]) < 60], key=lambda r: -float(r[4]))
    for p, x, y, z, d in rows[:12]:
        out = host.call(f'coop.wobj bundle carry {p} 5')['out'].strip()
        if not out.startswith('bundle 0') and 'bundle 00000000' not in out:
            carried = (p, (float(x), float(y), float(z)), out)
            break
    print('  host: a member carries food:', carried, flush=True)
    if ck(carried, 'a member of the host\'s tribe carries a food bundle (the host\'s game)'):
        # the bundle sits in the member's hand (it moves with the animation): the host's carried bundle near the member
        # (it walks ~7 m/s to the mat: the host's bundle by its pointer, the guest's copy lags it by up to ~3.5 m:
        # lab 2026-11-06, coop.wobj list on both every 0.7 s)
        bp = re.search(r'bundle ([0-9A-F]{8})', carried[2]).group(1)

        def host_carried():
            return [b for b in bundles(host, 300) if b['cls'] & 0x100 and b['p'] == bp]
        hb = host_carried()
        if ck(hb, 'the host\'s carried bundle is listed'):
            seen = []

            def guest_carried():
                h = host_carried()
                if not h:
                    return []
                gb = [b for b in bundles(guest, 40, h[0]['pos']) if b['cls'] & 0x100]
                seen.append((round(time.time() % 100, 1), h[0]['pos'], [round(math.dist(b['pos'], h[0]['pos']), 1) for b in gb]))
                return [b for b in gb if math.dist(b['pos'], h[0]['pos']) < 4.0]
            got = wait(guest_carried, 12)
            print(f'  guest carried bundles near the host\'s at {hb[0]["pos"]}: {got}', flush=True)
            if not got:
                print(f'  (polls: t, host carried, guest carried distances: {seen[:12]})', flush=True)
                h = host_carried()
                at = h[0]['pos'] if h else hb[0]['pos']
                print(f'  (host carried now: {h[:1]}; guest carried within 30 m: '
                      f'{[(b["pos"], round(math.dist(b["pos"], at), 1)) for b in bundles(guest, 30, at) if b["cls"] & 0x100]})',
                      flush=True)
            ck(got, 'the guest\'s copy of that member carries the food too (the guest\'s game: a bundle in a creature\'s '
                    'container, within 4 m of the host\'s)')
    # the ground food taken in the guest's game
    g = near(bundles(guest, 10, hpos), hpos, 1.5)
    if g:
        print('  guest:', guest.call(f'coop.wobj bundle take {g[0]["p"]}')['out'].strip(), '(a guest\'s member took the food)', flush=True)
        gone = wait(lambda: not near(bundles(host, 10, hpos), hpos, 1.0), 10)
        ck(gone, 'the food the guest\'s member took is gone from the host\'s ground')


def bundle_carry(host, guest, ck):
    """BUG-0617: food a host's member carries while it walks (its copy on the guest's screen is drawn 1-3.5 m behind):
    when the host's member no longer carries it (put down / delivered: `coop.wobj bundle take` on the host), the guest's
    copy of the member carries none either within 6 s (an unbound copy was kept for good)"""
    import re
    ids = {}
    for line in host.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s+#(\d+) ([0-9A-Fa-f]{8}) owner=', line)
        if m:
            ids[m.group(2).upper()] = int(m.group(1))
    rows = re.findall(r'([0-9A-F]{8}) alive=1 pos=\((\S+) (\S+) (\S+)\) dist=(\S+)', host.call('coop.tribe objs 18EB4B7')['out'])
    rows = [r for r in rows if float(r[4]) < 60 and r[0] in ids]
    tried, left, unbound = 0, [], 0
    for p, x, y, z, d in rows[:12]:
        if tried >= 3:
            break
        out = host.call(f'coop.wobj bundle carry {p} 5')['out'].strip()
        if out.startswith('bundle 0') or 'bundle 00000000' in out:
            continue
        tried += 1
        bp = re.search(r'bundle ([0-9A-F]{8})', out).group(1)
        mp = (float(x), float(y), float(z))
        # away from the village: 40 m on the line from the host's player through the member
        me = re.search(r'me pos=\((\S+) (\S+) (\S+)\)', host.call('coop.census all 1')['out'])
        hp = tuple(float(v) for v in me.groups()) if me else (0.0, 0.0, 0.0)
        v = [a_ - b_ for a_, b_ in zip(mp, hp)]
        n = sum(q * q for q in v) ** 0.5 or 1.0
        goal = tuple(a_ + q / n * 40 for a_, q in zip(mp, v))
        print(f'  host: member #{ids[p]} carries food ({out}); '
              + host.call('coop.tribe order %d 4 %.2f %.2f %.2f' % ((ids[p],) + goal))['out'].strip(), flush=True)
        time.sleep(4)
        hb = [b for b in bundles(host, 300) if b['cls'] & 0x100 and b['p'] == bp]
        if not hb:
            print('    (the host\'s member put it down already)', flush=True)
            continue
        gb = [b for b in bundles(guest, 30, hb[0]['pos']) if b['cls'] & 0x100]
        unbound += sum(1 for b in gb if not b['bound'])
        print(f'    walking: host bundle at {hb[0]["pos"]}; guest carried copies {[(round(math.dist(b["pos"], hb[0]["pos"]), 1), b["bound"]) for b in gb]}', flush=True)
        print('    host: ' + host.call(f'coop.wobj bundle take {bp}')['out'].strip() + ' (the host\'s member put it down)', flush=True)
        at = hb[0]['pos']
        time.sleep(6)
        hc = [b for b in bundles(host, 60, at) if b['cls'] & 0x100]
        gc = [b for b in bundles(guest, 60, at) if b['cls'] & 0x100]
        extra = [b for b in gc if not any(math.dist(b['pos'], h['pos']) < 8 for h in hc)]
        print(f'    6 s later within 60 m: host carried {len(hc)}, guest carried {len(gc)}, guest without a host one '
              f'within 8 m: {[(b["pos"], b["p"]) for b in extra]}', flush=True)
        left += extra
    print(f'  guest copies unbound while walking: {unbound}', flush=True)
    ck(tried > 0, f'members of the host\'s tribe carried food ({tried})')
    ck(not left, f'no member carries food on the guest\'s screen that the host\'s member put down ({len(left)} left)')


def bundle_drop(host, guest, ck):
    """BUG-0661: a host member carrying food dies (`coop.tribe pincer`, the game's damage path): its food falls to the
    ground in the host's game. The guest's game must have a ground bundle within 3 m of the host's and no carried copy
    of it left (0.7.10: the guest's copy stayed bound as a carried one; the detector's `Tribe:host_only:game_bundle`)"""
    import re
    from coop_tribe import members
    hm = members(host)
    ids = {}
    for line in host.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s+#(\d+) ([0-9A-Fa-f]{8}) owner=', line)
        if m:
            ids[m.group(2).upper()] = int(m.group(1))
    rows = re.findall(r'([0-9A-F]{8}) alive=1 pos=\((\S+) (\S+) (\S+)\) dist=(\S+)', host.call('coop.tribe objs 18EB4B7')['out'])
    rows = sorted([r for r in rows if r[0] in ids and ids[r[0]] in hm], key=lambda r: float(r[4]))
    print(f'  host: {len(rows)} members ({len(hm)} in coop.tribe)', flush=True)
    done, bad = 0, []
    for p, x, y, z, d in rows[:12]:
        if done >= 2:
            break
        victim = ids[p]
        att = next((i for i, m in hm.items() if m[3] != hm[victim][3]), None)
        if att is None:
            continue
        out = host.call(f'coop.wobj bundle carry {p} 5')['out'].strip()
        if out.startswith('bundle 0') or 'bundle 00000000' in out:
            continue
        bp = re.search(r'bundle ([0-9A-F]{8})', out).group(1)
        time.sleep(3)
        hb = [b for b in bundles(host, 300) if b['p'] == bp]
        gb = [b for b in bundles(guest, 30, hb[0]['pos']) if b['cls'] & 0x100] if hb else []
        print(f'  host: member #{victim} carries food ({out}); host {hb[:1]}, guest carried copies '
              f'{[(round(math.dist(b["pos"], hb[0]["pos"]), 1), b["bound"]) for b in gb]}', flush=True)
        if not hb or not (hb[0]['cls'] & 0x100) or not gb:
            continue
        print('  host:', host.call(f'coop.tribe pincer {victim} {att} 1000')['out'].strip(), flush=True)
        time.sleep(2)
        hb2 = [b for b in bundles(host, 300) if b['p'] == bp]
        hg = [b for b in bundles(host, 30, hb[0]['pos']) if not b['cls'] & 0x100]
        print(f'    host after the death: the bundle {hb2[:1]}; ground bundles within 30 m {[b["pos"] for b in hg]}', flush=True)
        if not hg:
            print('    (no food fell on the ground in the host\'s game)', flush=True)
            continue
        done += 1
        time.sleep(6)
        hg = [b for b in bundles(host, 30, hb[0]['pos']) if not b['cls'] & 0x100]
        gg = [b for b in bundles(guest, 30, hb[0]['pos']) if not b['cls'] & 0x100]
        gc = [b for b in bundles(guest, 30, hb[0]['pos']) if b['cls'] & 0x100]
        hc = [b for b in bundles(host, 30, hb[0]['pos']) if b['cls'] & 0x100]
        lone = [h['pos'] for h in hg if not any(math.dist(h['pos'], g['pos']) <= 3 for g in gg)]
        extra = [g['pos'] for g in gc if not any(math.dist(g['pos'], h['pos']) <= 8 for h in hc)]
        print(f'    6 s later within 30 m: host ground {len(hg)} carried {len(hc)}, guest ground {len(gg)} carried {len(gc)}; '
              f'host ground without a guest one within 3 m {lone}, guest carried without a host one {extra}', flush=True)
        if lone or extra:
            bad.append((lone, extra))
    ck(done > 0, f'food a host member carried fell on the ground when it died ({done} times)')
    ck(not bad, f'the guest\'s game has that food on the ground at the host\'s place, not carried ({len(bad)} of {done} not)')


def bundle_civ(host, guest, ck):
    import re
    from play_civ import civ_state

    def ground(con):
        return [b for b in objs(con, 'game_bundle', 3000, (0.0, 0.0, 0.0)) if not b['cls'] & 0x100]

    cities, _, _ = civ_state(host)
    mine = sorted(c for c, d in cities.items() if d['player'])
    if not ck(mine, f'the host has a city ({sorted(cities)})'):
        return
    c = cities[mine[0]]['pos']
    # food on the ground by the player's city (as a fight's or a capture's loot): the host's, then one of the guest's own
    out = host.call('coop.wobj bundle ground %.2f %.2f %.2f 5' % (c[0] + 6, c[1], c[2] + 6))['out'].strip()
    print('  host:', out, flush=True)
    m = re.search(r'pos=\((\S+) (\S+) (\S+)\)', out)
    if not ck('bundle 00000000' not in out and m, 'the host\'s game dropped a food bundle by its city'):
        return
    hpos = tuple(float(x) for x in m.groups())
    got = wait(lambda: near(ground(guest), hpos, 1.5), 12)
    print(f'  guest ground bundles at the host\'s {hpos}: {got}', flush=True)
    ck(got, 'the host\'s ground food lies at its place in the guest\'s game (Civ)')
    out = guest.call('coop.wobj bundle ground %.2f %.2f %.2f 5' % (c[0] - 6, c[1], c[2] - 6))['out'].strip()
    print('  guest (its own food):', out, flush=True)
    m = re.search(r'pos=\((\S+) (\S+) (\S+)\)', out)
    if m:
        gpos = tuple(float(x) for x in m.groups())
        gone = wait(lambda: not near(ground(guest), gpos, 1.0), 12)
        print(f'  guest ground bundles at its own {gpos}: {near(ground(guest), gpos, 1.0)}; on the host: '
              f'{near(ground(host), gpos, 1.0)}', flush=True)
        ck(gone and not near(ground(host), gpos, 1.0), 'a ground bundle only the guest\'s game had is gone from it (Civ)')
    hb, gb = ground(host), ground(guest)
    print(f'  ground bundles now: {len(hb)} on the host, {len(gb)} on the guest', flush=True)


def cultural(host, guest, ck):
    import worlddiff
    from play_civ import civ_state
    noun = 0x03D5C325
    near_r = 300.0

    def targets(con, at):
        w = worlddiff.dump(con, near_r + 50, at)
        return w, [r for r in w.rows if r['noun'] == noun]

    # the census lists the objects nearest its centre: centre it on the host's first city (a census around the
    # planet's centre listed no target at all, lab 20261106-055541)
    cities, _, _ = civ_state(host)
    mine = sorted(c for c, d in cities.items() if d['player'])
    if not ck(mine, f'the host has a city ({sorted(cities)})'):
        return
    at = cities[mine[0]]['pos']
    wait(lambda: targets(host, at)[1] and targets(guest, at)[1], 40)

    def compare():
        _, ht = targets(host, at)
        _, gt = targets(guest, at)
        ht = [r for r in ht if math.dist(r['pos'], at) < near_r]
        gt = [r for r in gt if math.dist(r['pos'], at) < near_r + 20]
        far = []
        for h in ht:
            d = min((math.dist(h['pos'], g['pos']) for g in gt), default=1e9)
            if d > 1.0:
                far.append((tuple(round(v, 1) for v in h['pos']), round(d, 1)))
        return ht, gt, far

    ht, gt, far = compare()
    print(f'  around {tuple(round(v) for v in at)}: {len(ht)} cultural targets on the host, {len(gt)} on the guest; '
          f'host targets without a guest one within 1 m (pos, nearest m): {far[:6]}', flush=True)
    if not ck(ht, 'the host\'s game has cultural targets near the guest'):
        return
    # the rows came after a guest's rejoin in campaign_space (the guest's game sets its cities up again from the
    # host's world of now): play a minute, the guest leaves and joins again
    time.sleep(60)
    from coop_realign_planet import rejoin
    if not rejoin(guest, 'Civ', ck):
        return
    time.sleep(8)
    ht, gt, far = compare()
    print(f'  after the rejoin: {len(ht)} on the host, {len(gt)} on the guest, without a partner within 1 m: {far[:6]}',
          flush=True)
    ok = wait(lambda: not compare()[2], 20)
    ht, gt, far = compare()
    print(f'  after 20 s: {len(ht)} on the host, {len(gt)} on the guest, without a partner within 1 m: {far[:6]}', flush=True)
    ck(ok and not far, 'every cultural target of the host near the guest stands at its place in the guest\'s game (within 1 m)')


def ornament(host, guest, ck, rounds=3):
    import worlddiff
    from coop_space_map import context, place, sp
    noun = 0x018C88E4

    def orns(con):
        return [r for r in worlddiff.dump(con, 5000, (0.0, 0.0, 0.0)).rows if r['noun'] == noun]

    def near_pos(lst, pos, d):
        return any(math.dist(o['pos'], pos) < d for o in lst)

    def compare():
        ho, go = orns(host), orns(guest)
        # the host sends the 56 nearest the guest's ship (one packet, kind city_ornament): compare that area
        me = worlddiff.dump(guest, 1, (0.0, 0.0, 0.0)).me
        if me and len(ho) > 56:
            r = sorted(math.dist(h['pos'], me) for h in ho)[55]
            ho = [h for h in ho if math.dist(h['pos'], me) <= r - 2]
            go = [g for g in go if math.dist(g['pos'], me) <= r - 2]
        h_only = [tuple(round(v) for v in h['pos']) for h in ho if not near_pos(go, h['pos'], 1.5)]
        g_only = [tuple(round(v) for v in g['pos']) for g in go if not near_pos(ho, g['pos'], 1.5)]
        return ho, go, h_only, g_only

    bad = []
    for rnd in range(rounds):
        for con, who in ((host, 'host'), (guest, 'guest')):
            if context(con) != 'planet':
                print(f'  round {rnd + 1}: {who} land:', sp(con, 'land'), flush=True)
        landed = wait(lambda: context(host) == 'planet' and context(guest) == 'planet', 60)
        if not ck(landed, f'round {rnd + 1}: both players are on a planet'):
            return
        if not ck(place(host)[0] == place(guest)[0], f'round {rnd + 1}: both are on the same planet {place(host)} {place(guest)}'):
            return
        time.sleep(10)
        ho, go, h_only, g_only = compare()
        print(f'  round {rnd + 1}: {len(ho)} ornaments on the host, {len(go)} on the guest; host only {len(h_only)} '
              f'{h_only[:4]}, guest only {len(g_only)} {g_only[:4]}', flush=True)
        if h_only or g_only:
            wait(lambda: not any(compare()[2:]), 15)
            ho, go, h_only, g_only = compare()
            print(f'  round {rnd + 1} after 15 s: host only {len(h_only)} {h_only[:4]}, guest only {len(g_only)} {g_only[:4]}',
                  flush=True)
        ck(ho, f'round {rnd + 1}: the host\'s planet has ornaments')
        if h_only or g_only:
            bad.append(rnd + 1)
        if rnd < rounds - 1:  # up to the system and down again: the planet and its cities are set up anew
            for con in (host, guest):
                sp(con, 'takeoff')
            wait(lambda: context(host) != 'planet' and context(guest) != 'planet', 60)
            time.sleep(3)
    ck(not bad, f'the guest\'s planet has the host\'s ornaments at their places and no others in every round (not in {bad})')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('fruit', 'bundle', 'bundle_carry', 'bundle_drop', 'bundle_civ', 'cultural', 'ornament'), required=True)
    a = ap.parse_args()
    host, guest = start({'fruit': 'creature', 'bundle': 'tribe', 'bundle_carry': 'tribe', 'bundle_drop': 'tribe', 'bundle_civ': 'civ_npc', 'cultural': 'civ_npc', 'ornament': 'space'}[a.case])
    ck = Checks()
    time.sleep(6)  # the first snapshots
    {'fruit': fruit, 'bundle': bundle, 'bundle_carry': bundle_carry, 'bundle_drop': bundle_drop, 'bundle_civ': bundle_civ, 'cultural': cultural, 'ornament': ornament}[a.case](host, guest, ck)
    print('  guest:', guest.call('coop.wobj')['out'].strip().replace('\n', ' | '), flush=True)
    print('  host:', host.call('coop.wobj')['out'].strip().replace('\n', ' | '), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
