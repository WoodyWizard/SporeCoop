#!/usr/bin/env python3
"""Cell -> Creature -> Tribe the players' way over a player's home connection, the guest on a weak PC.

Run it with SPORE_NET=player (latency spikes, loss bursts: netem.py) and SPORE_FPS=guest:30 (suite.py
does). Two players from the `cell` fixture; each stage change is the campaign's (coop_campaign.py
Campaign.advance): the host fills the stage's bar (Cell `coop.pool food 1000`, Creature
`AddEvolutionPoints 3000`), presses History and its check like a player; the evolution vote is answered
by the guest's real "I'm ready" click, the editors are left with "done", captions with their check.

After each stage change, on the GUEST's game:
  follows   it plays the host's new stage with an avatar, the host has it in its world (Campaign.follow:
            together for 20 s)
  sees      after --settle s it draws the host within the stage's distance of where the host's own game
            has it (coop.sync p0 against the host's avatar), on 3 reads in a row within 20 s
  no desync for --watch s after the settle time no `sync-check: MISMATCH` line in either game's log (the
            mod's digest compare of the two games' objects: a difference that lasted 3 s)
Campaign findings other than `info` (a dead game, a crash report, a freeze, a caught fault, a vote that
ran out of time although the guest was ready) fail the test too.

  SPORE_LANE=9 SPORE_NET=player SPORE_FPS=guest:30 python3 tools/tests/coop_transition_net.py
  TRANSITION_PLANT=mismatch: the guest's own creature is shifted (`coop.diag sync force shift`) during
  the watch: must FAIL (shows the MISMATCH check works)
"""
import argparse
import math
import os
import random
import re
import time
from types import SimpleNamespace

import campaign_lib as cl
import lane
from coop_campaign import Campaign, Player
from coop_multi import sample
from party import NEAR
from twoinst import Checks, start


def own_pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(map(float, m.groups())) if m else None


def log_size(inst):
    try:
        return os.path.getsize(lane.game_log(inst))
    except OSError:
        return 0


def log_since(inst, off):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(off)
            return f.read().decode('utf-8', 'replace').splitlines()
    except OSError:
        return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--to', default='tribe', choices=('creature', 'tribe'))
    ap.add_argument('--settle', type=float, default=30, help='s after the guest followed before the checks')
    ap.add_argument('--watch', type=float, default=60, help='s without a MISMATCH after the settle time')
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--allow', action='append', default=[],
                    help='"<kind>=BUG-NNNN": a MISMATCH of this kind ("t missing") is a known open bug: printed, not counted')
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    plant = os.environ.get('TRANSITION_PLANT', '')
    print(f'  network {os.environ.get("SPORE_NET") or "plain"}, frame caps {os.environ.get("SPORE_FPS") or "none"}, '
          f'seed {seed}' + (f', PLANTED: {plant}' if plant else ''), flush=True)
    ck = Checks()
    t_start = time.time()
    host_con, guest_con = start('cell')
    players = [Player('Alice', lane.HOST), Player('Bob', lane.GUEST, lane.JOIN)]
    ca = SimpleNamespace(start='cell', end=a.to, minutes=0, advance_minutes=None, players=2, noise=0.0,
                         rejoins=False, finale=False, finale_from_galaxy=False, jump=False, seed=seed)
    run_dir = lane.out_dir('transition', time.strftime('%Y%m%d-%H%M%S'))
    camp = Campaign(ca, players, run_dir, seed)
    camp.ids()
    camp.health()
    host, guest = players
    stages = cl.STAGES[:cl.STAGES.index(a.to) + 1]
    for i, st in enumerate(stages[:-1]):
        nxt = stages[i + 1]
        t0 = time.time()
        ok = camp.advance(st)
        ck(ok, f'[{nxt}] the host changed {st} -> {nxt} the players\' way and the guest followed '
               f'({time.time() - t0:.0f} s)')
        if not ok:
            break
        gst = cl.stage_of(guest.call('game.mode'))
        ck(gst == nxt and guest.con.call('coop.avatar').get('ok') and guest.pid in camp.together(),
           f'[{nxt}] the guest\'s game plays {nxt} with an avatar in the host\'s world ({gst})')
        time.sleep(a.settle)
        near = NEAR.get(nxt, 6.0)
        good, last = 0, None
        t1 = time.time()
        while time.time() - t1 < 20 and good < 3:
            hp, at = own_pos(host.con), sample(guest.con).get('p0')
            last = None if hp is None or at is None else math.dist(hp, at)
            good = good + 1 if last is not None and last <= near else 0
            time.sleep(1)
        ck(good >= 3, f'[{nxt}] after {a.settle:.0f} s the guest draws the host where it is (<= {near} m: '
           f'{"not shown" if last is None else "%.1f m" % last})')
        offs = {p.name: log_size(p.inst) for p in players}
        if plant == 'mismatch' and nxt == stages[-1]:
            print('  PLANTED: ' + guest.call('coop.diag sync force shift').strip(), flush=True)
        time.sleep(a.watch)
        if plant == 'mismatch':
            guest.call('coop.diag sync force off')
        mm = {p.name: [l for l in log_since(p.inst, offs[p.name]) if 'sync-check: MISMATCH' in l] for p in players}
        allowed = dict(x.split('=', 1) for x in a.allow)
        for name, lines in mm.items():
            for l in lines[:3]:
                print(f'    {name}: {l.strip()[:200]}', flush=True)
            # a known open bug (the lab's allow() rule: printed with its ticket, never silent); only when every
            # difference the line names is of an allowed kind
            keep = []
            for l in lines:
                kinds = [f'{o} {w}' for o, w in re.findall(r'\b([a-z]+): \d+ (missing|extra|apart)', l.split(' compared')[-1])]
                if kinds and all(k in allowed for k in kinds):
                    t = allowed[kinds[0]]
                    print(f'    allowed [{t}]: {name}: {l.strip()[-120:]}', flush=True)
                else:
                    keep.append(l)
            mm[name] = keep
        ck(not any(mm.values()), f'[{nxt}] no sync-check MISMATCH in {a.watch:.0f} s after the settle time '
           f'({", ".join(f"{k} {len(v)}" for k, v in mm.items())})')
        camp.health()
    bad = [it for it in camp.F.items if it['kind'] != 'info']
    for it in camp.F.items:
        print(f'    finding [{it["kind"]}] {it["stage"]}: {it["text"]}', flush=True)
    ck(not bad, f'no campaign finding (dead game, crash, freeze, fault, vote time-out): {len(bad)}')
    print('  timeline:\n' + '\n'.join('    ' + l for l in camp.timeline[-40:]), flush=True)
    print(f'  ({time.time() - t_start:.0f} s)', flush=True)
    ck.faults(host=host_con, guest=guest_con)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
