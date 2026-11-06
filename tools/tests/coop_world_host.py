#!/usr/bin/env python3
"""World objects that exist differently in the two games: the host's game is the truth (0.7.9 L2, mods/coop/worldobj.cpp,
docs/re/creature.md#creature-host-places). Every case asserts the game's own object list on both machines (exists,
position), not the mod's bookkeeping:

  --case fruit      (BUG-0497, creature) a fruit falls from a tree in the host's game where the guest's tree slot is
                    already empty (its own fall before: the slot's bit cleared in the guest's game only), and a fruit
                    the guest's game rolled elsewhere (moved 9 m in the guest's game only): both lie at the host's
                    places in the guest's game (within 1.5 m). SPORE_FIX_OFF=wobj.fruit_place fails.

  coop_world_host.py --case fruit
"""
import argparse
import math
import time

from coop_world_objects import near, objs, pairs, wait
from twoinst import Checks, start


def fruit_at(con, pos, d=1.5):
    return [f for f in near(objs(con, 'fruit', 4, pos), pos, d) if f['st'] in (1, 2)]


def fruit(host, guest, ck):
    # 1. a slot that fell in the guest's game before: the host's fruit of it has no copy there
    ps = [(h, g) for h, g in pairs(host, guest, 'tree', 0.3) if h['st'] == g['st'] and bin(h['st']).count('1') >= 2]
    print('  trees with fruit on both games (host mask, guest mask, pos):', [(h['st'], g['st'], h['pos']) for h, g in ps][:4],
          flush=True)
    if not ck(ps, 'a tree with fruit, the same fruit left on both games'):
        return
    h, g = ps[0]
    slot = min(i for i in range(32) if h['st'] >> i & 1)
    print('  guest:', guest.call(f'coop.wobj mask {g["p"]} {g["st"] & ~(1 << slot):X}')['out'].strip(),
          f'(slot {slot} fell in the guest\'s game before)', flush=True)
    time.sleep(3)
    before = {f['id'] for f in objs(host, 'fruit', 30, h['pos'])}
    print('  host:', host.call(f'coop.wobj drop {h["p"]} {slot}')['out'].strip(), f'(slot {slot} of the tree at {h["pos"]})',
          flush=True)
    time.sleep(4)  # it falls and comes to rest
    new = [f for f in objs(host, 'fruit', 30, h['pos']) if f['id'] not in before and f['st'] in (1, 2)]
    if not ck(new, 'the host\'s fruit fell (the host\'s game)'):
        return
    hf = new[0]
    for _ in range(3):  # where it rests
        time.sleep(2)
        hf = next((f for f in objs(host, 'fruit', 30, h['pos']) if f['id'] == hf['id']), hf)
    got = wait(lambda: fruit_at(guest, hf['pos']), 12)
    print(f'  host fruit at {hf["pos"]}; guest fruit there: {got}', flush=True)
    ck(got, 'the host\'s fruit of a slot that fell in the guest\'s game before lies at its place in the guest\'s game')
    # 2. a fruit the guest's game rolled elsewhere
    ps = [(h, g) for h, g in pairs(host, guest, 'fruit', 1.0, 60) if h['st'] == 2 and g['st'] == 2
          and math.dist(h['pos'], hf['pos']) > 3]
    print('  fruit on the ground on both games:', [(h['id'], g['id'], h['pos']) for h, g in ps][:4], flush=True)
    if not ck(ps, 'a fruit on the ground at the same place on both games'):
        return
    h2, g2 = ps[0]
    away = (g2['pos'][0] + 6, g2['pos'][1], g2['pos'][2] + 6)
    print('  guest:', guest.call('coop.wobj move %s %.2f %.2f %.2f' % ((g2['p'],) + away))['out'].strip(),
          '(the guest\'s fruit rolled 8.5 m away)', flush=True)
    time.sleep(8)
    hf2 = next((f for f in objs(host, 'fruit', 6, h2['pos']) if f['id'] == h2['id']), None)
    if not ck(hf2, 'the host\'s fruit is still there (the host\'s game)'):
        return
    got = fruit_at(guest, hf2['pos'])
    extra = [f for f in near(objs(guest, 'fruit', 6, away), away, 1.5)]
    print(f'  guest fruit at the host\'s place: {got}; at the rolled place: {extra}', flush=True)
    ck(got, 'the fruit lies at the host\'s place in the guest\'s game')
    ck(not extra, 'no fruit of the guest\'s own at the place it rolled to')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('fruit',), required=True)
    a = ap.parse_args()
    host, guest = start({'fruit': 'creature'}[a.case])
    ck = Checks()
    time.sleep(6)  # the first snapshots
    {'fruit': fruit}[a.case](host, guest, ck)
    print('  guest:', guest.call('coop.wobj')['out'].strip().replace('\n', ' | '), flush=True)
    print('  host:', host.call('coop.wobj')['out'].strip().replace('\n', ' | '), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
