#!/usr/bin/env python3
"""BUG-0420 (class: hits without a relation filter): an NPC nation's army is not a second player of a joint siege.

The host's capture hook counted the attacker of every hit by the owner the mod gave its vehicle; an NPC nation's
vehicle has the owner 0xFE, which was counted as a player: one guest attacking a city that an NPC army attacked too
captured it 50% faster ("joint siege"). Through the game's own capture function (coop.civ siege, as coop_civ_play):

  1. an NPC nation's hit (owner 254) on a city of another nation, then the guest's (player 1): the game's capture
     meter rises by 1.0 for the guest's hit (no joint siege)
  2. control: the host's hit (player 0) on the same city right after: +1.5 (two players within 10 s)

  SPORE_FIX_OFF=civ.siege_players coop_civ_siege_npc.py   (the guest's hit counts as a joint siege: must FAIL)
"""
import re
import time

from coop_civ import civ
from twoinst import Checks, start


def delta(out):
    m = re.search(r'capture ([\d.]+) -> ([\d.]+)', out)
    return float(m.group(2)) - float(m.group(1)) if m else None


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    npc = [k for k, v in hc.items() if not v['player']]
    print('  npc cities', npc[:6])
    ck(npc, f'the planet has cities of other nations: {npc}')
    if npc:
        c = npc[-1]
        r0 = host.call(f'coop.civ siege {c} 254')['out']
        r1 = host.call(f'coop.civ siege {c} 1')['out']
        r2 = host.call(f'coop.civ siege {c} 0')['out']
        d0, d1, d2 = delta(r0), delta(r1), delta(r2)
        print(f'  NPC army hit: {r0} | guest hit: {r1} | host hit: {r2}')
        st = host.call('coop.status')['out']
        print('  host:', ' '.join(x for x in st.split() if x.startswith(('joint_hits=', 'siege_npc='))))
        ck(d1 is not None and abs(d1 - 1.0) < 0.05, f"the guest's hit after an NPC army's is no joint siege (capture +{d1})")
        ck(d2 is not None and abs(d2 - 1.5) < 0.05, f"the host's hit after the guest's is one (capture +{d2})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
