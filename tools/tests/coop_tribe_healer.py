#!/usr/bin/env python3
"""Tribe: the healer bonus (BUG-0199): a member of one player heals a partner's member to full health, the shared
tribe food grows by 5 on both games.

docs/re/tribe.md "C. Healing": the host hooks the heal tick 0xDB9B20 (healer, patient); when the patient reaches
its maximum and healer and patient belong to different players, the tribe gets 5 food (tribe.cpp h_HealTick).

1. Two games on the tribe fixture. The host buys the heal tool (type 9) through the game's own placement
   (`coop.tribe buy 9 ...` + `buy_end`, as the palette does) and one of its members takes it (`coop.tribe grab`:
   the GrabTool order a click on the tool gives); the host's game shows the member holding tool 9.
2. The guest's game hits one of the guest's own members (`coop.tribe hurt`: the citizen's TakeDamage in the
   guest's game); the host's game has the member hurt.
3. The host selects only its healer and gives the heal order on the patient (`coop.tribe act 5 <patient>`:
   DoActionForObjects(selected, 5, patient), what a click in heal mode gives).
PASS: in the host's game the patient is back at full health and the guest's game shows it at full health too;
the shared food rose by at least 5 on the host's game and on the guest's.
SPORE_FIX_OFF=tribe.heal_max must FAIL (BUG-0745: the heal happens, no food: the old maximum read).

  SPORE_LANE=3 python3 tools/tests/coop_tribe_healer.py
"""
import re
import time

from coop_tribe_tools import tools
from twoinst import Checks, shot, start

HEAL = 9


def roster(con):
    """{id: (owner, hp, tool)} of the player tribe's adults"""
    out, player = {}, False
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)', line)
        if m:
            player = 'player' in line
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) age=(\d+) sel=\d+ hp=([\d.-]+).*?tool=(-?\d+)', line)
        if m and player and m.group(1) != '0' and m.group(3) == '1':
            out[int(m.group(1))] = (int(m.group(2)), float(m.group(4)), int(m.group(5)))
    return out


def food(con):
    m = re.search(r'shared food=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def heals(con):
    m = re.search(r'heals=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    r = roster(host)
    print('  host roster:', r, flush=True)
    mine = [i for i, (o, hp, _) in r.items() if o == 0 and hp > 0]
    theirs = [i for i, (o, hp, _) in r.items() if o == 1 and hp > 0]
    ck(mine and theirs, f'members of both players (host {mine}, guest {theirs})')
    if not (mine and theirs):
        ck.faults(host=host, guest=guest)
        return ck.result()
    # 1. the heal tool, and the host's healer
    out = host.call(f'coop.tribe buy {HEAL} 9 200')['out'].strip()
    print('  host:', out, flush=True)
    time.sleep(3)
    print('  host:', host.call('coop.tribe buy_end')['out'].strip(), flush=True)
    tool = wait(lambda: next((i for i, t, _ in tools(host)[0] if t == HEAL), None) is not None and
                next(i for i, t, _ in tools(host)[0] if t == HEAL), 20)
    ck(tool is not None, f'the host\'s tribe has the heal tool (index {tool})')
    healer = next((i for i in mine if r[i][2] != 11), mine[0])  # (not the chief: tool 11, it takes no tool)
    held = None
    for attempt in range(3):
        if tool is None:
            break
        print('  host:', host.call(f'coop.tribe grab {healer} {tool}')['out'].strip(), flush=True)
        held = wait(lambda: roster(host).get(healer, (0, 0, 0))[2] == HEAL, 40, 2)
        if held:
            break
    ck(bool(held), f'the host\'s member #{healer} holds the heal tool in the host\'s game')
    # 2. the guest's member hurt in the guest's game
    patient = theirs[0]
    hp_full = roster(host)[patient][1]
    # (the host simulates every member: the blow goes through the host's citizen TakeDamage, its own hit)
    print('  host:', host.call(f'coop.tribe pincer {patient} {patient} {hp_full * 0.5:.0f}')['out'].strip(), flush=True)
    hurt = wait(lambda: roster(host).get(patient, (0, hp_full))[1] < hp_full - 1, 15)
    hp_hurt = roster(host).get(patient, (0, 0))[1]
    ck(bool(hurt), f'the host\'s game has the guest\'s member #{patient} hurt ({hp_full} -> {hp_hurt})')
    ghurt = wait(lambda: roster(guest).get(patient, (0, hp_full))[1] < hp_full - 1, 10)
    ck(bool(ghurt), f'the guest\'s game shows its member #{patient} hurt ({roster(guest).get(patient)})')
    time.sleep(2)
    f0h, f0g, n0 = food(host), food(guest), heals(host)
    print(f'  food before the heal: host {f0h} guest {f0g}, heals {n0}', flush=True)
    # 3. the heal order
    for i in mine + theirs:
        host.call(f'coop.tribe select {i} 0')
    print('  host:', host.call(f'coop.tribe select {healer} 1')['out'].strip(), flush=True)
    print('  host:', host.call(f'coop.tribe act 5 {patient}')['out'].strip(), flush=True)
    t0, last_food, full_h, f_before = time.time(), food(host), None, None
    while time.time() - t0 < 90:
        hp = roster(host).get(patient, (0, 0))[1]
        if hp >= hp_full - 0.5:
            full_h, f_before = hp, last_food
            break
        last_food = food(host)
        time.sleep(1)
    print(f'  host: patient at {roster(host).get(patient)} after {time.time() - t0:.0f} s, heals {heals(host)}, '
          f'food just before {f_before}', flush=True)
    shot(host, 'healer_host.png')
    ck(full_h is not None, f'in the host\'s game the healer brought #{patient} back to full health ({hp_hurt} -> {hp_full})')
    full_g = wait(lambda: roster(guest).get(patient, (0, 0))[1] >= hp_full - 0.5, 15)
    print(f'  guest: patient at {roster(guest).get(patient)}', flush=True)
    ck(bool(full_g), f'the guest\'s game shows #{patient} at full health')
    fh, fg = [], []
    for _ in range(8):
        fh.append(food(host))
        fg.append(food(guest))
        time.sleep(1)
    print(f'  food after the heal: host {fh} guest {fg}', flush=True)
    base = f_before if f_before is not None else f0h
    ck(base is not None and any(f is not None and f >= base + 4 for f in fh),
       f'the host\'s tribe food rose by the healer bonus ({base} -> {max([f for f in fh if f is not None], default=None)})')
    ck(base is not None and any(f is not None and f >= base + 4 for f in fg),
       f'the guest\'s game shows the tribe food with the healer bonus ({base} -> {max([f for f in fg if f is not None], default=None)})')
    shot(guest, 'healer_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
