#!/usr/bin/env python3
"""GitHub #199 ("vehicles get faster and faster, like flash"): the city-alarm speed boost of the host (BUG-0771,
BUG-0773, BUG-0876), checked on the game's own speed field (+0x218, read with mem.read) and on the guest's screen.

Two games on civ_npc; the host raises the alarm for its capital (`coop.civ alarm <city>`, the path of
cCity::OnAttacked). The alarm boosts the other players' vehicles within 600 m to 1.5x their speed for 20 s (civ_npc
has one player city, the shared capital: every player's vehicle counts, here the host's free vehicle, whose copy the
guest's player sees).

1. BUG-0876: an NPC nation's vehicle is moved next to the capital first (precondition: it stands within 600 m).
   After the alarm no NPC vehicle (owner 254) is boosted and its +0x218 is what it was before.
   Precondition: a vehicle of the guest is boosted to 1.5x.
2. BUG-0771: five more alarms, each one in the moment the last boost has run out and before the mod's boost tick
   put the speed back (`coop.civ alarm <city> expire`: the order of a siege's hit in the game's update after the
   boost's end, measured nowhere else). The player vehicle's +0x218 must stay at 1.5x its own speed (without the fix
   it is read back as its own speed each time: x1.5 per alarm, 11x after six). Then the host orders that vehicle
   400 m away and the guest's game is sampled: the guest's copy must not drive faster than 1.5x the vehicle's own
   top speed (what the guest's player sees, from its positions). 22 s after the last alarm +0x218 is its own again.
3. BUG-0773: an alarm, then the mod forgets the boost while the vehicle lives: (a) `coop.civ hvdrop <id>` (out of the
   host's table), (b) `coop.civ world_change` (the tables' reset of a world change seen while the world stays, the
   BUG-0223 class). Each time the vehicle's +0x218 must be its own speed within 1 s (before: 1.5x for good).

  SPORE_FIX_OFF=civ.boost_players coop_civ_boost_stack.py   (step 1 must FAIL)
  SPORE_FIX_OFF=civ.boost_base coop_civ_boost_stack.py      (step 2 must FAIL)
  SPORE_FIX_OFF=civ.boost_restore coop_civ_boost_stack.py   (step 3 must FAIL)
"""
import math
import re
import struct
import time

from play_civ import civ_state
from twoinst import Checks, start

ADDR = re.compile(r'^\s+veh #(\d+) (?:0x)?([0-9A-Fa-f]+) ', re.M)


def speed(con, addr):
    """the game's top speed of a vehicle (+0x218: what MoveTo hands the locomotion)"""
    out = con.call(f'mem.read 0x{int(addr, 16) + 0x218:08X} 4')['out']
    try:
        return struct.unpack('<f', bytes(int(x, 16) for x in out.split()[1:5]))[0]
    except (ValueError, struct.error):
        return None


def addrs(con):
    return {int(m.group(1)): m.group(2).upper() for m in ADDR.finditer(con.call('coop.civ')['out'])}


def boosts(con):
    """{address: (orig, owner)}"""
    return {m.group(1).upper(): (float(m.group(2)), int(m.group(3))) for m in
            re.finditer(r'^boost (?:0x)?([0-9A-Fa-f]+) orig=([-\d.]+) left=\S+ owner=(-?\d+)',
                        con.call('coop.civ boosts')['out'], re.M)}


def near(v, pos, r):
    return math.dist(v['pos'], pos) <= r


