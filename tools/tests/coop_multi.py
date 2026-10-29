#!/usr/bin/env python3
"""Three or four players in one session, for minutes (docs/stability-report.md): the host and the
first guest from lane A (SPORE_LANE), one or two more guests either in the same lane (default, how
the lab runs it: instances 20+N and 30+N in lane N's bottle, own console and co-op ports that no
lane uses) or from another lane B (SPORE_LANE_B=B: its own bottle and profiles). The chaos bot plays
all of them (real input, rejoins), and every minute the session is checked:

  together   the host sees every guest in its world
  desync     every guest's copy of every synchronized object vs the host's (90th percentile of
             the position error per kind, coop_sync.LIMITS), and how much of the host's world the
             guest has (coverage)
  shared     food / money the same on every machine
  health     no freeze, no crash, no caught fault (twoinst monitors, exc)

  SPORE_LANE=5 python3 tools/tests/coop_multi.py --fixture tribe --players 3 --minutes 3
  SPORE_LANE=1 SPORE_LANE_B=8 python3 tools/tests/coop_multi.py --fixture tribe --players 4 --minutes 8
"""
import argparse
import os
import random
import re
import subprocess
import sys
import time

import lane
from coop_chaos import Bot
from coop_sync import LIMITS, pct
from sporectl import Console
from twoinst import Checks, Monitor, shot, start, wait_up

NAMES = ['Carol', 'Dave']


def lane_b():
    """the lane of the extra guests: this one unless SPORE_LANE_B names another (the lab gives a test
    one lane: the next lane belongs to other work)"""
    b = int(os.environ.get('SPORE_LANE_B') or lane.LANE)
    env = dict(os.environ, SPORE_LANE=str(b))
    return b, env


def extra_instances(b, n):
    """instance numbers of n extra guests: the other lane's two, or 20+N / 30+N in this lane (console
    ports 47820..47839 and co-op ports 47830..47849 are no lane's: lanes 1..10 use instances 1..20)"""
    if b == lane.LANE:
        return [20 + b, 30 + b][:n]
    return [2 * b - 1, 2 * b][:n]


def sample(con):
    d = {}
    for line in con.call('coop.sync')['out'].splitlines():
        f = line.split()
        if len(f) == 8:
            try:
                d[f[0]] = tuple(map(float, f[1:4]))
            except ValueError:
                pass
    return d


def res(con):
    out = con.call('coop.res')['out']
    return {m.group(1): float(m.group(2)) for m in re.finditer(r'shared (\w+)=(-?[\d.]+)', out)}


def together(host):
    return sorted(int(m.group(1)) for m in re.finditer(r'^together (\d+) 1$', host.call('coop.status')['out'], re.M))


def start_extra(n):
    """n more guests from lane B, joined to lane A's host; {name: console}"""
    b, env = lane_b()
    sh = lambda *a: subprocess.run([lane.SPORE, *a], env=env, capture_output=True)  # noqa: E731
    if b != lane.LANE:  # another lane: a clean start there (in this lane the host and guest run already)
        sh('lane', 'create', str(b)) if b > 1 else None
        sh('kill')
        sh('deploy')
    out = {}
    insts = extra_instances(b, n)
    for i, inst in enumerate(insts):
        prof = f'mp{i + 1}'
        # an empty profile: the host's world arrives
        appdata = lane.APPDATA.replace(f'/{lane.BOTTLE}/', f'/SporeLab{b}/' if b > 1 else '/Games/')
        subprocess.run(['rm', '-rf', f'{appdata}/SporeKit-{prof}/Spore/Games'])
        sh('run', '-i', str(inst), '-p', prof)
        time.sleep(2)
    for i, inst in enumerate(insts):
        assert wait_up(inst, 120), f'extra guest instance {inst} did not start'
        c = Console(inst)
        for _ in range(120):
            if 'GalaxyGameEntry' in c.call('game.mode')['out']:
                break
            time.sleep(1)
        c.call('wait.frames 200 60000')
        out[NAMES[i]] = (c, inst)
    return out


def join_cmd(name):
    return lane.JOIN.replace(' Bob', ' ' + name)


def wait_all(host, want, timeout=150):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(together(host)) >= want:
            return True
        f1 = re.search(r'frame=(\d+)', host.call('frame')['out'])
        time.sleep(0.7)
        f2 = re.search(r'frame=(\d+)', host.call('frame')['out'])
        if f1 and f2 and f1.group(1) == f2.group(1):
            host.call('key enter')  # the host's "game saved" box (world.cpp closes it too)
    return False


