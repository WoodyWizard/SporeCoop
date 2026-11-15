#!/usr/bin/env python3
"""BUG-1078: a guest's creature attack event aimed at a village the re-alignment removed faults once it starts.

The game's creature attack event (Drama Manager kind 7) keeps its attacked tribe as a counted reference:
SpawnNpcTribe hands it the Nth NPC tribe the game makes (0xD516B0). A guest's game makes NPC tribes for the host's
(the member sync); after the host's game over and reload the re-alignment destroys the ones the host's world has
not any more (realign.villages), but the event's reference keeps the destroyed tribe, without its hut. On a guest
the event is held back (tribe.guest_attacks); alone (the guest left, or the host did) it starts and its start reads
the missing hut's combatant: 0xC0000005 at 0x00D521BA, data 0x120 (lab 20261113-103507: tribe_bounce_world, then
the warm reset's `coop.leave`).

1. Tribe fixture. The host's world gets a new NPC tribe (`coop.tribe form`, as coop_tribe_bounce_world.py); the
   guest's game makes its own for it.
2. The guest's creature attack event gets that tribe as its attacked tribe (`coop.drama target <obj>`: the game's own
   setter, what SpawnNpcTribe does when its count reaches the event's pick; which tribe the game picks is random).
3. The host knocks out its own player tribe, OK on the game-over caption: its game reloads the last save (without
   the new tribe). The guest's re-alignment removes its tribe; the event's target has no hut now.
4. The guest leaves (`coop.leave`) and starts its creature attack (`coop.drama start 7`, the game's StartKind).
PASS: no fault in the guest's game, the event forgot the removed tribe. FAIL (SPORE_FIX_OFF=tribe.attack_target):
the guest's game faults at 0x00D521BA.

  SPORE_LANE=6 python3 tools/tests/coop_tribe_attack_stale.py
"""
import math
import re
import time

from coop_tribe_bounce_world import bounce_ok, diff
from coop_tribe_gameover import frames_go_on, player_members
from coop_tribe_guest_tribes import show, tribes
from twoinst import Checks, start


def tribe_objs(con):
    """[(player, (x, y, z), obj hex)] from the game's tribe list"""
    out = []
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe \d+( \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\): .*obj=(?:0x)?([0-9A-Fa-f]+)', line)
        if m:
            out.append((m.group(1) is not None, tuple(float(m.group(i)) for i in (2, 3, 4)), m.group(5)))
    return out


def target(con, arg=''):
    out = con.call(('coop.drama target ' + arg).strip())['out'].strip()
    m = re.search(r'tribe=(?:0x)?([0-9A-Fa-f]+|\(nil\)) place=(?:0x)?([0-9A-Fa-f]+|\(nil\)) alive=(-?\d+) fix=(\d) '
                  r'dropped=(\d+)', out)
    if not m:
        return out, None
    num = lambda s: 0 if s == '(nil)' else int(s, 16)
    return out, dict(tribe=num(m.group(1)), place=num(m.group(2)), alive=int(m.group(3)), fix=int(m.group(4)),
                     dropped=int(m.group(5)))


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(6)
    hb = [t for t in tribes(host) if not t[0]]
    # BUG-0943: tribe.form_pace holds the third tribe until the party's food is over the threshold times the players
    print('  host: ' + host.call('coop.res add food 300')['out'].strip(), flush=True)
    time.sleep(3)
    print('  host: ' + host.call('coop.tribe form')['out'].strip(), flush=True)
    new, t0 = [], time.time()
    while time.time() - t0 < 40:
        h0, g0 = tribes(host), tribes(guest)
        new = [t for t in h0 if not t[0] and all(math.dist(t[1], x[1]) > 40 for x in hb)]
        lack, extra = diff(h0, g0)
        if new and not lack:
            break
        time.sleep(3)
    print(f'  host {show(h0)}, new {show(new)}; guest {show(g0)}', flush=True)
    ck(new, "the host's world has a new NPC tribe")
    mine = [t for t in tribe_objs(guest) if not t[0] and new and math.dist(t[1], new[0][1]) <= 40]
    ck(mine, "the guest's game has its tribe for the host's new one")
    if not (new and mine):
        return ck.result()
    obj = mine[0][2]
    print('  guest: ' + guest.call('coop.drama start 7')['out'].strip().replace('\n', ' | ')[:300], flush=True)
    out, st = target(guest, obj)
    print(f'  guest: {out}', flush=True)
    ck(st and st['tribe'] == int(obj, 16) and st['place'], f"the guest's creature attack event aims at that tribe ({st})")
    if not st:
        return ck.result()

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
    t1, gone = time.time(), False
    while time.time() - t1 < 60 and not gone:
        time.sleep(3)
        gone = all(t[2] != obj for t in tribe_objs(guest))
    out, st = target(guest)
    print(f'  guest after the re-alignment: {out}', flush=True)
    ck(gone, "the guest's re-alignment removed its tribe of the host's dropped one")
    ck(st and (st['tribe'] == 0 or not st['place']),
       f"the event's attacked tribe has no hut now, or the fix forgot it already (the scenario happened: {st})")

    print('  guest: ' + guest.call('coop.leave')['out'].strip(), flush=True)
    time.sleep(3)
    print('  guest alone: ' + guest.call('coop.drama start 7')['out'].strip().replace('\n', ' | ')[:300], flush=True)
    time.sleep(5)
    out, st = target(guest)
    print(f'  guest alone after the start: {out}', flush=True)
    ck(st and st['tribe'] != int(obj, 16) and st['dropped'] >= 1,
       f"the guest's event forgot the removed tribe before its start ({st})")
    ck(frames_go_on(guest, 5), "the guest's game keeps drawing")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
