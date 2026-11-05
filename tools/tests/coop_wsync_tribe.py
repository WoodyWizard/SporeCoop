#!/usr/bin/env python3
"""Tribe: the host-owned world state reaches the guest's game (W1, mods/coop/wsync.cpp).

  python3 coop_wsync_tribe.py rel     BUG-0565: a village's feelings about the player tribe change in the host's
                                      game (the game's ApplyRelationship, as a raid's or a gift's event does); PASS:
                                      the guest's game computes the same relation (CalculateRelationship) for it.
  python3 coop_wsync_tribe.py goal    BUG-0564 (goal): the host's goal card moves to another village (the game's
                                      SetTargetTribe, what an attack order does); PASS: the guest's Tribe Mission
                                      Manager's current mission is on the same village, and the goal progress is equal.
  python3 coop_wsync_tribe.py stages  BUG-0577: the host's game has a stage done the guest's has not (cPlayer
                                      consequence trait); PASS: the guest's game has it, and again after the guest
                                      rejoins.
  python3 coop_wsync_tribe.py food    BUG-0580: an NPC village's food changes in the host's game (the game's
                                      AddFood: a gathering, then a spending); PASS: the guest's game has the same food
                                      for every village (its own bundles), within 10 s of each change.

Every check reads the guest's own game (`coop.wsync dump`), not the mod's table.

  SPORE_LANE=4 python3 tools/tests/coop_wsync_tribe.py rel
"""
import sys
import time

import lane
import twoinst
from twoinst import Checks, start
from wsync_util import dump, rel_pairs


def wait(pred, secs, step=1.0):
    t0 = time.time()
    while True:
        v = pred()
        if v or time.time() - t0 > secs:
            return v
        time.sleep(step)


def t_rel(host, guest, ck):
    h = dump(host, 'tribe_rel')
    ck(bool(h.get('tribe_rel')), f'the host\'s game has an NPC village ({h})')
    if not h.get('tribe_rel'):
        return
    e = h['tribe_rel'][0]
    g = dump(guest, 'tribe_rel')
    pairs = rel_pairs(h, g)
    print(f'  before: host {[(a["key"], a["rel"]) for a, _ in pairs]} guest {[(b and b["rel"]) for _, b in pairs]}',
          flush=True)
    print('  host: ' + host.call(f'coop.wsync rel {e["key"]} -7')['out'].strip(), flush=True)
    time.sleep(2)
    h = dump(host, 'tribe_rel')
    hv = [x for x in h['tribe_rel'] if x['key'] == e['key']][0]['rel']
    ck(hv < -5, f'the host\'s village is hostile now ({hv})')

    def same():
        p = [b for a, b in rel_pairs(dump(host, 'tribe_rel'), dump(guest, 'tribe_rel')) if a['key'] == e['key']]
        return p and p[0] and abs(p[0]['rel'] - hv) < 0.1 and p[0]
    got = wait(same, 10)
    g = dump(guest, 'tribe_rel')
    gv = [b for a, b in rel_pairs(h, g) if a['key'] == e['key']]
    print(f'  guest: {g.get("tribe_rel")}', flush=True)
    ck(bool(got), f'within 10 s the guest\'s game feels the host\'s relation for that village (host {hv}, guest '
       f'{gv[0] and gv[0]["rel"] if gv else None})')
    time.sleep(10)
    gv = [b for a, b in rel_pairs(h, dump(guest, 'tribe_rel')) if a['key'] == e['key']]
    ck(gv and gv[0] and abs(gv[0]['rel'] - hv) < 0.1, f'and 10 s later ({gv[0] and gv[0]["rel"] if gv else None})')


def t_food(host, guest, ck):
    def pairs():
        return rel_pairs(dump(host, 'tribe_food'), dump(guest, 'tribe_food'), 'tribe_food')

    p = pairs()
    ck(bool(p) and all(b for _, b in p), f'the host has NPC villages and the guest each of them ({p})')
    if not p or not all(b for _, b in p):
        return
    print(f'  before: {[(a["key"], a["food"], b["food"]) for a, b in p]}', flush=True)
    key = p[0][0]['key']
    for n in (37, -20):
        print('  host: ' + host.call(f'coop.wsync food {key} {n}')['out'].strip(), flush=True)
        time.sleep(1)
        hv = [a['food'] for a, _ in pairs() if a['key'] == key][0]

        def same():
            q = pairs()
            return all(b and abs(a['food'] - b['food']) <= 1 for a, b in q) and q
        got = wait(same, 10)
        q = pairs()
        print(f'  after {n:+d} on the host: {[(a["key"], a["food"], b and b["food"]) for a, b in q]}', flush=True)
        ck(bool(got), f'within 10 s every village of the guest\'s game has the host\'s food (village {key}: host {hv}, '
           f'guest {[b and b["food"] for a, b in q if a["key"] == key]})')
    time.sleep(10)
    q = pairs()
    ck(all(b and abs(a['food'] - b['food']) <= 1.5 for a, b in q),
       f'and 10 s later ({[(a["food"], b and b["food"]) for a, b in q]})')