def mode(con):
    return con.call('game.mode')['out'].strip().split('\n')[0].split(' ', 1)[-1][:20]


def fps(con):
    m = re.search(r'fps=([\d.]+)', con.call('frame')['out'])
    return m.group(1) if m else '?'



def my_key(con, sample_keys):
    """the guest's own player key in its coop.sync sample (p<id>)"""
    remotes = {f'p{m.group(1)}' for m in re.finditer(r'^remote (\d+) ', con.call('coop.status')['out'], re.M)}
    mine = [k for k in sample_keys if k.startswith('p') and k not in remotes]
    return mine[0] if len(mine) == 1 else None


def check_round(ck, host, guests, label, report):
    """desync / coverage / resources of every guest against the host"""
    report.append(f'  [{label}] modes: host {mode(host)} {fps(host)} fps, '
                  + ', '.join(f'{n} {mode(g)} {fps(g)} fps' for n, g in guests.items())
                  + ' | host ' + host.call('coop.npc')['out'].strip().splitlines()[0][:120])
    h1 = sample(host)
    worst = {}
    for name, g in guests.items():
        errs, cov, total, _ = measure(host, g)
        over = [k for k, v in errs.items() if over_limit(v, limit(k))]
        # one moment of one guest: a guest that has just rejoined spawns its NPC proxies within
        # seconds (a churn right before the round), a fight can throw a few NPCs for a moment. A
        # real desync stays: measure again
        if cov < 0.85 or over:
            report.append(f'  [{label}] {name}: coverage {cov:.0%}, over the limit {over}: measured again 6 s later')
            report.append('      ' + ' | '.join(l.strip()[:110] for l in g.call('coop.npc')['out'].splitlines()[:2])
                          + ' | host ' + host.call('coop.npc')['out'].splitlines()[-1].strip()[:110])
            time.sleep(6)
            wait_all(host, len(guests), 90)  # a churn just before the round: the guest is still loading
            time.sleep(4)
            h1 = sample(host)
            errs, cov, total, _ = measure(host, g)
        kinds = {}
        for k in h1:
            kinds[k[0]] = kinds.get(k[0], 0) + 1
        line = f'{name}: coverage {cov:.0%} of {total} (host {dict(sorted(kinds.items()))})'
        for kind, v in sorted(errs.items()):
            p90 = pct(v, 0.9)
            line += f', {kind} p90 {p90:.2f} m'
            worst[(name, kind)] = (p90, limit(kind), over_limit(v, limit(kind)))
        report.append(f'  [{label}] {line}')
        ck(cov >= 0.85, f'[{label}] {name} has the host\'s world ({cov:.0%})')
    for (name, kind), (p90, lim, over) in worst.items():
        ck(not over, f'[{label}] {name}: {kind} objects in sync (p90 {p90:.2f} m, limit {lim} m for 10 % and 2 objects)')
    hr = res(host)
    for name, g in guests.items():
        gr = res(g)
        same = all(abs(gr.get(k, v) - v) <= max(2.0, abs(v) * 0.02) for k, v in hr.items())
        ck(same, f'[{label}] {name}: shared resources match ({gr} vs {hr})')


def limit(kind):
    return LIMITS.get(kind, (3.0, 30))[0] * 2  # chaos keeps everything moving: twice the still limit


def seg_dist(p, a, b):
    """distance of p from the segment a-b: the guest shows the host's object somewhere between the
    host's two samples around its own (h1 can be seconds old for the last of four guests; the two
    ends alone counted the object's way in between as a desync, BUG-0269)"""
    d = [y - x for x, y in zip(a, b)]
    dd = sum(v * v for v in d)
    t = 0.0 if dd == 0 else max(0.0, min(1.0, sum((q - x) * v for q, x, v in zip(p, a, d)) / dd))
    return sum((q - (x + t * v)) ** 2 for q, x, v in zip(p, a, d)) ** 0.5


# the host's path is sampled over this much time before the guest is read: the guest shows NPCs
# 0.15 s late on purpose (npc.cpp buf.delay) and cells from their last state (0.1 s), and a guest
# read ~10 ms after the host's sample lay outside that sample's segment (BUG-0275)
LAG_GAP = 0.25


def host_path(host):
    """two host samples LAG_GAP apart: the start of the host's path the guest is compared with"""
    h0 = sample(host)
    time.sleep(LAG_GAP)
    return h0, sample(host)


