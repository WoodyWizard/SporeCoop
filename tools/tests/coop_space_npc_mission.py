#!/usr/bin/env python3
"""Space: an NPC empire's mission done by the guest pays its reward in the host's game too (BUG-0750).

The mission is the game's own: asked for and accepted in the empire's comm screen by real clicks; its reward
(cEmpire::AddMoney in the base OnCompleted, docs/re/space.md 7.5) lands in the taker's money, and the shared money
(shared.cpp; a guest's earning goes to the host as MSG_RES_ADD) must carry it to the other game.

1. Two games on space_free (galaxy flights allowed; contact with 80001FCC at 0046102D and 80001F53 at 00460038).
2. The first mission any NPC empire offers is the Galactic Adventures intro (2AB57FAC, "Beam Down?", reward 0) until it
   is done (cMissionManager::ChooseMission 0xFEEBC0). Test setup: the guest asks the first empire (comm tab, the
   first answer, Missions, "Any missions?"), the intro mission appears and is completed by `coop.space allies
   missions done` (its own SetState(5)).
3. The guest jumps to the second empire, asks the same way; that empire offers a regular mission with a reward;
   "Tell me more...", then the accept answer (real clicks). The accepted mission (state 3) is completed by `coop.space
   allies missions done` (the game's own completion: OnCompleted pays).
PASS: the guest's money rose by the mission's reward and the host's shared money by the same within 10 s.
SPORE_FIX_OFF=res.guest_send must FAIL (the guest's earning stays in the guest's game).

  SPORE_LANE=6 python3 tools/tests/coop_space_npc_mission.py
"""
import re
import time

from coop_space_jump import here, settle, sp
from twoinst import Checks, shot, start

EMPIRES = (('0x0046102D', 0x80001FCC), ('0x00460038', 0x80001F53))


def money(con):
    m = re.search(r'shared money=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def missions(con):
    return [dict(ptr=m.group(1), id=m.group(2), state=int(m.group(3)), giver=int(m.group(4), 16), reward=float(m.group(5)),
                 flags=int(m.group(6), 16))
            for m in re.finditer(r'mission ([0-9A-F]+) id ([0-9A-F]+) state (-?\d+) giver ([0-9A-F]+) reward ([\d.]+) '
                                 r'flags ([0-9A-F]+)', sp(con, 'allies missions'))]


def jump(con, star, pol, who):
    for _ in range(5):
        r = sp(con, 'jumpto', star)
        print(f'  {who} jumpto {star}: {r}', flush=True)
        if r == 'jumping':
            break
        settle(con, who, 10)
        time.sleep(2)
    t0 = time.time()
    while time.time() - t0 < 40 and here(con)[0] != pol:
        time.sleep(2)
    settle(con, who)
    return here(con)[0] == pol


def ask(con, tag):
    """comm tab, the first answer, Missions, "Any missions?" (docs/re/space.md#space-npc-store-runtime)"""
    con.call('click 288 914')
    time.sleep(4)
    con.call('click 720 725')
    time.sleep(3)
    con.call('click 838 754')
    time.sleep(3)
    con.call('click 720 725')
    time.sleep(3)
    shot(con, f'npc_mission_{tag}.png')


def bye(con):
    for x, y in ((720, 756), (424, 882), (720, 756), (424, 882)):
        con.call(f'click {x} {y}')
        time.sleep(1.5)


def main():
    host, guest = start('space_free')
    ck = Checks()
    settle(guest, 'guest', 20)
    known = {m['ptr'] for m in missions(guest)}
    offer = None
    for n, (star, pol) in enumerate(EMPIRES):
        ck(jump(guest, star, pol, 'guest'), f'guest: in the system of the empire {pol:08X}')
        ask(guest, f'{n}_ask')
        new = [m for m in missions(guest) if m['ptr'] not in known and m['giver'] == pol]
        print(f'  {pol:08X} offers {new}', flush=True)
        if new and new[0]['id'] == '2AB57FAC':  # the Galactic Adventures intro (test setup: done)
            print('  intro done:', sp(guest, 'allies missions done', new[0]['ptr']), flush=True)
            bye(guest)
            continue
        if new and new[0]['reward'] > 0:
            offer = new[0]
            break
        bye(guest)
    ck(offer, f'an NPC empire offered a mission with a reward ({offer})')
    if not offer:
        ck.faults(host=host, guest=guest)
        return ck.result()
    guest.call('click 720 725')  # "Tell me more..."
    time.sleep(3)
    shot(guest, 'npc_mission_more.png')
    guest.call('click 720 725')  # accept
    time.sleep(3)
    shot(guest, 'npc_mission_accept.png')
    m = next((x for x in missions(guest) if x['ptr'] == offer['ptr']), None)
    print(f'  after accept: {m}', flush=True)
    ck(m and m['state'] == 3, f'the guest accepted the mission by clicks (state {m["state"] if m else None})')
    bye(guest)
    g0, h0 = money(guest), money(host)
    print('  done:', sp(guest, 'allies missions done', offer['ptr']), flush=True)
    time.sleep(2)
    g1 = money(guest)
    paid = g1 - g0 if g0 is not None and g1 is not None else None
    ck(paid is not None and abs(paid - offer['reward']) < 1,
       f'the mission paid its reward in the guest\'s game ({g0} -> {g1}, reward {offer["reward"]})')
    t0 = time.time()
    while time.time() - t0 < 10 and not (money(host) is not None and h0 is not None and money(host) - h0 >= offer['reward'] - 1):
        time.sleep(1)
    h1 = money(host)
    ck(h0 is not None and h1 is not None and abs((h1 - h0) - offer['reward']) < 1,
       f'the host\'s shared money rose by the reward ({h0} -> {h1})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