def t_goal(host, guest, ck):
    h = dump(host)
    rel = h.get('tribe_rel', [])
    ck(bool(rel) and bool(h.get('tribe_goal')), f'the host has a village and a mission manager ({h.get("tribe_goal")})')
    if not rel or not h.get('tribe_goal'):
        return
    target = rel[0]
    print(f'  before: host goal {h["tribe_goal"][0]["raw"]}; guest {dump(guest, "tribe_goal").get("tribe_goal")}',
          flush=True)
    print('  host: ' + host.call(f'coop.wsync goal {target["key"]}')['out'].strip(), flush=True)
    time.sleep(2)
    hg = dump(host, 'tribe_goal')['tribe_goal'][0]
    ck(hg['key'] == target['key'], f'the host\'s goal card is on that village ({hg["raw"]})')

    def same():
        gg = dump(guest, 'tribe_goal').get('tribe_goal')
        return gg and gg[0]['key'] != 'FFFFFFFF' and \
            sum((a - b) ** 2 for a, b in zip(gg[0]['pos'], hg['pos'])) < 40 ** 2 and gg[0]
    gg = wait(same, 10)
    last = dump(guest, 'tribe_goal').get('tribe_goal')
    print(f'  guest goal: {last and last[0]["raw"]}', flush=True)
    ck(bool(gg), f'within 10 s the guest\'s goal card is on the host\'s village ({last and last[0]["raw"]})')
    ck(last and 'card=1' in last[0]['raw'], f'the guest\'s HUD shows that mission\'s card ({last and last[0]["raw"]})')
    ck(last and abs(last[0]['progress'][0] - hg['progress'][0]) < 0.01 and
       abs(last[0]['progress'][1] - hg['progress'][1]) < 0.01,
       f'the guest\'s goal progress is the host\'s ({hg["progress"]} / {last and last[0]["progress"]})')


def t_stages(host, guest, ck):
    h = dump(host, 'stages')
    ck(bool(h.get('stages')), 'the host has a cPlayer')
    if not h.get('stages'):
        return
    print(f'  before: host {h["stages"][0]["traits"]} guest {dump(guest, "stages").get("stages")}', flush=True)
    # a played Cell and Creature stage: their consequence trait ids (any non-zero id marks the stage done)
    print('  host: ' + host.call('coop.wsync stage 0 0BB2D2A7')['out'].strip(), flush=True)
    print('  host: ' + host.call('coop.wsync stage 1 0D6F2C7B')['out'].strip(), flush=True)
    want = dump(host, 'stages')['stages'][0]['traits']

    def same(con):
        g = dump(con, 'stages').get('stages')
        return g and g[0]['traits'] == want
    ck(wait(lambda: same(guest), 10), f'within 10 s the guest\'s game has the host\'s stages done ({want} / '
       f'{dump(guest, "stages").get("stages")})')
    print('  guest rejoins', flush=True)
    print('  guest: ' + guest.call('coop.leave')['out'].strip()[:80], flush=True)
    time.sleep(2)
    guest.call(lane.JOIN)
    ck(twoinst.wait_guest_in(host, guest, 120), 'the guest is back in the host\'s world after its rejoin')
    got = wait(lambda: same(guest), 30)
    print(f'  after the rejoin: guest {dump(guest, "stages").get("stages")}', flush=True)
    ck(bool(got), 'after its rejoin the guest\'s game has the host\'s stages done again')


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else 'rel'
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(6)
    {'rel': t_rel, 'goal': t_goal, 'stages': t_stages, 'food': t_food}[what](host, guest, ck)
    print('  guest wsync: ' + guest.call('coop.wsync')['out'].strip().replace('\n', ' | '), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
