#!/usr/bin/env python3
"""Votes of four players over their own bad networks (SPORE_NET per guest, netem.py): an evolution vote,
an action vote while one guest is still in its own editor and leaves, the leaver's return, a vote of
all four. Every check reads the players' games (their mode, their co-op window, what they draw), the
host's log only for how a vote ended (tools/tests/party.py).

1 editor   the host's evolution request (`coop.vote editor`); Bob, Carol and Dave click "I'm ready" in
           their co-op windows (real clicks, packets lost on the way): the vote ends "everybody is ready"
           (not by its 60 s timer) and every machine's game opens an editor within 20 s. The host, Bob
           and Carol leave theirs with "done" and are back in the stage together; Dave stays in his.
2 leaver   an action vote (`coop.vote action`) while Dave edits: Bob and Carol ready; the windows of
           the host, Bob and Carol all show the same ready count; the vote waits. Dave leaves the
           session from his editor: the vote passes within 10 s, once. Dave's game keeps running.
3 return   Dave leaves his editor with "done" and joins again: he plays the host's world, every game
           shows every other player where that player is, as the species that player plays.
4 all      an action vote of all four: everybody ready, it passes within 10 s, once.
No caught fault on any machine; the invariant monitor watches all four games.

  SPORE_LANE=9 SPORE_NET=players python3 tools/tests/coop_vote_net_4p.py
  VOTE_PLANT=noready: Dave's ready in step 4 is not clicked (a ready that never counts): must FAIL.
  SPORE_FIX_OFF=world.share_drop_peer did not fail it (lab 20261101-152552).
"""
import argparse
import os
import re
import time

import lane
from coop_party import actions_run, ran_once, ready_counts, wait_until
from coop_party_rejoin_net import species_view
from party import Party
from twoinst import Checks, shot

DONE = (1416, 1062)


def in_editor(p):
    return 'Editor' in p.call('game.mode')


def done(p):
    p.call(f'move {DONE[0]} {DONE[1]}')
    time.sleep(0.4)
    p.call(f'click {DONE[0]} {DONE[1]}')


def host_log_since(off):
    try:
        with open(lane.game_log(lane.HOST), 'rb') as f:
            f.seek(off)
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def log_size():
    try:
        return os.path.getsize(lane.game_log(lane.HOST))
    except OSError:
        return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='creature')
    a = ap.parse_args()
    print(f'  network per guest: {os.environ.get("SPORE_NET") or "plain"}', flush=True)
    ck = Checks()
    t0 = time.time()
    p = Party.start(a.fixture, 4)
    try:
        print(f'  players {p.ids()} in {time.time() - t0:.0f} s', flush=True)
        if not p.check_together(ck, 'start'):
            return ck.result()
        time.sleep(5)
        bob, carol, dave = p.guests
        stage0 = p.stage(p.host)

        # 1. the evolution vote
        off = log_size()
        print('  host: ' + p.host.call('coop.vote editor').strip().splitlines()[0], flush=True)
        time.sleep(2)
        for gp in p.guests:
            ck(p.vote_ready(gp), f'[editor] {gp.name} clicks "I\'m ready"')
        ok = wait_until(lambda: all(in_editor(x) for x in p.all), 30)
        modes = {x.name: x.call('game.mode').strip() for x in p.all}
        ck(ok, f'[editor] every machine\'s game opened an editor: {modes}')
        ends = host_log_since(off)
        how = [l for l in ends.splitlines() if 'evolution starts' in l]
        print('  host log: ' + ' | '.join(l.split('coop: ')[-1] for l in how), flush=True)
        ck(how and all('everybody is ready' in l for l in how), '[editor] the vote ended because everybody was ready')
        for x in p.all:
            x.call('wait.frames 120 60000')
        for x in (p.host, bob, carol):
            done(x)
        ok = wait_until(lambda: all(p.stage(x) == stage0 for x in (p.host, bob, carol)), 60)
        ck(ok, f'[editor] the host, Bob and Carol are back in {stage0} after "done"')
        ok = p.wait_in(bob, 200, stable=3) and p.wait_in(carol, 200, stable=3)
        ck(ok, '[editor] Bob and Carol play the host\'s world again')
        ck(in_editor(dave), f'[editor] Dave is still in his editor ({dave.call("game.mode").strip()})')

        # 2. an action vote while Dave edits; Dave leaves
        n0 = actions_run(p)
        print('  host: ' + p.host.call('coop.vote action').strip().splitlines()[0], flush=True)
        time.sleep(2)
        for gp in (bob, carol):
            ck(p.vote_ready(gp), f'[leaver] {gp.name} clicks "I\'m ready"')
        time.sleep(3)
        rc = {k: v for k, v in ready_counts(p).items() if k != 'Dave'}
        want = '3/4'
        print(f'  ready as each window shows it: {rc} (want {want})', flush=True)
        ck(all(v == want for v in rc.values()), f'[leaver] the host\'s, Bob\'s and Carol\'s windows show ready {want}: {rc}')
        ck(p.host.call('coop.vote').startswith('vote active') and actions_run(p) == n0,
           '[leaver] the vote waits for Dave, who edits')
        off = log_size()
        dave.call('coop.leave')
        t1 = time.time()
        ck(wait_until(lambda: not p.host.call('coop.vote').startswith('vote active'), 10),
           f'[leaver] Dave left from his editor: the vote passes within 10 s ({time.time() - t1:.1f} s)')
        # the result may wait on the host at the busy gate after the vote ended (BUG-0404; lab
        # 20261101-210646: 0.9 s for the "game saved" box of the world share to Bob and Carol, BUG-0447)
        n = ran_once(p, n0, max(1.0, 10 - (time.time() - t1)))
        waits = [l.split('coop: ')[-1] for l in host_log_since(off).splitlines() if "vote's action" in l]
        print(f'  host log: {" | ".join(waits) or "the action did not wait"}', flush=True)
        ck(n == n0 + 1, f'[leaver] it ran once within 10 s of the leave ({n0} -> {n})')
        ck(dave.ok('game.mode'), f'[leaver] Dave\'s game runs ({dave.call("game.mode").strip()})')
        shot(dave.con, 'vote_net_4p_dave_editor')

        # 3. Dave's return
        if in_editor(dave):
            done(dave)
            wait_until(lambda: p.stage(dave) == stage0, 60)
        ck(p.stage(dave) == stage0, f'[return] Dave left his editor offline ({dave.call("game.mode").strip()})')
        ck(p.join(dave), '[return] Dave joins again')
        time.sleep(6)
        p.check_together(ck, 'return')
        p.check_partners(ck, 'return')
        if a.fixture == 'creature':
            species_view(p, ck, 'return')
        p.check_res(ck, 'return')

        # 4. a vote of all four
        n1 = actions_run(p)
        print('  host: ' + p.host.call('coop.vote action').strip().splitlines()[0], flush=True)
        time.sleep(2)
        for gp in p.guests:
            if os.environ.get('VOTE_PLANT') == 'noready' and gp is dave:  # a planted problem: a ready that never counts
                print('  PLANTED: Dave\'s "I\'m ready" is not clicked', flush=True)
                continue
            ck(p.vote_ready(gp), f'[all] {gp.name} clicks "I\'m ready"')
        ck(wait_until(lambda: not p.host.call('coop.vote').startswith('vote active'), 10), '[all] the vote passes')
        n = ran_once(p, n1)
        ck(n == n1 + 1, f'[all] it ran once ({n1} -> {n})')
        p.faults(ck)
        print(f'  ({time.time() - t0:.0f} s)', flush=True)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
