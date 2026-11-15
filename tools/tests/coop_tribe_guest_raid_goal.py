#!/usr/bin/env python3
"""Tribe: a village only the guest raided counts for the stage's goal on the host (BUG-1068).

The goal bar counts a village when its cTribeToTribeMission ends; the host's game makes that mission
(TribeMission_SetTargetTribe) from its own raid and gift clicks only. A guest's order ran on the host through
DoAction without it: the village fell (hut down, conquered flag) and the bar stayed where it was.

1. Two games on the tribe fixture. Only the guest's members raid the hut of the fixture's NPC village (the one
   `coop.wsync dump tribe_rel` lists, not the host's goal): the guest selects them and orders CitizenAction 15
   through its own game's order path (`coop.tribe act_obj`: DoActionForObjects, which a guest sends to the host as
   MSG_TRIBE_CMD). Once they are at it the hut's health is cut to 2 (`coop.tribe huthp`), the game's own blows end it.
   Within 30 s of the fall the host's goal progress (cPlayer +0x10F0, `tribe_goal progress=`) is higher than before.
2. A gift (CitizenAction 9, the same path) on a village the host forms (`coop.tribe npc_tribe`): the host's goal card
   (`tribe_goal` target) goes onto it, as for the host's own gift click.
SPORE_FIX_OFF=tribe.guest_mission: the bar stays 0 after the fall and the card does not move: FAIL.
(A village formed during play counted its fall with the fix off: lab 20261113-072313, so the raid uses the
fixture's own village, as in the ticket.)
"""
import re
import time

from coop_tribe_conquer_orders import HUT, huts
from coop_tribe_gather_stall import pos_of
from coop_tribe_tools import dist, members
from play_tribe import frame_point
from twoinst import Checks, keyframe, start
from wsync_util import dump

TRIBE = '18C6D19'


def goal(con):
    g = dump(con, 'tribe_goal').get('tribe_goal')
    return g[0] if g else None


def order(guest, ids, action, noun, p):
    for mid in ids:
        guest.call(f'coop.tribe select {mid} 1')
    out = guest.call('coop.tribe act_obj %d %s %.2f %.2f %.2f' % ((action, noun) + tuple(p)))['out'].strip()
    print(f'  guest order {action} on {noun}: {out}', flush=True)
    return out


def host_line(host):
    m = re.search(r'^tribe\(host\):.*$', host.call('coop.status')['out'], re.M)
    return m.group(0) if m else '-'


def raid(host, guest, own, hp):
    """the guest's members raid the hut at hp until it falls in the host's game: seconds, or None"""
    order(guest, own, 15, HUT, hp)
    t0, cut, last_hp, last_change = time.time(), False, None, time.time() + 30  # the walk there
    while time.time() - t0 < 240:
        time.sleep(2)
        hs = host.call('coop.tribe huthp %.2f %.2f %.2f -' % hp)['out'].strip()
        mm = re.search(r'exact=([-\d.]+) state=(\d+)', hs)
        h, state = (float(mm.group(1)), int(mm.group(2))) if mm else (None, None)
        nearby = [mid for mid in own if pos_of(host, mid) and dist(pos_of(host, mid), hp) < 14]
        print(f'  {time.time() - t0:4.0f} s: {hs}; raiders near {nearby}', flush=True)
        if hs.startswith('no hut') or state == 2:
            return time.time() - t0
        if h is not None and (last_hp is None or h < last_hp - 0.01):
            last_change = time.time()  # a blow
        last_hp = h
        if time.time() - last_change > 15:
            # raiders that left a standing hut, or stand at it without a blow, are sent again (BUG-0761)
            print('  raider: ' + host.call(f'coop.tribe ai {own[0]}')['out'].strip()[:200], flush=True)
            order(guest, own, 15, HUT, hp)
            last_change = time.time()
        if cut and h is not None and h > 10:
            cut = False  # the village mended it
        if not cut and len(nearby) >= max(1, len(own) // 2):
            print('  cut: ' + host.call('coop.tribe huthp %.2f %.2f %.2f 2' % hp)['out'].strip(), flush=True)
            cut = True
    return None


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    mem = members(guest)
    own = sorted(i for i, o in mem.items() if o != 0)[:3]
    if not ck(bool(own), f'the guest has members ({own})'):
        return ck.result()
    g0 = goal(host)
    print(f'  host goal at the start: {g0 and g0["raw"]}', flush=True)

    # 1. the fixture's NPC village, raided by the guest's members only
    home = pos_of(host, own[0])
    vil = [e for e in dump(host, 'tribe_rel').get('tribe_rel', []) if not g0 or e['key'] != g0.get('key')]
    vil.sort(key=lambda e: dist(e['pos'], home) if home else 0)
    if not ck(bool(vil), f'an NPC village other than the goal ({len(vil)})'):
        return ck.result()
    v = vil[0]
    hh = sorted((dist(p, v['pos']), h, p) for h, p in huts(host))
    if not ck(bool(hh) and hh[0][0] < 40, f"the village {v['key']} at {v['pos']} has a hut ({hh[:1]})"):
        return ck.result()
    _, hut, hp = hh[0]
    pbefore = goal(host)
    pbefore = pbefore['progress'][0] if pbefore else 0.0
    print(f'  hut at {hp}, {dist(hp, home) if home else -1:.0f} m from the raiders; host goal progress before {pbefore}',
          flush=True)
    fell = raid(host, guest, own, hp)
    print(f'  host: {host_line(host)}', flush=True)
    if not ck(fell is not None, f"the guest's raid ended the hut in the host's game ({fell and round(fell)} s)"):
        ck.faults(host=host, guest=guest)
        return ck.result()
    t0, g2 = time.time(), None
    while time.time() - t0 < 30:
        g2 = goal(host)
        print(f'  {time.time() - t0:4.0f} s after the fall: host goal {g2 and g2["raw"]}', flush=True)
        if g2 and g2['progress'][0] > pbefore + 0.5:
            break
        time.sleep(2)
    ck(g2 and g2['progress'][0] > pbefore + 0.5,
       f"the village only the guest raided counted for the host's goal ({pbefore} -> {g2 and g2['progress'][0]})")
    gg = goal(guest)
    print(f'  guest goal: {gg and gg["raw"]} (wsync.tribe_goal: the host\'s, a row cycle later)', flush=True)
    frame_point({'host': host, 'guest': guest}, hp)
    keyframe('conquered', "the fixture's village the guest's members conquered, on both screens; the goal bar")

    # 2. a gift on a village the host forms
    out = host.call('coop.tribe npc_tribe 3 45')['out'].strip()
    print('  host: ' + out, flush=True)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    if ck(bool(m), 'the host formed an NPC tribe'):
        tp = tuple(float(m.group(i)) for i in (1, 2, 3))
        time.sleep(3)
        order(guest, own, 9, TRIBE, tp)
        t0, g1 = time.time(), None
        while time.time() - t0 < 15:
            time.sleep(1)
            g1 = goal(host)
            if g1 and dist(g1['pos'], tp) < 20:
                break
        print(f'  host after the gift: {g1 and g1["raw"]} | {host_line(host)}', flush=True)
        ck(g1 and dist(g1['pos'], tp) < 20, f"the guest's gift moved the host's goal card onto that village ({tp})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
