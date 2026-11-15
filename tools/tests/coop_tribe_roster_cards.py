#!/usr/bin/env python3
"""Tribe: a member that dies on the host leaves the guest's roster too (BUG-0961).

play_tribe's end frame (lab 20261112-102606) had 2 cards on the host's roster (the right edge) and 3 on the guest's.
A player-tribe member that dies leaves its tribe and its card leaves the roster through the stage's message listener
(0xCD81F0 -> 0xCD51F0 -> the roster's removal 0xCCBCE0); the guest's mirror of it is taken out of the guest's tribe by
the mod (detach_body) and kept its card.

Two games on the tribe fixture, 2 more members. The host's game kills one host-owned and one guest-owned member (not
the chief) with the game's own blows (`coop.tribe pincer`, an NPC the attacker; the knocked-out ones that stand up
again are hit again, bounded). Checked on the games' own state, 5 s and 20 s after: the roster's cards (the game UI's
card map: [cTribeUI_Get]+0x240 -> +0x98, its size at +0xA8) equal the player tribe's members on each game, and the
same on both; a key frame of both rosters. Without the fix (SPORE_FIX_OFF=tribe.body_card) the guest keeps a card
for each dead member.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_roster_cards.py
"""
import re
import struct
import time

from coop_creature_kills import read
from play_tribe import tribe_state
from twoinst import Checks, game_runs, keyframe, start

STAGE_PTR = 0x0169B2C8  # SporeCoop::cTribeUI_Get's global


def u32(con, a):
    b = read(con, a, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else 0


def cards(con):
    """the roster's cards: the size of the game UI's citizen -> card map"""
    stage = u32(con, STAGE_PTR)
    ui = u32(con, stage + 0x240) if stage else 0
    return u32(con, ui + 0xA8) if ui else -1


def members(con):
    m = re.search(r'tribe \d+ \(player\)[^\n]*: (\d+) members', con.call('coop.tribe')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip()[:80], flush=True)
    time.sleep(8)
    hs = tribe_state(host)
    lead = set(re.findall(r'#(\d+) \S+ .*leader=1', host.call('coop.tribe')['out']))
    mem = [i for i, v in hs.items() if v['tribe'] == 'p']
    enemy = [i for i, v in hs.items() if v['tribe'] != 'p' and v['hp'] > 0]
    c0 = {'host': (cards(host), members(host)), 'guest': (cards(guest), members(guest))}
    print(f'  player tribe {mem}, leaders {sorted(lead)}; cards / members at the start {c0}', flush=True)
    ck(c0['host'][0] == c0['host'][1] and c0['guest'][0] == c0['guest'][1],
       f'at the start each roster has a card per member ({c0})')
    victims = [next((i for i in mem if str(i) not in lead and hs[i]['owner'] == o), None) for o in (0, 1)]
    victims = [v for v in victims if v is not None]
    if not ck(len(victims) == 2 and enemy, f'a host member and a guest member to lose ({victims}), an enemy'):
        return ck.result()
    t0 = time.time()
    while time.time() - t0 < 40:
        hs = tribe_state(host)
        alive = [v for v in victims if v in hs and hs[v]['tribe'] == 'p']
        if not alive:
            break
        for v in alive:
            if hs[v]['hp'] > 0:
                host.call(f'coop.tribe pincer {v} {enemy[0]} 5000')
        time.sleep(1.5)
    hs = tribe_state(host)
    gone = [v for v in victims if v not in hs or hs[v]['tribe'] != 'p']
    print(f'  out of the host\'s player tribe after {time.time() - t0:.0f} s: {gone}', flush=True)
    ck(len(gone) == 2, f'both members died on the host (left the player tribe: {gone})')
    for wait in (5, 15):
        time.sleep(wait)
        c = {'host': (cards(host), members(host)), 'guest': (cards(guest), members(guest))}
        print(f'  +{5 if wait == 5 else 20} s: cards / members {c}; guest '
              f'{next((ln.strip()[-40:] for ln in guest.call("coop.status")["out"].splitlines() if "body cards" in ln), "")}',
              flush=True)
        ck(c['host'][0] == c['host'][1], f'+{5 if wait == 5 else 20} s: the host\'s roster has a card per member ({c["host"]})')
        ck(c['guest'][0] == c['guest'][1], f'+{5 if wait == 5 else 20} s: the guest\'s roster has a card per member '
                                           f'({c["guest"]})')
        ck(c['guest'][0] == c['host'][0], f'+{5 if wait == 5 else 20} s: the same number of cards on both '
                                          f'(host {c["host"][0]}, guest {c["guest"][0]})')
        if wait == 5:
            keyframe('roster', 'both rosters (right edge): the same cards, none for the two members that died', settle=0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
