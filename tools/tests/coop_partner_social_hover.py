#!/usr/bin/env python3
"""BUG-1250 (players' video): in the social stance the partner's creature got the green ring and its name plate under
the cursor, so the players thought it could be befriended or taken into the pack (a click on it starts nothing).

Creature fixture, friendly fire as the fixture has it. The host's creature stands 5 m in front of the guest's; the
guest's camera is turned onto it (right-drag) and the mouse is put over it (`move x y`, a real cursor move), in the
social stance and then in the combat stance. The game's own hover result is read: cGameViewManager +0x88 (what the
hover update 0xB38F50 picked; docs/re/creature.md#creature-partner-pass), compared with the host's puppet (the
creature, its spatial part +0xC0 or its model +0xB54). Checked: in the social stance the game's hover never is the
host's creature; key frame of the guest's screen with the cursor on the host's creature (no green ring, no plate).
SPORE_FIX_OFF=creature.partner_social_hover must FAIL (the hover picks the host's creature)."""
import re
import struct
import time

from coop_creature_kills import read
from coop_creature_partner_pass import aim
from harness import avatar_pos as pos, puppet
from twoinst import Checks, ability_mode, keyframe, set_stance, shot, start


def u32(con, a):
    b = read(con, a, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else None


def gvm(con):
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call Simulator::cGameViewManager::Get cdecl').get('out') or '')
    return int(m.group(1), 16) if m else 0


def hovered(con, g, pup, x, y, n=8):
    """[the game's hover result is the puppet] over n samples with the mouse moved onto (x, y)"""
    ids = {pup, pup + 0xC0, u32(con, pup + 0xB54) or -1}
    out, raw = [], set()
    for i in range(n):
        con.call(f'move {x + (i % 3) - 1} {y + (i % 2)}')
        time.sleep(0.35)
        h = u32(con, g + 0x88)
        raw.add(h)
        out.append(h in ids)
    return out, raw, ids


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
    time.sleep(3)
    gp = pos(guest)
    host.call('coop.npc gopos %.2f %.2f %.2f' % (gp[0] + 5, gp[1], gp[2]))
    time.sleep(3)
    pup = puppet(guest)
    g = gvm(guest)
    ck(pup and g, f'the guest shows the host\'s creature ({pup:#x}) and has a game view manager ({g:#x})')
    if not (pup and g):
        ck.faults(host=host, guest=guest)
        return ck.result()
    res = {}
    for stance in ('social', 'combat'):
        ck(set_stance(guest, stance), f'the guest is in the {stance} stance ({ability_mode(guest)})')
        o = aim(guest, 'p0')
        if not o:
            print(f'  {stance}: the host\'s creature not on the guest\'s screen', flush=True)
            continue
        hv, raw, ids = hovered(guest, g, pup, o[1], o[2] - 10)
        res[stance] = hv
        print(f'  {stance}: cursor on the host\'s creature at ({o[1]}, {o[2]}): hover is it {sum(hv)} of {len(hv)} '
              f'(hover {sorted("%x" % (r or 0) for r in raw)}, puppet {sorted("%x" % i for i in ids)}); '
              + guest.call('coop.partnerpass')['out'].strip(), flush=True)
        if stance == 'social':
            keyframe('social_hover', 'the guest\'s screen, social stance, the cursor on the host\'s creature: NO green ring '
                     'under it and no name plate over it (the guest cannot befriend its partner)', players={'guest': guest},
                     settle=0)
    ck('social' in res, 'the cursor was put on the host\'s creature in the social stance')
    if 'social' in res:
        ck(not any(res['social']), f'in the social stance the guest\'s game never hovers the host\'s creature '
           f'({sum(res["social"])} of {len(res["social"])})')
    shot(guest, 'social_hover_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