def over_limit(v, lim):
    """a kind is out of sync when at least 10 % of its objects, and at least two of them, are
    farther than the limit. With fewer than 10 objects (2 of 3 checks in the Creature stage) the
    90th percentile was the largest error: one object off (a proxy pushed by the guest's own
    avatar, BUG-0281) made the whole kind a desync (BUG-0275)"""
    n = sum(1 for e in v if e > lim)
    return n >= max(2, len(v) - int(0.9 * len(v)))


def measure(host, g, path=None):
    """position errors per kind of one guest against the host's path (host_path() before the
    guest's sample, one more host sample after it), coverage, objects counted.
    Returns errs {kind: [m]}, coverage, total, worst {kind: (m, key)}"""
    h0, h1 = path or host_path(host)
    gs = sample(g)
    h2 = sample(host)
    errs, worst = {}, {}
    missing = 0
    me = gs.get(my_key(g, gs))
    total = 0
    for key, hp in h1.items():
        if key.startswith('p'):
            continue
        # the host streams NPCs within 110 m of each guest (npc.cpp kRelevance): only those count
        if key.startswith('n') and me and sum((a - b) ** 2 for a, b in zip(hp, me)) ** 0.5 > 90:
            continue
        total += 1
        gp = gs.get(key)
        if gp is None:
            missing += 1
            continue
        e = min(seg_dist(gp, h0.get(key, hp), hp), seg_dist(gp, hp, h2.get(key, hp)))
        errs.setdefault(key[0], []).append(e)
        if e > worst.get(key[0], (-1, ''))[0]:
            worst[key[0]] = (e, key)
    cov = 1 - missing / total if total else 1
    return errs, cov, total, worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='tribe')
    ap.add_argument('--players', type=int, default=4, choices=(3, 4))
    ap.add_argument('--minutes', type=float, default=6)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--rate', type=float, default=2.0)
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  multi: {a.players} players, fixture {a.fixture}, {a.minutes} min, seed {seed}', flush=True)
    host, guest = start(a.fixture)
    ck = Checks()
    extra = start_extra(a.players - 2)
    t0 = time.time()
    for name, (c, _) in extra.items():
        c.call(join_cmd(name))
        time.sleep(1)
    ck(wait_all(host, a.players - 1), f'all {a.players - 1} guests are in the host\'s world '
       f'({time.time() - t0:.0f} s, together {together(host)})')
    # watch every game for freezes and crashes
    for name, (c, inst) in extra.items():
        m = Monitor(inst, name)
        m.start()
    guests = {'Bob': guest, **{n: c for n, (c, _) in extra.items()}}
    report = []
    check_round(ck, host, guests, 'start', report)
    ids = {'host': 0, 'guest': 1}
    bot = Bot(host, guest, a.fixture, seed, a.rate, extra={n: c for n, (c, _) in extra.items()},
              joins={'guest': lane.JOIN, **{n: join_cmd(n) for n in extra}}, ids=ids,
              back_in=lambda name: wait_all(host, a.players - 1, 120))
    bot.weights['pause'] = 0  # random clicks into an open pause menu can quit to the galaxy menu
    t_end, t_check, n, ok, rounds = time.time() + a.minutes * 60, time.time() + 60, 0, True, 0
    try:
        while time.time() < t_end and ok:
            ok = bot.step()
            n += 1
            if Monitor.events:
                ok = False
            if time.time() > t_check:
                rounds += 1
                t_check = time.time() + 60
                if wait_all(host, a.players - 1, 60):
                    check_round(ck, host, guests, f'min {rounds}', report)
                else:
                    ck(False, f'[min {rounds}] all guests in the host world (together {together(host)})')
    except (OSError, ValueError) as e:
        bot.log.append(f'  !! console error: {e!r}')
        ok = False
    print('\n'.join(report))
    print(f'  {n} actions, {bot.joins} rejoins:', ' '.join(f'{k}={v}' for k, v in sorted(bot.count.items())))
    ck(ok, 'the session survived')
    if not ok:
        print('  last actions:')
        for line in bot.log[-30:]:
            print('   ', line)
        print(f'  replay: SPORE_LANE={lane.LANE} SPORE_LANE_B={lane_b()[0]} python3 tools/tests/coop_multi.py '
              f'--fixture {a.fixture} --players {a.players} --minutes {a.minutes} --seed {seed}')
    try:
        shot(host, f'multi_{a.fixture}_host.png')
        cons = {'host': host, 'guest': guest, **{n: c for n, (c, _) in extra.items()}}
        ck.faults(**cons)
    except (OSError, ValueError) as e:
        ck(False, f'every game answers at the end ({e!r})')
    subprocess.run([lane.SPORE, 'kill'], env=lane_b()[1], capture_output=True)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
