#!/usr/bin/env python3
"""Tribe: members under the host's ground, the probes of BUG-1104 (not reproduced; prints, one check).

The 4-player campaigns' monitor ground rule found a player member 1.0-2.0 m under the host's ground (GetHeightAt) on
1-2 trace lines (10-20 s, the same depth on both), always during a raid on an NPC hut (campaign_tribe_end, 4 of 4) or
an epic fight (campaign, 2 of 2), the host at 10-20 fps (docs/re/tribe.md#tribe-member-under). The probes, on the
tribe fixture with the host at 10 fps (all members read with `coop.tribe airprobe -1000`: height over GetHeightAt and
over the game's own surface point, node, locomotion mode, grounded byte, velocity; `B` rows: the tracked bodies):
  --rounds N [--blows] [--hangs]  a member thrown by a blow (`coop.tribe pincer`), the host's main thread held 0-1000 ms
                                  during the throw (`kit.test.hang`, the campaign's long frames): where it lands
  --sinks 1.3,2,..  [--air]       a member put that deep under the ground once (`coop.tribe sink`; --air: not grounded,
                                  standing still, the airborne node's state): CHECK the game has it on its ground
                                  again within 1 s (no placing can leave a member under the ground for 10-20 s)
  --raid S                        the host's members raid the fixture's NPC hut for S s (the hut mended)
  --stomp 0,1.5,.. [--quiet]      the co-op epic put onto a member (`coop.tribe epic on`)
  --bodies N [--body-wait]        N members killed: their bodies' height until the game removes them
  --racks 9,16,..                 weapon racks and the heights of their grab spots (`coop.tribe layout`)
  --secs S                        S s of a fight with the co-op epic
  SPORE_LANE=2 SPORE_FPS=host:10 python3 tools/tests/coop_tribe_under.py --rounds 0 --sinks 1.3,2 --raid 120
"""
import argparse
import re
import time

from twoinst import Checks, start

ROW = re.compile(r'[MB] #(\d+) \S+ owner=(-?\d+) agl=([-\d.]+) surf=([-\d.]+) .*')


def probe(con):
    rows = []
    for line in con.call('coop.tribe airprobe -1000')['out'].splitlines():
        m = ROW.match(line)
        if m:
            rows.append((int(m.group(1)), int(m.group(2)), float(m.group(3)), float(m.group(4)), line))
    return rows


