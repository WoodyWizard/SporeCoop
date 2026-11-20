#!/usr/bin/env python3
"""BUG-9263 / BUG-9264: every player has their own stage history (the consequence cards, cPlayer +0x10FC..+0x110C) and
their own Creature trait progress (+0x1278 social, +0x127C combat), kept with the party (<planet>.spo.duoparty, PTY2).

What is asserted is each game's own cPlayer (`coop.wstate`: "st player traits=<5 cards> ... social= combat=", the
fields the game's History, its stage-end decision and its editor requests read), never the mod's counters.

  1. the guest is seated; its game gets its own history, started from the host's cards (the history so far was shared)
  2. BUG-9264: the guest kills an animal (`coop.npc hit`, the lethal hit the host lands as its avatar's): the host's
     game's combat progress does not move, the guest's game's grows. FAILS with `coop.fix party.trait_credit off`
     on the host (the host's combat grows, the guest's not)
  3. BUG-9263: the guest's seat plays aggressively (combat 5000, `coop.cards credit`), the host's own Creature card
     is social (FD159D91 written into the host's cPlayer, as the stage end 0xD2C7D3 does): the guest's game shows
     its own Creature card, the one the game's decision (Creature_GetConsequenceTrait) gives for its progress:
     17E5EF84 aggressive; the host's game keeps FD159D91. FAILS with `coop.fix party.own_cards off` (the guest's game
     shows the host's card: the wsync row "stages")
  4. a rejoin: the guest's game shows its own card again (the host's world it loads has the host's)
  5. a reload of the planet by the host (saved by coop.menu): the seat's card is read back from the party file

The stage end itself is the host's command (3), not the players' way through History and the editors (campaign
tests do that); the decision runs on the host's game the moment its world has a stage done that the seat has not.

  SPORE_LANE=6 python3 tools/tests/coop_own_cards.py
  SPORE_LANE=6 SPORE_FIX_OFF=party.own_cards python3 tools/tests/coop_own_cards.py   (FAILS at 3)
  SPORE_LANE=6 SPORE_FIX_OFF=party.trait_credit python3 tools/tests/coop_own_cards.py (FAILS at 2)
"""
import re
import time

import lane
from twoinst import Checks, start, wait_guest_in

HOST_CARD, AGGRESSIVE = 0xFD159D91, 0x17E5EF84


def player(inst):
    """(cards[5], social, combat) of this game's own cPlayer"""
    m = re.search(r'st player traits=([\d,]+) .* social=([-\d.]+) combat=([-\d.]+)', inst.call('coop.wstate').get('out', ''))
    if not m:
        return None
    return [int(x) for x in m.group(1).split(',')], float(m.group(2)), float(m.group(3))


def wait(f, secs, step=1.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except (OSError, ValueError, TypeError):
            pass
        time.sleep(step)
    return False


def guest_id(guest):
    m = re.search(r'client id=(\d+)', guest.call('coop.status').get('out', ''))
    return int(m.group(1)) if m else None


def prey(guest):
    """a live animal of another species the guest mirrors (not its own species: the family)"""
    own = re.search(r'key=0x([0-9A-F]+)', guest.call('coop.avatar').get('out', ''))
    for ln in guest.call('coop.npc list').get('out', '').splitlines():
        m = re.match(r'\s+#(\d+) species=([0-9A-F]+) .*game_dead=0', ln)
        if m and (not own or m.group(2) != own.group(1)):
            return int(m.group(1))
    return None


def main():
    host, guest = start('creature')
    ck = Checks()
    gid = guest_id(guest)
    ck(gid is not None, f'the guest has a player id ({gid})')
    # 1. the guest's own history, from the host's cards
    ck(wait(lambda: 'have=1 own=1' in guest.call('coop.cards')['out'], 30), 'the guest\'s game got its own history')
    h0, g0 = player(host), player(guest)
    ck(h0 and g0 and h0[0] == g0[0], f'it starts from the host\'s cards (host {h0}, guest {g0})')
    # 2. a guest's kill counts for the guest
    nid = prey(guest)
    ck(nid is not None, f'the guest mirrors an animal of another species ({nid})')
    if nid is not None:
        guest.call(f'coop.npc hit {nid} 1000')
        ck(wait(lambda: player(guest)[2] > g0[2], 15), f'the guest\'s game counts its kill (combat {g0[2]} -> {player(guest)[2]})')
        ck(abs(player(host)[2] - h0[2]) < 0.01, f'the host\'s game does not (combat {h0[2]} -> {player(host)[2]})')
    # 3. the stage end: each game its own Creature card
    host.call(f'coop.cards credit {gid} combat 5000')
    host.call(f'coop.wsync stage 1 {HOST_CARD:08X}')
    ck(wait(lambda: player(guest)[0][1] == AGGRESSIVE, 20),
       f'the guest\'s game shows its own Creature card {AGGRESSIVE:08X} ({player(guest)[0][1]:08X})')
    ck(player(host)[0][1] == HOST_CARD, f'the host\'s game keeps its own {HOST_CARD:08X} ({player(host)[0][1]:08X})')
    time.sleep(3)
    ck(player(guest)[0][1] == AGGRESSIVE, 'and the guest\'s card stays (the wsync row "stages" leaves it)')
    # 4. a rejoin
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest joined again')
    ck(wait(lambda: player(guest)[0][1] == AGGRESSIVE, 60), f'after the rejoin the guest\'s card is its own ({player(guest)})')
    # 5. a reload of the planet
    planet = re.search(r'planet=(\S+)\.spo', host.call('coop.party')['out'])
    ck(planet is not None, 'the party names its planet')
    host.call('coop.menu')
    ck(wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 120), 'the host is in the galaxy menu')
    host.call('wait.frames 240 90000')
    if planet:
        host.call(f'coop.load Game0 {planet.group(1)}')
    ck(wait(lambda: player(host)[0][1] == HOST_CARD and player(guest)[0][1] == AGGRESSIVE, 300, 3),
       f'after the reload: host {player(host)}, guest {player(guest)}')
    ck(wait(lambda: player(guest)[2] >= 5000, 30), f'the guest\'s progress came back from the party file ({player(guest)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
