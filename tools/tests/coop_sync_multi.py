#!/usr/bin/env python3
"""Per-player sync accuracy with four players (BUG-0275): the host and Bob from the lane, Carol and
Dave as instances 20+N / 30+N (coop_multi.start_extra). Every machine plays with the stage's play
bot (play_cell / play_creature, like coop_campaign.py), and every few seconds each guest's copies of
the host's cells (c) and NPCs (n) are compared with the host, in a rotating order, with
coop_multi.measure (the host's path over the last 0.25 s). Prints per guest and kind: checks, the
median and the largest p90, the checks over the limit (coop_multi.over_limit) and the frame rate.
A measurement tool, no pass / fail of its own.

  SPORE_LANE=6 python3 tools/tests/coop_sync_multi.py --fixture creature --rounds 45
  FPSLIM=Carol:66,Dave:33   force a frame time (ms) on some guests (frame.limit): 15 / 30 fps
  --food N                  cell: the host's food progress set to N first (level-ups)
  --jsonl out.jsonl         every object's error, key and offset, one line per guest check
"""
import argparse
import json
import os
import random
import re
import threading
import time

import coop_multi as cm
import lane
from fixtures import planet_of
from sporectl import Console
from twoinst import start


def fps(con):
    m = re.search(r'fps=([\d.]+)', con.call('frame')['out'])
    return float(m.group(1)) if m else 0.0


PROBE = 1.5  # m: an NPC this far off on a guest: `coop.npc list` of it on every machine (BUG-0305)


def probe_npc(host, guests, key, times=5, gap=0.3):
    """the line of NPC `key` (n<id>) in `coop.npc list` of the host and of every guest, `times` times
    `gap` s apart: the host's speed, each proxy's err / target / own speed / teleports (BUG-0305)"""
    pat = re.compile(rf'^\s*#{key[1:]}\s.*$', re.M)
    lines = []
    for k in range(times):
        for n, con in [('Alice', host), *guests.items()]:
            m = pat.search(con.call('coop.npc list')['out'])
            sm = cm.sample(con)
            near = ''
            if key in sm:  # what stands next to it on this machine: tribe members, other animals
                me = sm[key]
                d = sorted((sum((a - b) ** 2 for a, b in zip(me, v)) ** 0.5, o) for o, v in sm.items() if o != key)
                near = ' near ' + ' '.join(f'{o}:{x:.1f}' for x, o in d[:4] if x < 6)
            lines.append(f't{k} {n}: ' + (m.group(0).strip() if m else '-') + near)
        time.sleep(gap)
    return lines