def members(con):
    """ids of the living, non-leader player members"""
    out = []
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s+#(\d+) \S+ owner=(\d+) age=1 sel=\d+ hp=([\d.]+).*?dead=(\d).*?leader=(\d)', line)
        if m and float(m.group(3)) > 20 and int(m.group(2)) < 0xFE and m.group(4) == '0' and m.group(5) == '0':
            out.append(m.group(1))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=0)
    ap.add_argument('--blows', default='100')
    ap.add_argument('--hangs', default='0,300,600,1000')
    ap.add_argument('--rounds', type=int, default=12)
    ap.add_argument('--bodies', type=int, default=0)
    ap.add_argument('--racks', default='')
    ap.add_argument('--sinks', default='')
    ap.add_argument('--wound', action='store_true')
    ap.add_argument('--air', action='store_true')
    ap.add_argument('--stomp', default='')
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--raid', type=float, default=0)
    ap.add_argument('--body-wait', type=float, default=90)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(4)
    if a.secs:
        print('  host:', host.call('coop.tribe epic force')['out'].strip(), flush=True)
    else:
        print('  host:', host.call('coop.tribe spawn 6')['out'].strip()[:80], flush=True)
        time.sleep(6)
    own = []
    for line in host.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s+#(\d+) \S+ owner=(\d+) age=1 sel=\d+ hp=([\d.]+).*?leader=(\d)', line)
        if m and m.group(1) != '0' and float(m.group(3)) > 0 and int(m.group(2)) < 0xFE and m.group(4) == '0':
            own.append(m.group(1))
    print('  player members:', own, flush=True)
    # blows: each a member thrown by a blow of another member (`coop.tribe pincer`), the host's main thread held for
    # <hang> ms while it is in the air (the campaign's 200-1000 ms frames), its height read 4 times a second
    dmgs = [float(x) for x in a.blows.split(',') if x]
    hangs = [int(x) for x in a.hangs.split(',') if x]
    res = []
    for i in range(a.rounds):
        alive = [m for m in members(host) if m != '0']
        if len(alive) < 2:
            break
        vic, att = alive[i % len(alive)], alive[(i + 1) % len(alive)]
        dmg, hang, delay = dmgs[i % len(dmgs)], hangs[i % len(hangs)], 0.3 + 0.3 * ((i // len(hangs)) % 3)
        out = host.call(f'coop.tribe pincer {vic} {att} {dmg:g}')['out'].strip()[:60]
        if hang:
            time.sleep(delay)
            host.call(f'kit.test.hang {hang}')
        t1, lo, hi, last, under = time.time(), 1e9, -1e9, '', 0
        while time.time() - t1 < 12:
            for mid, owner, agl, surf, line in probe(host):
                if str(mid) == vic:
                    if agl < lo:
                        lo, last = agl, f't={time.time() - t1:.2f} {line}'
                    hi = max(hi, agl)
                    if agl < -0.5:
                        under += 1
                        if under <= 3:
                            print(f'    t={time.time() - t1:.2f} UNDER {line}', flush=True)
            time.sleep(0.25)
        res.append((dmg, hang, delay, hi, lo, under))
        host.call(f'coop.tribe sethp {vic} 150')
        print(f'  blow {dmg:g} on #{vic} ({out}), hang {hang} ms at {delay:.1f} s: highest {hi:.2f}, lowest {lo:.2f} m, '
              f'{under} reads under 0.5 m: {last}', flush=True)
    # raid: every player member (host and guest) raids the hut of the fixture's NPC village (the host's own order path:
    # select + DoActionForObjects, CitizenAction 15), the hut kept standing (`coop.tribe huthp` back to 400); every
    # member read 4 times a second, the ones under 0.5 m / over 3 m printed with their distance to the hut
    if a.raid:
        from coop_tribe_conquer_orders import HUT, huts
        from coop_tribe_tools import members as all_own
        from wsync_util import dump
        import math
        own = sorted(all_own(host))
        home = None
        for line in host.call('coop.tribe')['out'].splitlines():
            m = re.search(r'#%d .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)' % own[0], line)
            if m:
                home = tuple(float(m.group(i)) for i in (1, 2, 3))
        vil = dump(host, 'tribe_rel').get('tribe_rel', [])
        vil.sort(key=lambda e: math.dist(e['pos'], home))
        hh = sorted((math.dist(p, vil[0]['pos']), h, p) for h, p in huts(host))
        hp = hh[0][2]
        print(f'  raiders {own}; hut at {hp}, {math.dist(hp, home):.0f} m away', flush=True)
        def order():
            for mid in own:
                host.call(f'coop.tribe select {mid} 1')
            print('  order:', host.call('coop.tribe act_obj 15 %s %.2f %.2f %.2f' % ((HUT,) + tuple(hp)))['out'].strip()[:100], flush=True)
        order()
        t1, under, over, k = time.time(), {}, {}, 0
        while time.time() - t1 < a.raid:
            k += 1
            if k % 120 == 0:
                print('  hut:', host.call('coop.tribe huthp %.2f %.2f %.2f 400' % hp)['out'].strip()[:80], flush=True)
                order()
            for line in host.call('coop.tribe airprobe -1000')['out'].splitlines():
                m = re.match(r'M #(\d+) \S+ owner=(-?\d+) agl=([-\d.]+) surf=([-\d.]+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
                if not m:
                    continue
                agl = float(m.group(3))
                d = math.dist(hp, tuple(float(m.group(i)) for i in (5, 6, 7)))
                for lim, book, tag in ((-0.5, under, 'UNDER'), (3.0, over, 'OVER')):
                    if (agl < lim) if tag == 'UNDER' else (agl > lim):
                        n = book.get(m.group(1), 0)
                        book[m.group(1)] = n + 1
                        if n < 3 or n % 20 == 0:
                            print(f'    t={time.time() - t1:5.1f} {tag} d_hut={d:.1f} {line}', flush=True)
            time.sleep(0.25)
        print(f'  raid: reads under 0.5 m {under}, over 3 m {over}', flush=True)
    # stomp: the co-op epic put onto a member (`coop.tribe epic on <id> <dx>`), the member's height read 15 s
    if a.stomp:
        print('  host:', host.call('coop.tribe epic force')['out'].strip(), flush=True)
        time.sleep(2)
        if a.quiet:
            print('  host:', host.call('coop.tribe epic quiet')['out'].strip(), flush=True)
    for i, dx in enumerate(float(x) for x in a.stomp.split(',') if x):
        alive = [m for m in members(host) if m != '0']
        if not alive:
            break
        vic = alive[i % len(alive)]
        print(f'  #{vic} dx {dx:g}:', host.call(f'coop.tribe epic on {vic} {dx:g}')['out'].strip(), flush=True)
        t1, seq = time.time(), []
        while time.time() - t1 < 15:
            for mid, owner, agl, surf, line in probe(host):
                if str(mid) == vic and line.startswith('M'):
                    seq.append((round(time.time() - t1, 1), agl, re.search(r'bt=(\w+)', line).group(1),
                                re.search(r' gr=(\d)', line).group(1), re.search(r' v=\(([^)]*)\)', line).group(1),
                                re.search(r'd_epic=([-\d.]+)', line).group(1)))
            time.sleep(0.25)
        under = [x for x in seq if x[1] < -0.5]
        over = [x for x in seq if x[1] > 3]
        print(f'    {len(under)} under 0.5 m, {len(over)} over 3 m of {len(seq)} reads; lowest {min(seq, key=lambda x: x[1]) if seq else None}; '
              f'highest {max(seq, key=lambda x: x[1]) if seq else None}', flush=True)
        for x in seq[::4]:
            print(f'      {x}', flush=True)
    # sinks: a member put <m> under the ground once (`coop.tribe sink`, test setup): does the game keep it there?
    for i, depth in enumerate(float(x) for x in a.sinks.split(',') if x):
        alive = [m for m in members(host) if m != '0']
        if not alive:
            break
        vic = alive[i % len(alive)]
        if a.wound:
            host.call(f'coop.tribe sethp {vic} 10')
            time.sleep(3)
        print(f'  #{vic}:', host.call(f'coop.tribe sink {vic} {depth:g}' + (' air' if a.air else ''))['out'].strip(), flush=True)
        t1, seq = time.time(), []
        while time.time() - t1 < 20:
            for mid, owner, agl, surf, line in probe(host):
                if str(mid) == vic and line.startswith('M'):
                    seq.append((round(time.time() - t1, 1), agl, surf, re.search(r'bt=(\w+)', line).group(1),
                                re.search(r' gr=(\d)', line).group(1), re.search(r' v=\(([^)]*)\)', line).group(1)))
            time.sleep(0.25)
        under = [x for x in seq if x[1] < -0.5]
        print(f'    {len(under)} of {len(seq)} reads under 0.5 m, first ones: {seq[:6]}', flush=True)
        print(f'    last under: {under[-1] if under else None}; after: {seq[len(under):len(under) + 3]}', flush=True)
        late = [x for x in under if x[0] > 1.0]
        ck(bool(seq) and not late, f'#{vic} put {depth:g} m under the ground: on its ground again within 1 s '
           f'({len(late)} reads under 0.5 m after 1 s of {len(seq)})')
    # racks: weapon racks built at these distances from the village centre; their grab spots' height over the ground
    for d in [float(x) for x in a.racks.split(',') if x]:
        print(f'  build at {d:g} m:', host.call(f'coop.tribe build 1 {d:g}')['out'].strip(), flush=True)
        time.sleep(1)
        print('   ', host.call('coop.tribe layout 1')['out'].strip().replace(chr(10), chr(10) + '    '), flush=True)
    # bodies: player members killed (a killing blow), the host's game keeps their bodies out of the member list; the
    # host tracks them 60 s (BUG-0620): their height read twice a second until they go
    if a.bodies:
        alive = [m for m in members(host) if m != '0']
        vics = alive[:a.bodies]
        for v in vics:
            print(f'  kill #{v}:', host.call(f'coop.tribe pincer {v} {alive[-1]} 5000')['out'].strip()[:60], flush=True)
        t1, last = time.time(), {}
        while time.time() - t1 < a.body_wait:
            for line in host.call('coop.tribe airprobe 1000')['out'].splitlines():
                m = re.match(r'B #(\d+) \S+ owner=\d+ agl=([-\d.]+) surf=([-\d.]+)', line)
                if m and m.group(1) in vics:
                    k = (round(float(m.group(2)), 1), round(float(m.group(3)), 1))
                    tick = int((time.time() - t1) // 5)
                    if float(m.group(2)) < -0.05 or float(m.group(3)) < -0.05:
                        tick = time.time()  # under: every read
                    if last.get(m.group(1)) != (k, tick):
                        last[m.group(1)] = (k, tick)
                        print(f'    t={time.time() - t1:5.1f} {line}', flush=True)
            time.sleep(0.1)
    for hang in sorted(set(hangs)):
        rr = [r for r in res if r[1] == hang]
        print(f'  hang {hang} ms: {sum(1 for r in rr if r[5])} of {len(rr)} throws ended under the ground', flush=True)
    t0, k, under, seen = time.time(), 0, {}, 0
    while time.time() - t0 < a.secs:
        if own and k % 16 == 0:
            host.call(f'coop.tribe epic near {own[(k // 16) % len(own)]}')
        k += 1
        rows = probe(host)
        seen += len(rows)
        for mid, owner, agl, surf, line in rows:
            if agl < -0.5:
                n = under.get(mid, 0)
                under[mid] = n + 1
                if n < 6 or n % 10 == 0:
                    print(f'  t={time.time() - t0:.1f} UNDER {line}', flush=True)
        time.sleep(0.5)
    print(f'  members read {seen} times; under 0.5 m: {under}', flush=True)
    print('  host:', host.call('coop.tribe epic stats')['out'].strip().replace('\n', '\n  '), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
