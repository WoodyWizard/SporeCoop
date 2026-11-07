#!/usr/bin/env python3
"""Tribe: a guest's game has the host's tribe leaders, and its mirrors take the host members' tools (BUG-0652).

Lab log sweep (55 runs: tribe_walk_player 22 of 66, tribe_walk_mobile, tribe_coop, tribe_play):
`fields: tribe_member_tool N tool: applied 6.000, the game reads 11.000`. In the guest's game an NPC tribe's
musician (the host's member holds tool 6) led its tribe (cCommunity +0x11C): it kept the chief's staff
(tribe.leader_tool, BUG-0583), the mirror of the host's leader had a staff too. The leader was each game's own.

1. Tribe fixture. Each game's members (`coop.tribe`: tribe, tool=, leader= from the game's own 0xC23430).
2. A tribe with a member X that does not lead in the host's game and holds a tool 1..9 there. In the guest's game
   X is made its tribe's leader (`coop.tribe chief X`: the game's SetChieftain, the state a load or a respawned
   mirror left in the lab).
PASS (within 15 s): in the guest's game the member that leads X's tribe is the host's leader of it, X does not lead
and holds the host's tool, each tribe has one leader. `SPORE_FIX_OFF=tribe.leader_sync`: X keeps leading: FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_leader_sync.py
"""
import re
import time

from twoinst import Checks, start


def members(con):
    """{member id: (tribe index, tool, leader)} of every tribe"""
    out, ti = {}, None
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)', line)
        if m:
            ti = int(m.group(1))
            continue
        m = re.match(r'\s+#(\d+) .* tool=(-?\d+) leader=(\d)', line)
        if m and m.group(1) != '0':
            out[int(m.group(1))] = (ti, int(m.group(2)), m.group(3) == '1')
    return out


def leaders(ms, tribe):
    return [i for i, (t, _, ld) in ms.items() if t == tribe and ld]


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(10)
    hm, gm = members(host), members(guest)
    tribes = sorted({t for t, _, _ in hm.values()})
    for t in tribes:
        print(f'  tribe {t}: host leaders {leaders(hm, t)}, guest leaders {leaders(gm, t)}', flush=True)
    pick = None
    for i, (t, tool, ld) in sorted(hm.items()):
        if not ld and 1 <= tool <= 9 and i in gm and len(leaders(hm, t)) == 1 and leaders(hm, t)[0] in gm:
            pick = i
            break
    if pick is None:  # no member with a tool: the first one that does not lead
        pick = next((i for i, (t, _, ld) in sorted(hm.items())
                     if not ld and i in gm and len(leaders(hm, t)) == 1 and leaders(hm, t)[0] in gm), None)
    ck(pick is not None, 'a member that does not lead in the host\'s game, its leader mirrored in the guest\'s')
    if pick is None:
        return ck.result()
    tribe, htool, _ = hm[pick]
    lead = leaders(hm, tribe)[0]
    print(f'  member #{pick} (tribe {tribe}, host tool {htool}); the host\'s leader #{lead}', flush=True)
    print('  guest: chief: ' + guest.call(f'coop.tribe chief {pick}')['out'].strip(), flush=True)
    t0, gm = time.time(), members(guest)
    ck(leaders(gm, tribe) == [pick] or pick not in leaders(gm, tribe) and leaders(gm, tribe) == [lead],
       f'setup: #{pick} led tribe {tribe} in the guest\'s game ({leaders(gm, tribe)})')
    while time.time() - t0 < 15:  # the host's members change instruments now and then: both read together
        gm, hm = members(guest), members(host)
        if leaders(gm, tribe) == [lead] and pick in gm and pick in hm and (
                not 1 <= hm[pick][1] <= 9 or gm[pick][1] == hm[pick][1]):
            break
        time.sleep(1)
    print(f'  after {time.time() - t0:.1f} s: guest #{pick} {gm.get(pick)}, #{lead} {gm.get(lead)}; host #{pick} '
          f'{hm.get(pick)}, #{lead} {hm.get(lead)}', flush=True)
    ck(leaders(gm, tribe) == leaders(hm, tribe) == [lead],
       f'the guest\'s game: the host\'s leader #{lead} leads tribe {tribe} (guest {leaders(gm, tribe)}, host '
       f'{leaders(hm, tribe)})')
    ck(pick in gm and not gm[pick][2], f'the guest\'s game: #{pick} does not lead')
    if 1 <= hm[pick][1] <= 9:
        ck(gm[pick][1] == hm[pick][1], f'the guest\'s game: #{pick} holds the host\'s tool {hm[pick][1]} ({gm[pick][1]})')
    for t in tribes:
        ck(len(leaders(gm, t)) <= 1, f'the guest\'s game: tribe {t} has one leader ({leaders(gm, t)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
