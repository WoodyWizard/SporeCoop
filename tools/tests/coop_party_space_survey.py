#!/usr/bin/env python3
"""BUG-0830: a survey contract with three players, one of them joining the crew late and staying home.

Alice (host) offers a survey of one star; Alice and Bob accept (two ships needed: active). Carol accepts too while
it runs and stays in the home system. Alice and Bob jump to the target: the survey must be done (on every machine)
and pay its reward into the game money of every machine.

Before the fix a target counted only with every crew member at the star: Carol at home kept it at 0/1 until the
contract timed out and nobody was paid. SPORE_FIX_OFF=space.survey_needed: FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_party_space_survey.py
"""
import re
import time

from party import Party
from twoinst import Checks


def sp(p, *a):
    return p.call('coop.space ' + ' '.join(str(x) for x in a)).strip()


def cst(x):
    return dict(re.findall(r'(\w+)=(\S+)', sp(x, 'contracts')))


def cmoney(x):
    m = re.search(r'money (-?\d+)', sp(x, 'contracts money'))
    return int(m.group(1)) if m else -1


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def main():
    p = Party.start('space', players=3)
    try:
        return run(p)
    finally:
        p.stop()


def run(p):
    ck = Checks()
    alice, bob, carol = p.byname('Alice'), p.byname('Bob'), p.byname('Carol')
    ck(len(p.together()) >= 2, f'three players in the host\'s Space world ({p.together()})')
    sp(alice, 'contracts period 0')
    sp(alice, 'contracts limit 150')
    for x in p.all:
        print(f'  {x.name} drive:', sp(x, 'tool add interstellardrive'), flush=True)
    time.sleep(3)
    print(' ', sp(alice, 'contracts offer survey 1'), flush=True)
    targets = re.findall(r'target (\d+) \S+ (.+?) done', sp(alice, 'contracts targets'))
    reward = int(cst(alice).get('money', 0))
    ck(len(targets) == 1, f'a survey of one star ({targets}), reward {reward}')
    if not targets:
        return ck.result()
    star = targets[0][0]
    for x in (alice, bob):
        print(f'  {x.name}:', sp(x, 'contracts accept'), flush=True)
    ck(wait(lambda: cst(alice).get('state') == 'active', 10), f'Alice and Bob make the crew: active ({cst(alice)})')
    print('  Carol (late):', sp(carol, 'contracts accept'), flush=True)
    ck(wait(lambda: len(cst(alice).get('crew', '').split(',')) == 3, 10), f'Carol is in the crew too ({cst(alice).get("crew")})')
    m0 = {x.name: cmoney(x) for x in p.all}
    for x in (alice, bob):
        print(f'  {x.name} jumpto {star}:', sp(x, 'jumpto', star), flush=True)
    ok = wait(lambda: all(cst(x).get('state') == 'done' for x in p.all), 90, 1)
    print('  states:', [cst(x).get('state') for x in p.all], '| host:', sp(alice, 'contracts').splitlines()[0], flush=True)
    ck(ok, f'the survey is done on every machine with Carol at home ({[cst(x).get("state") for x in p.all]})')
    ok = wait(lambda: all(cmoney(x) == m0[x.name] + reward for x in p.all), 20)
    ck(ok, f'the reward arrived once in every game (+{reward}: { {x.name: cmoney(x) - m0[x.name] for x in p.all} })')
    p.faults(ck)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
