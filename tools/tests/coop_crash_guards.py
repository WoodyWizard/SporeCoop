#!/usr/bin/env python3
"""Two crashes of 0.7.6 / 0.7.7 and their guards.

  --case model_fx  (BUG-0575, Tribe, the guest; the crash was not reproduced: what freed the part is not known) a
                   guest crashed at 0xA826F3 after the home camera jump: the camera range check of a model's attached
                   effects (SpatialEffects_UpdateByDistance 0xC8ABF0) started an effect whose component was freed.
                   `coop.fxcut` makes that state on the next model with a running attached effect (its threshold 0 for
                   a moment: the game stops it; then its component vector points at one freed component, a vtable of
                   nulls, and the threshold is back: the range check starts it, as the camera jump did).
                   PASS: the guard dropped it, no fault, the guest keeps drawing.
                   SPORE_FIX_OFF=game.model_fx fails (a call through null in VisualEffect_Start).
  --case fruit_pool (BUG-0584, Creature, the host; reproduced) the host eats a fruit of the save the way a guest's
                   "took fruit" request does (`coop.wobj eat`, worldobj's fruit_eat), then a fruit falls from a tree
                   (`coop.wobj drop`, the flora's DropFruit). Without the fixes the eaten fruit lies in the plants'
                   free fruit with one reference and the fall's take frees it: a call through null at 0xB097C2.
                   PASS: no fault, the fruit fell. SPORE_FIX_OFF=wobj.fruit_saved,game.fruit_pool fails; either
                   switch alone passes (wobj.fruit_saved: the fruit is destroyed as the game's eating does;
                   game.fruit_pool: the take does not get it).

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


def fruit_pool(host, guest, ck):
    out = pool(host)
    saved = set(re.findall(r'([0-9A-F]{8})/r\d+/f[13579BDF]/s2', out.split('loose', 1)[-1]))
    fr = [o for o in objs(host, 'fruit', 80) if o['st'] == 2 and o['p'].upper().zfill(8) in saved]
    print('  host fruit of the save on the ground:', [(o['id'], o['p'], o['pos']) for o in fr][:6], flush=True)
    if not ck(fr, 'the host has a fruit of the save on the ground (+0x148 bit 0)'):
        return
    # the guest's "took fruit" request does this on the host (worldobj on_act -> fruit_eat)
    print('  host:', host.call(f'coop.wobj eat {fr[0]["p"]}')['out'].strip(), f'(fruit {fr[0]["p"]})', flush=True)
    time.sleep(2)
    out = pool(host)
    free = out.split('loose', 1)[0]
    m = re.search(fr[0]['p'].upper().zfill(8) + r'/r(\d+)/f[13579BDF]/s6', free)
    # the take's pop frees a free fruit with one reference left; the eaten fruit's other references vary (1 or 3 in
    # the lab), so the state is checked, not only the fault
    ck(not m, 'the eaten fruit of the save is not in the plants\' free fruit (where the next falling fruit is taken'
              + (f'; it is, with {m.group(1)} reference(s))' if m else ')'))
    trees = [o for o in objs(host, 'tree', 120) if o['st']]
    print('  host trees with fruit (mask):', [(o['p'], hex(o['st'])) for o in trees][:6], flush=True)
    if not ck(trees, 'the host has a tree with fruit near its avatar'):
        return
    t = trees[0]
    slot = next(s for s in range(64) if t['st'] >> s & 1)
    print('  host:', host.call(f'coop.wobj drop {t["p"]} {slot}')['out'].strip(), f'(slot {slot})', flush=True)
    time.sleep(2)
    pool(host)
    taken = status_num(host, r'freed fruit taken out of the pool (\d+)')
    print(f'  host: fruit taken out of the pool by the guard {taken}, faults {faults(host)}', flush=True)
    after = [o for o in objs(host, 'tree', 120) if o['p'] == t['p']]
    ck(after and not (after[0]['st'] >> slot & 1), 'the fruit fell from the tree (its slot is empty)')
    ck(frames_go_on(host), 'the host keeps drawing')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('model_fx', 'fruit_pool'), required=True)
    a = ap.parse_args()
    host, guest = start('tribe' if a.case == 'model_fx' else 'creature')
    ck = Checks()
    time.sleep(8)
    if a.case == 'model_fx':
        model_fx(host, guest, ck)
    else:
        fruit_pool(host, guest, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
