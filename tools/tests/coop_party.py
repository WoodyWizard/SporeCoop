#!/usr/bin/env python3
"""Three or four players per stage (BUG-0152, GitHub #131): the paths between guests, checked in
every machine's game. Minutes per stage, not the campaign's hours (tools/tests/party.py).

1. join     the host plays the fixture, Bob joins (twoinst.start), Carol (and Dave) join at the same
            moment: the world goes to two guests at once (#91). Every guest plays the host's stage
            with an avatar and is together with the host.
2. see      each guest's game shows every other player, the other guests too, where that player's
            own game has it (coop.sync of the viewer against the subject's own avatar). Shared
            food / money is the same on every machine.
3. vote     the host opens an action vote (`coop.vote action`); all guests but the last click "I'm
            ready" in their co-op window: every machine's window shows the same "ready k/N" and the
            vote waits. The last guest leaves: the vote passes (everybody present is ready), it ran
            once on the host.
4. rejoin   the last guest joins again: it plays the host's world, sees the others where they are,
            the others see it.
5. away     the host goes to the main menu (`coop.menu`): every guest knows (host_away=1) and stays
            in its stage; the host loads its world again: every guest is back with it, sees the
            others, the shared resources match.
Afterwards no machine logged a fault (the lab also fails on crash reports and dead games).

  SPORE_LANE=6 python3 tools/tests/coop_party.py --fixture creature --players 4
"""
import argparse
import re
import time

from party import Party
from twoinst import Checks


def actions_run(p):
    m = re.search(r'actions run (\d+)', p.host.call('coop.vote'))
    return int(m.group(1)) if m else -1


def ready_counts(p):
    """{name: 'k/N'} as each machine's co-op window shows the vote"""
    out = {}
    for x in p.all:
        m = re.search(r'ready (\d+/\d+)', p.vote_line(x))
        out[x.name] = m.group(1) if m else None
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='creature')
    ap.add_argument('--players', type=int, default=4)
    ap.add_argument('--one-by-one', action='store_true', help='the extra guests join one after the other')
    ap.add_argument('--skip', default='', help='steps to leave out: vote,rejoin,away')
    a = ap.parse_args()
    skip = set(filter(None, a.skip.split(',')))
    ck = Checks()
    t0 = time.time()
    p = Party.start(a.fixture, a.players, join_together=not a.one_by_one)
    try:
        print(f'  players {p.ids()} in {time.time() - t0:.0f} s', flush=True)
        if not p.check_together(ck, 'join'):
            return ck.result()
        time.sleep(5)
        p.check_partners(ck, 'join')
        p.check_res(ck, 'join')
        last = p.guests[-1]

        if 'vote' not in skip:
            n0 = actions_run(p)
            print('  host:', p.host.call('coop.vote action').splitlines()[0], flush=True)
            time.sleep(2)
            for gp in p.guests[:-1]:
                ck(p.vote_ready(gp), f'[vote] {gp.name} clicks "I\'m ready"')
            time.sleep(2)
            rc = ready_counts(p)
            want = f'{len(p.guests)}/{len(p.all)}'
            print(f'  ready as each window shows it: {rc} (want {want})', flush=True)
            ck(all(v == want for v in rc.values()), f'[vote] every machine shows ready {want}: {rc}')
            ck(p.host.call('coop.vote').startswith('vote active'), f'[vote] the vote waits for {last.name}')
            ck(p.leave(last), f'[vote] {last.name} leaves')
            t1 = time.time()
            while time.time() - t1 < 10 and p.host.call('coop.vote').startswith('vote active'):
                time.sleep(0.5)
            ck(not p.host.call('coop.vote').startswith('vote active'), '[vote] the vote ends when the one not ready left')
            ck(actions_run(p) == n0 + 1, f'[vote] it ran once ({n0} -> {actions_run(p)})')
            if 'rejoin' in skip:
                ck(p.join(last), f'[vote] {last.name} joins again')

        if 'rejoin' not in skip:
            if 'vote' in skip:
                ck(p.leave(last), f'[rejoin] {last.name} leaves')
                time.sleep(3)
            t1 = time.time()
            ok = p.join(last)
            print(f'  {last.name} back after {time.time() - t1:.0f} s: {ok} ({p.log[-1] if p.log else ""})', flush=True)
            ck(ok, f'[rejoin] {last.name} plays the host\'s world again')
            time.sleep(5)
            p.check_together(ck, 'rejoin')
            p.check_partners(ck, 'rejoin')
            p.check_res(ck, 'rejoin')

        if 'away' not in skip:
            ck(p.host_away(), '[away] every guest knows the host is in the menu')
            time.sleep(5)
            stages = {gp.name: p.stage(gp) for gp in p.guests}
            ck(all(s == a.fixture or (a.fixture == 'civ_npc' and s == 'civ') for s in stages.values()),
               f'[away] every guest stays in its stage: {stages}')
            if len(p.guests) > 1:
                g1, g2 = p.guests[0], p.guests[1]
                d = p.partner_view(g1, g2)
                ck(d is not None, f'[away] {g1.name}\'s game still shows {g2.name} ({d})')
            ck(p.host_back(), '[away] the host is back and every guest with it')
            time.sleep(5)
            p.check_together(ck, 'back')
            p.check_partners(ck, 'back')
            p.check_res(ck, 'back')
        p.faults(ck)
        print(f'  ({time.time() - t0:.0f} s)', flush=True)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