def drive_speed(host, guest, vid, target, seconds=6.0):
    """the host orders vehicle vid to target; the guest's copy's largest speed over 1 s windows (m/s)"""
    print(' ', host.call(f'coop.civ move {vid} {target[0]:.1f} {target[1]:.1f} {target[2]:.1f}')['out'].strip())
    samples = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        gv = civ_state(guest)[1]
        if vid in gv:
            samples.append((time.time(), gv[vid]['pos']))
        time.sleep(0.05)
    best = 0.0
    j = 0
    for i in range(len(samples)):
        while samples[i][0] - samples[j][0] > 1.0:
            j += 1
        dt = samples[i][0] - samples[j][0]
        if dt > 0.8:
            best = max(best, math.dist(samples[i][1], samples[j][1]) / dt)
    moved = math.dist(samples[0][1], samples[-1][1]) if samples else 0.0
    return best, moved, len(samples)


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    cities, hv, _ = civ_state(host)
    mine = [i for i, c in cities.items() if c['player']]
    ck(mine, f'the host has player cities: {mine}')
    if not mine:
        return ck.result()
    ci = mine[0]
    cpos = cities[ci]['pos']
    ad = addrs(host)
    # the player vehicles near the capital, an NPC vehicle (nearest) moved there
    gvs = [i for i, v in hv.items() if 0 <= v['owner'] < 4 and near(v, cpos, 500)]
    npcs = sorted((i for i, v in hv.items() if v['owner'] == 254), key=lambda i: math.dist(hv[i]['pos'], cpos))
    print(f'  capital {ci} at {cpos}; guest vehicles near it {gvs}; NPC vehicles (nearest first) {npcs[:5]}', flush=True)
    ck(gvs, 'a player vehicle stands within 500 m of the host\'s capital (precondition)')
    ck(npcs, 'the planet has NPC vehicles (precondition)')
    if not gvs or not npcs:
        return ck.result()
    npc = npcs[0]
    if not near(hv[npc], cpos, 500):
        print(' ', host.call(f'coop.civ move {npc} {cpos[0]:.1f} {cpos[1]:.1f} {cpos[2]:.1f}')['out'].strip())
        t0 = time.time()
        while time.time() - t0 < 60 and not near(civ_state(host)[1].get(npc, {'pos': (1e9, 0, 0)}), cpos, 450):
            time.sleep(1)
    hv = civ_state(host)[1]
    ck(npc in hv and near(hv[npc], cpos, 590), f'NPC vehicle #{npc} is within 600 m of the capital (precondition)')
    ad = addrs(host)
    gv0 = gvs[0]
    base = {i: speed(host, ad[i]) for i in gvs + [npc] if i in ad}
    print('  own speeds (+0x218): ' + ', '.join(f'#{i} {s:.2f}' for i, s in base.items()), flush=True)

    # 1. BUG-0876: no NPC vehicle boosted
    print(' ', host.call(f'coop.civ alarm {ci}')['out'].strip(), flush=True)
    time.sleep(0.5)
    b = boosts(host)
    sp = {i: speed(host, ad[i]) for i in base}
    print('  boosts: ' + ', '.join(f'{a} orig={o:.2f} owner={w}' for a, (o, w) in b.items()))
    print('  speeds after the alarm: ' + ', '.join(f'#{i} {s:.2f}' for i, s in sp.items()), flush=True)
    ck(ad[gv0] in b and abs(sp[gv0] - 1.5 * base[gv0]) < 0.02 * base[gv0],
       f'the player vehicle #{gv0} is boosted to 1.5x ({base[gv0]:.2f} -> {sp[gv0]:.2f}) (precondition)')
    npc_boosted = [a for a, (_, w) in b.items() if w == 254]
    ck(not npc_boosted and abs(sp[npc] - base[npc]) < 1e-3,
       f'1: no NPC vehicle is boosted ({len(npc_boosted)} are; #{npc}: {base[npc]:.2f} -> {sp[npc]:.2f})')

    # 2. BUG-0771: alarms at the boost's end do not stack
    worst = sp[gv0] / base[gv0]
    for k in range(5):
        time.sleep(0.4)
        host.call(f'coop.civ alarm {ci} expire')
        time.sleep(0.4)
        s = speed(host, ad[gv0])
        worst = max(worst, s / base[gv0])
        print(f'  alarm {k + 2} at the boost\'s end: #{gv0} +0x218 {s:.2f} ({s / base[gv0]:.2f}x)', flush=True)
    ck(worst < 1.5 * 1.01, f'2: the player vehicle\'s top speed stays at 1.5x its own (largest {worst:.2f}x)')
    hv = civ_state(host)[1]
    p = hv[gv0]['pos']
    dx, dz = p[0] - cpos[0], p[2] - cpos[2]
    n = math.hypot(dx, dz) or 1.0
    target = (p[0] + 400 * dx / n, p[1], p[2] + 400 * dz / n)  # away from the city, along the ground
    top, moved, ns = drive_speed(host, guest, gv0, target)
    print(f'  the guest\'s copy of #{gv0}: largest speed {top:.1f} m/s over 1 s, moved {moved:.0f} m '
          f'({ns} samples; own top speed {base[gv0]:.1f})', flush=True)
    ck(moved > 5, f'2: the vehicle drove on the guest\'s screen ({moved:.0f} m, precondition)')
    ck(top <= 1.5 * base[gv0] * 1.3 + 2.0,
       f'2: the guest sees the vehicle at most 1.5x its own top speed ({top:.1f} m/s, limit '
       f'{1.5 * base[gv0] * 1.3 + 2.0:.1f})')
    time.sleep(22)
    s = speed(host, ad[gv0])
    ck(s is not None and abs(s - base[gv0]) < 1e-3,
       f'2: 22 s after the last alarm the top speed is its own again ({s:.2f}, own {base[gv0]:.2f})')
    if s is not None and abs(s - base[gv0]) > 1e-3:
        host.call(f'mem.write 0x{int(ad[gv0], 16) + 0x218:08X} {struct.pack("<f", base[gv0]).hex()}')

    own, a0 = base[gv0], ad[gv0]  # the vehicle keeps its address, its net id changes with the tables
    # 3. BUG-0773: a boost forgotten while the vehicle lives is put back
    for how in ('hvdrop', 'world_change'):
        ids = {a: i for i, a in addrs(host).items()}
        ck(a0 in ids, f'3 ({how}): the vehicle {a0} is listed (precondition)')
        if a0 not in ids:
            break
        print(' ', host.call(f'coop.civ alarm {ci}')['out'].strip(), flush=True)
        time.sleep(0.5)
        boosted = speed(host, a0)
        cmd = f'coop.civ hvdrop {ids[a0]}' if how == 'hvdrop' else 'coop.civ world_change'
        print(' ', host.call(cmd)['out'].strip(), '|', host.call('coop.civ boosts')['out'].strip().replace('\n', ' | '))
        time.sleep(1.0)
        s = speed(host, a0)
        ck(s is not None and abs(s - own) < 1e-3,
           f'3 ({how}): the vehicle\'s top speed is its own 1 s after the mod forgot its boost '
           f'({boosted:.2f} -> {s:.2f}, own {own:.2f})')
        if s is not None and abs(s - own) > 1e-3:
            host.call(f'mem.write 0x{int(a0, 16) + 0x218:08X} {struct.pack("<f", own).hex()}')
        time.sleep(22)  # the boosts run out
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
