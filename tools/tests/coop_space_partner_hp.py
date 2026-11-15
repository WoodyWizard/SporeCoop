#!/usr/bin/env python3
"""Space: the partner's ship shows its real health on the other game (BUG-0697; class BUG-0419: state sent but
not applied).

Both players in the star system of the `space` fixture, each sees the other's ship. The guest's own ship takes a
hit through the game's damage path (`coop.space hit <dmg>`: the combatant's TakeDamage, 40 % of its max); after
2 s the host's game holds the guest's ship puppet (cCombatant health +0x540 / max +0x534, `coop.puppet hp`) at the
guest's share of its max (within 5 %). Then the same the other way: the host's ship hit, the guest's puppet of it.
0.7.10 (SPORE_FIX_OFF=drive.space_hp): the puppet stays at its max (space_drive wrote the max every frame).
"""
import re
import time

from twoinst import Checks, start


def own(con):
    m = re.search(r'hp ([\d.]+) max ([\d.]+)', con.call('coop.space hp')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else (None, None)


def puppet(con, pid):
    m = re.search(rf'puppet {pid} hp ([\d.]+) max ([\d.]+)', con.call('coop.puppet hp')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else (None, None)


def together(con, pid):
    return re.search(rf'together {pid} 1', con.call('coop.status')['out']) is not None


def case(ck, who, victim, viewer, vid):
    hp, mx = own(victim)
    if not ck(hp is not None and mx, f"{who}'s ship has health ({hp} / {mx})"):
        return
    print(' ', victim.call(f'coop.space hit {mx * 0.4:.1f}')['out'].strip())
    time.sleep(2)
    hp, mx = own(victim)
    ph, pm = puppet(viewer, vid)
    print(f"  {who}'s ship {hp} / {mx}; its puppet on the other game {ph} / {pm}", flush=True)
    ck(ph is not None and pm, f"the other game has {who}'s ship puppet ({ph} / {pm})")
    if ph is None or not pm or not mx:
        return
    ck(abs(ph / pm - hp / mx) < 0.05, f"the puppet shows {who}'s health share ({ph / pm:.2f}, {who} {hp / mx:.2f})")


def main():
    host, guest = start('space')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 30 and not (together(host, 1) and together(guest, 0)):
        time.sleep(1)
    ck(together(host, 1) and together(guest, 0), 'both see the partner ship')
    case(ck, 'the guest', guest, host, 1)
    case(ck, 'the host', host, guest, 0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
