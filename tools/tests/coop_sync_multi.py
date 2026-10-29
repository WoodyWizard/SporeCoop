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
    ap.add_argument('--fixture', default='creature', choices=('cell', 'creature'))
    ap.add_argument('--rounds', type=int, default=40)
    ap.add_argument('--jsonl', default=None)
    a = ap.parse_args()
    host, guest = start(a.fixture, planet_of(a.fixture))
    extra = cm.start_extra(2)
    for name, (c, _) in extra.items():
        c.call(cm.join_cmd(name))
        time.sleep(1)
    print('  together:', cm.wait_all(host, 3), cm.together(host), flush=True)
    guests = {'Bob': guest, **{n: c for n, (c, _) in extra.items()}}
    for kv in filter(None, os.environ.get('FPSLIM', '').split(',')):
        n, ms = kv.split(':')
        print(f'  {n}: frame.limit {ms}:', guests[n].call(f'frame.limit {ms}')['out'].strip(), flush=True)
    stop = []
    insts = {'Alice': (lane.HOST, 0), 'Bob': (lane.GUEST, 1),
             **{n: (inst, k + 2) for k, (n, (_, inst)) in enumerate(extra.items())}}
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
                if out:
                    out.write(json.dumps({'r': r, 'g': name, 'fps': f, 'cov': cov, 'total': total,
                                          'e': {k: [round(x, 3) for x in v] for k, v in errs.items()},
                                          'worst': worst}) + '\n')
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
