#!/usr/bin/env python3
"""Two crash guards of 0.7.8, each with the state of its crash made on purpose (the crashes themselves were not
reproduced: who freed the object is not known).

  --case model_fx  (BUG-0575, Tribe, the guest) a guest crashed at 0xA826F3 after the home camera jump: the camera
                   range check of a creature's attached effects (SpatialEffects_UpdateByDistance 0xC8ABF0) started
                   an effect whose component was freed. `coop.fxcut` stops a running attached effect of one of the
                   guest's creatures (its threshold 0 for a moment), points its component vector at one freed
                   component (a vtable of nulls) and puts the threshold back: the next range check starts it, as
                   the camera jump did. PASS: the guard dropped the broken effect (coop.status), no fault, the
                   guest keeps drawing. SPORE_FIX_OFF=game.model_fx fails (a call through null in VisualEffect_Start).
  --case fruit_pool (BUG-0584, Creature, the host) a host faulted at 0xB097C2 (a call through null): a fruit that
                   falls took a freed fruit from the plants' fruit pool. The host eats a fruit (it goes to the free
                   vector), `coop.fruitcut` makes the last free fruit freed memory, a fruit falls from a tree
                   (`coop.wobj drop`, the flora's DropFruit). PASS: the guard took the freed fruit out, no fault,
                   the fruit still fell (the tree's mask). SPORE_FIX_OFF=game.fruit_pool fails.

  coop_crash_guards.py --case model_fx|fruit_pool
"""
import argparse
import re
import time

import twoinst
from twoinst import Checks, start
from coop_world_objects import objs, wait


def status_num(con, pat):
    m = re.search(pat, con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def frames_go_on(con, secs=3):
    f0 = twoinst.host_frame(con)
    time.sleep(secs)
    return twoinst.host_frame(con) > f0 + 10


def fx_tests(con):
    m = re.search(r'broken model effects dropped (\d+) \(tests: planted (\d+), dropped (\d+)\)', con.call('coop.status')['out'])
    return tuple(map(int, m.groups())) if m else None


def model_fx(host, guest, ck):
    print('  guest:', guest.call('coop.fxcut')['out'].strip(), flush=True)
    t0 = time.time()
    st = None
    while time.time() - t0 < 25:
        st = fx_tests(guest)
        if (st and st[1] > 0 and (st[2] > 0 or time.time() - t0 > 20)) or faults(guest) > 0:
            break
        time.sleep(1)
    time.sleep(2)  # the game's next range checks of that model
    st = fx_tests(guest)
    print(f'  guest model effect guard (dropped, test planted, test dropped): {st}, faults {faults(guest)}', flush=True)
    if not ck(st and st[1] > 0, 'a model of the guest\'s game had a running attached effect: it was stopped and got a freed part (the test state)'):
        return
    ck(st[2] > 0, f'the guard dropped the effect with the freed part before the game started it ({st})')
    ck(frames_go_on(guest), 'the guest keeps drawing')


def pool(con):
    out = con.call('coop.fruitpool')['out'].strip()
    print('  ' + out.replace('\n', ' | ')[:700], flush=True)
    return out


def fruit_pool(host, guest, ck, plant):
    fr = [o for o in objs(host, 'fruit', 80) if o['st'] == 2]
    print('  host fruit on the ground:', [(o['id'], o['pos']) for o in fr][:6], flush=True)
    pool(host)
    if not ck(fr, 'the host has a fruit on the ground'):
        return
    # the guest's "took fruit" request does this on the host (worldobj on_act -> cFruit_SetState 5)
    print('  host:', host.call(f'coop.wobj eat {fr[0]["p"]}')['out'].strip(), f'(fruit {fr[0]["p"]})', flush=True)
    time.sleep(2)
    pool(host)
    trees = [o for o in objs(host, 'tree', 120) if o['st']]
    print('  host trees with fruit (mask):', [(o['p'], hex(o['st'])) for o in trees][:6], flush=True)
    if not ck(trees, 'the host has a tree with fruit near its avatar'):
        return
    if plant:
        r = host.call('coop.fruitcut')['out'].strip()
        print('  host:', r, flush=True)
        if not ck(r.startswith('armed'), 'the host\'s fruit pool has a freed fruit now (the test state is made)'):
            return
    t = trees[0]
    slot = next(s for s in range(64) if t['st'] >> s & 1)
    print('  host:', host.call(f'coop.wobj drop {t["p"]} {slot}')['out'].strip(), f'(slot {slot})', flush=True)
    time.sleep(2)
    pool(host)
    taken = status_num(host, r'freed fruit taken out of the pool (\d+)')
    print(f'  host: fruit taken out of the pool by the guard {taken}, faults {faults(host)}', flush=True)
    if plant:
        ck(taken > 0, f'the guard took the freed fruit out of the pool before the fall took it ({taken})')
    after = [o for o in objs(host, 'tree', 120) if o['p'] == t['p']]
    ck(after and not (after[0]['st'] >> slot & 1), 'the fruit fell from the tree (its slot is empty)')
    ck(frames_go_on(host), 'the host keeps drawing')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('model_fx', 'fruit_pool'), required=True)
    ap.add_argument('--plant', action='store_true', help='fruit_pool: a freed fruit is planted as the last free one')
    a = ap.parse_args()
    host, guest = start('tribe' if a.case == 'model_fx' else 'creature')
    ck = Checks()
    time.sleep(8)
    if a.case == 'model_fx':
        model_fx(host, guest, ck)
    else:
        fruit_pool(host, guest, ck, a.plant)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
