#!/usr/bin/env python3
"""Tribe: the guest's goal card is on a village the host's reloaded world no longer has (BUG-0942).

Found by the whole campaign of four players (lab 20261111-113158 of fix/w47-final12b): the host loaded its Tribe
world again, a guest stayed in its game and re-aligned (three villages "not in the host's world: removed here",
BUG-0564). The guest's goal card (the Tribe Mission Manager's current cTribeToTribeMission) was on one of those
villages. The game never destroys a tribe in the Tribe stage, so the mission kept the destroyed tribe (no hut, no
members), and the next refresh of the card's text (0xC30000: `tribe->hut->combatant state`) read through the null hut:
813 caught faults, one per frame, `0xC0000005 at 0x008E8230 (read 0x154)`, a 2 s hitch and the "SporeCoop caught a
problem" report.

1. Tribe fixture. The host's world gets a new NPC tribe its last save does not have (`coop.tribe form`), and the
   host's goal card goes onto it (`coop.wsync goal`, the game's SetTargetTribe: what an attack order or a gift does).
   The guest's goal card follows (wsync.tribe_goal).
2. The host knocks out its own player tribe, OK on the game-over caption: its game reloads the last save, without
   the new tribe. The guest's realign removes that village.
3. A goal event in the guest's game (`coop.wsync goal dirty`: the manager's own "refresh the cards" call, vt 0x28,
   what every relation change, order or mission event of the game ends with).
4. PASS: the guest's game caught no fault in the 20 s after, its HUD shows no card of the removed village (its
   current mission is none or on a village of the host's world), frames go on.
   `SPORE_FIX_OFF=realign.village_goal` fails (the fault storm).
5. The same for an event of the removed village's own pol (`coop.wsync goal dirty <pol>`: the manager asks the
   map's mission of that pol for its text at once): no fault. `BOUNCE_GOAL_PLANT=nohook` (the fix off after the
   card went) fails here.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_bounce_goal.py
"""
import math
import os
import re
import time

from coop_tribe_bounce_world import bounce_ok, diff
from coop_tribe_gameover import frames_go_on, player_members
from coop_tribe_guest_tribes import show, tribes
from twoinst import Checks, start
from wsync_util import dump


def fault_count(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def goal(con):
    g = dump(con, 'tribe_goal').get('tribe_goal')
    return g[0] if g else None


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(6)
    before = {e['key'] for e in dump(host, 'tribe_rel').get('tribe_rel', [])}
    # (tribe.form_pace holds the third tribe until the party's food is over the threshold times the players)
    print('  host: ' + host.call('coop.res add food 300')['out'].strip(), flush=True)
    time.sleep(3)
    print('  host: ' + host.call('coop.tribe form')['out'].strip(), flush=True)
    time.sleep(10)
    h0, g0 = tribes(host), tribes(guest)
    lack, extra = diff(h0, g0)
    ck(not lack and not extra, f'before the game over the guest has the host\'s tribes (lacks {show(lack)}, '
       f'extra {show(extra)})')
    new = [e for e in dump(host, 'tribe_rel').get('tribe_rel', []) if e['key'] not in before]
    ck(bool(new), f'the host\'s world has the village formed after its save ({[e["key"] for e in new]})')
    if not new:
        return ck.result()
    print('  host: ' + host.call(f'coop.wsync goal {new[0]["key"]}')['out'].strip(), flush=True)
    gg, t0 = None, time.time()
    while time.time() - t0 < 15:
        gg = goal(guest)
        if gg and gg.get('key') != 'FFFFFFFF' and math.dist(gg['pos'], new[0]['pos']) < 40:
            break
        time.sleep(1)
    print(f'  guest goal: {gg and gg["raw"]}', flush=True)
    ck(gg and gg.get('key') != 'FFFFFFFF' and math.dist(gg['pos'], new[0]['pos']) < 40 and 'card=1' in gg['raw'],
       'the guest\'s goal card is on the new village')
    village = gg['pos'] if gg else new[0]['pos']
    gone_key = gg['key'] if gg else '0'  # the guest's own pol of that village

    own, enemy = player_members(host)
    ck(own and enemy, 'the player tribe and another tribe have members')
    if not (own and enemy):
        return ck.result()
    t0 = time.time()
    while time.time() - t0 < 60:
        own, enemy = player_members(host)
        alive = [i for i, hp in own.items() if hp > 0]
        if not alive or not enemy:
            break
        for i in alive:
            host.call(f'coop.tribe pincer {i} {enemy[0]} 300')
        time.sleep(1)
    t2, alas = time.time(), False
    while time.time() - t2 < 75 and not alas:
        time.sleep(3)
        alas = 'no window' not in host.call('coop.ui find "Alas"')['out']
    ck(alas, 'the game-over caption is up on the host')
    bounce_ok(host)
    ck(frames_go_on(host, 15), 'the host bounces back and keeps drawing')
    t1 = time.time()
    while True:
        h1, g1 = tribes(host), tribes(guest)
        lack, extra = diff(h1, g1)
        if (not lack and not extra) or time.time() - t1 > 40:
            break
        time.sleep(2)
    print(f'  after the bounce back: host {show(h1)}; guest {show(g1)}', flush=True)
    ck(all(math.dist(t[1], village) > 40 for t in h1), 'the host\'s reloaded world lacks the village of the goal (the '
       'scenario happened)')
    ck(not extra, f'the guest\'s realign removed that village ({show(extra)} extra)')
    f0 = fault_count(guest)
    print(f'  guest goal after the realign: {(goal(guest) or {}).get("raw")}; faults {f0}', flush=True)
    print('  guest: ' + guest.call('coop.wsync goal dirty')['out'].strip(), flush=True)
    time.sleep(20)
    f1 = fault_count(guest)
    gg = goal(guest)
    print(f'  guest goal 20 s after the goal event: {gg and gg["raw"]}; faults {f1}', flush=True)
    ck(f1 == f0, f'the guest\'s game caught no fault after the goal event ({f1 - f0} new)')
    ck(gg is not None and (gg.get('key') == 'FFFFFFFF' or any(math.dist(t[1], gg['pos']) < 40 for t in h1)),
       f'the guest\'s goal card is not on the removed village ({gg and gg["raw"]})')
    ck(gg is not None and 'card=1' not in gg['raw'] or (gg and gg.get('key') != 'FFFFFFFF'),
       'the guest\'s HUD shows no card of a mission without a village')
    # the mission stays in the manager's map: an event of that village's pol asks for its text directly
    if os.environ.get('BOUNCE_GOAL_PLANT') == 'nohook':  # the card went with the village; only the text guard off
        print('  PLANT: ' + guest.call('coop.fix realign.village_goal off')['out'].strip()[:60], flush=True)
    try:  # (asked from the console the fault is outside the game's own handler: the game dies without the guard)
        print('  guest: ' + guest.call(f'coop.wsync goal dirty {gone_key}')['out'].strip(), flush=True)
        time.sleep(5)
        f2 = fault_count(guest)
        ck(f2 == f1, f'no fault after an event of the removed village\'s own pol {gone_key} ({f2 - f1} new)')
        ck(frames_go_on(guest, 10), 'the guest keeps drawing')
    except (OSError, ConnectionError) as e:
        ck(False, f'the guest\'s game answers after an event of the removed village\'s own pol {gone_key} ({e})')
        return ck.result()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
