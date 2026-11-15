#!/usr/bin/env python3
"""Tribe: a blow of a guest's own game never brings up that game's own game over (BUG-0685).

The 4-player campaign (lab 20261107-141346): the host's tribe was down to its chief; on one guest the input lock went
up (`busy=input_lock,paused`) and its mirror of the chief went missing for good (`t: 1 missing on the guest (t4)`).
A guest's mirrors have the host's members' health, but a blow of the guest's own game (its TakeDamage) that took the
last mirror with health to 0 made the guest's game decide on its own that the tribe died out: its input lock at once,
12 s later the caption "Alas! Your population has fallen below the minimum ..." (its OK reloads the guest's own last
save) while the host's chief stood. With the lock up the BUG-0172/0288 guard then refused the mirror the host needed.

1. Tribe fixture. The host's members but the chief are knocked out (`coop.tribe pincer`, the game's damage path).
2. The guest's game hits its mirror of the chief with a blow of 1000 (`coop.tribe hurt`: the citizen's TakeDamage in
   the guest's game, as a fight there).
PASS (20 s): the guest's game shows no game-over caption, its input lock stays 0, its mirror of the chief is there with
health and not dead, as the host's chief. `SPORE_FIX_OFF=tribe.mirror_blow`: the guest's own game over: FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_leader_ko.py
"""
import re
import time

from twoinst import Checks, start


def members(con):
    """{id: (player, hp, dead, leader)}"""
    out, player = {}, False
    for line in con.call('coop.tribe')['out'].splitlines():
        if re.match(r'tribe \d+', line):
            player = '(player)' in line
            continue
        m = re.match(r'\s+#(\d+) .*?hp=(-?[\d.]+) .*? dead=(\d) .* leader=(\d)', line)
        if m and m.group(1) != '0':
            out[int(m.group(1))] = (player, float(m.group(2)), m.group(3) == '1', m.group(4) == '1')
    return out


def lock(con):
    m = re.search(r'input_lock=(-?\d+)', con.call('coop.uistate')['out'])
    return int(m.group(1)) if m else None


def caption(con):
    return 'no window' not in con.call('coop.ui find "Alas"')['out']


def own(ms):
    return {i: v for i, v in ms.items() if v[0]}


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(8)
    hm = members(host)
    lead = [i for i, v in own(hm).items() if v[3]]
    enemy = [i for i, v in hm.items() if not v[0] and v[1] > 0]
    print(f'  host player tribe {own(hm)}; leader {lead}; enemies {enemy[:4]}', flush=True)
    ck(len(lead) == 1 and enemy, 'the host\'s player tribe has a leader, another tribe has members')
    if not (len(lead) == 1 and enemy):
        return ck.result()
    lead = lead[0]
    t0 = time.time()
    while time.time() - t0 < 40:  # every member but the chief knocked out on the host
        rest = [i for i, v in own(members(host)).items() if i != lead and v[1] > 0]
        if not rest:
            break
        for i in rest:
            host.call(f'coop.tribe pincer {i} {enemy[0]} 300')
        time.sleep(1)
    t0 = time.time()
    while time.time() - t0 < 15:  # the guest's mirrors have the host's health
        gm = own(members(guest))
        if [i for i, v in gm.items() if v[1] > 0] == [lead]:
            break
        time.sleep(1)
    hm, gm = own(members(host)), own(members(guest))
    print(f'  host {hm}\n  guest {gm}', flush=True)
    ck([i for i, v in hm.items() if v[1] > 0] == [lead] and [i for i, v in gm.items() if v[1] > 0] == [lead],
       f'setup: only the chief #{lead} has health in both games')
    ck(lock(guest) == 0 and not caption(guest), 'setup: the guest\'s game has no input lock, no caption')
    print('  a blow of the guest\'s game on its mirror of the chief:', guest.call(f'coop.tribe hurt {lead} 1000')['out'].strip(),
          flush=True)
    t0, worst = time.time(), []
    while time.time() - t0 < 20:
        g, lk, cap = members(guest).get(lead), lock(guest), caption(guest)
        if lk or cap or not g or g[1] <= 0 or g[2]:
            worst.append(f'{time.time() - t0:.1f} s: mirror {g} lock {lk} caption {cap}')
        time.sleep(1)
    for w in worst[:3] + worst[-2:]:
        print('  guest:', w, flush=True)
    h, g = members(host).get(lead), members(guest).get(lead)
    print(f'  after 20 s: host #{lead} {h}, guest #{lead} {g}', flush=True)
    ck(not caption(guest), 'the guest\'s game shows no game-over caption ("Alas! Your population ...")')
    ck(lock(guest) == 0 and not [w for w in worst if ' lock 1 ' in w], 'the guest\'s input lock stayed 0')
    ck(g and g[1] > 0 and not g[2] and h and h[1] > 0 and not h[2],
       f'the guest\'s mirror of the chief stands with health, as the host\'s chief (guest {g}, host {h})')
    print(' ', [l for l in guest.call('coop.tribe')['out'].splitlines() if 'lifetime' in l][:1], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