def bots(fixture, insts, stop):
    """the stage's play bot on every machine, each in a thread with its own console"""
    def run(fn):
        while not stop:
            try:
                fn()
            except Exception as e:  # noqa: BLE001 (a bot hiccup must not end the measurement)
                print('  bot error', repr(e), flush=True)
                time.sleep(1)
    if fixture == 'cell':
        import play_cell
        for name, (inst, pid) in insts.items():
            b = play_cell.Player(name, Console(inst, timeout=30), pid, 1 if pid == 0 else 0, random.Random(7 + pid))
            threading.Thread(target=run, daemon=True, args=(
                lambda b=b: (b.step(time.time(), play_cell.pool(b.c)), time.sleep(0.25)),)).start()
    elif fixture == 'tribe':
        # as coop_campaign.py's Tribe play: 3 more members per player, play_tribe's Game steps every
        # player in turn in one thread (it is not thread-safe)
        import play_tribe
        cons = {name: Console(inst, timeout=30) for name, (inst, _) in insts.items()}
        host = cons['Alice']
        print('  tribe setup:', host.call(f'coop.tribe spawn {3 * len(insts)}').get('out', '').strip(), flush=True)
        time.sleep(8)
        g = play_tribe.Game(host, cons['Bob'], 7, 1.0)
        for name, (inst, pid) in insts.items():
            if pid >= 2:
                g.p.append(play_tribe.Player(name, cons[name], pid, random.Random(7 + pid), g.log, g.w, g.h))
        for pl, name in zip(g.p, insts):
            pl.name = name
            pl.calibrate()

        def steps():
            for pl in g.p:
                g.step(pl)
        threading.Thread(target=run, args=(steps,), daemon=True).start()
    else:
        import play_creature
        world = play_creature.World(lane.HOST)
        sh = play_creature.Shared()
        sh.exit_result = None
        for name, (inst, pid) in insts.items():
            b = play_creature.Player(name, inst, 1 if pid == 0 else 0, world, sh, 7 + pid, time.time() + 3600)
            threading.Thread(target=run, args=(b.step,), daemon=True).start()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='creature', choices=('cell', 'creature', 'tribe'))
    ap.add_argument('--rounds', type=int, default=40)
    ap.add_argument('--jsonl', default=None)
    ap.add_argument('--spawn', type=int, default=0, help='tribe: coop.tribe spawn N on the host first (the campaign\'s setup, BUG-0329)')
    ap.add_argument('--food', type=int, default=0, help='cell: the host\'s food progress first (level-ups, BUG-0307 hunt)')
    a = ap.parse_args()
    host, guest = start(a.fixture, planet_of(a.fixture))
    extra = cm.start_extra(2)
    for name, (c, _) in extra.items():
        c.call(cm.join_cmd(name))
        time.sleep(1)
    print('  together:', cm.wait_all(host, 3), cm.together(host), flush=True)
    guests = {'Bob': guest, **{n: c for n, (c, _) in extra.items()}}
    for name in filter(None, (n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(','))):
        for n, (c, _) in extra.items():  # twoinst.start switched it off on the host and Bob
            print(f'  {n}: SPORE_FIX_OFF:', c.call(f'coop.fix {name} off')['out'].strip(), flush=True)
    for kv in filter(None, os.environ.get('FPSLIM', '').split(',')):
        n, ms = kv.split(':')
        print(f'  {n}: frame.limit {ms}:', guests[n].call(f'frame.limit {ms}')['out'].strip(), flush=True)
    if a.food and a.fixture == 'cell':
        print('  host: food', host.call(f'coop.pool food {a.food}')['out'].strip()[:100], flush=True)
    stop = []
    insts = {'Alice': (lane.HOST, 0), 'Bob': (lane.GUEST, 1),
             **{n: (inst, k + 2) for k, (n, (_, inst)) in enumerate(extra.items())}}
    if a.spawn:
        print('  host:', host.call(f'coop.tribe spawn {a.spawn}')['out'].strip(), flush=True)
    time.sleep(20)
    bots(a.fixture, insts, stop)
    out = open(a.jsonl, 'w') if a.jsonl else None
    stats = {}
    names = list(guests)
    try:
        for r in range(a.rounds):
            time.sleep(4)
            for name in names[r % 3:] + names[:r % 3]:
                g = guests[name]
                errs, cov, total, worst = cm.measure(host, g)
                f = fps(g)
                for kind, v in errs.items():
                    s = stats.setdefault((name, kind), {'p90': [], 'over': 0, 'fps': []})
                    s['p90'].append(cm.pct(v, 0.9))
                    s['over'] += cm.over_limit(v, cm.limit(kind))
                    s['fps'].append(f)
                probe = None
                w = worst.get('n')
                if w and w[0] > PROBE:
                    probe = probe_npc(host, guests, w[1])
                    for line in probe:
                        print(f'  r{r} {name} {w[1]} {w[0]:.2f} m |', line, flush=True)
                w = worst.get('t')
                if w and w[0] > 2.5:  # BUG-0329: a tribe member off on a guest: its coop.tribe line everywhere
                    k = w[1][1:]
                    for who, con in [('host', host)] + list(guests.items()):
                        ln = [x.strip() for x in con.call('coop.tribe')['out'].splitlines() if re.match(rf'\s*#{k}\b', x)]
                        print(f'  r{r} {name} {w[1]} {w[0]:.2f} m | {who}: {ln[0][:300] if ln else "-"}', flush=True)
                if out:
                    out.write(json.dumps({'r': r, 'g': name, 'fps': f, 'cov': cov, 'total': total,
                                          'e': {k: [round(x, 3) for x in v] for k, v in errs.items()},
                                          'worst': worst, 'probe': probe}) + '\n')
                    out.flush()
    finally:
        stop.append(1)
    for (name, kind), s in sorted(stats.items()):
        p = sorted(s['p90'])
        print(f'  {name} {kind}: {len(p)} checks, p90 median {p[len(p) // 2]:.2f} m, largest {p[-1]:.2f} m, '
              f'over the limit {s["over"]}, fps {sorted(s["fps"])[len(p) // 2]:.0f}', flush=True)
    import subprocess
    subprocess.run([lane.SPORE, 'kill'], capture_output=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
