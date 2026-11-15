#!/usr/bin/env python3
"""Tribe: the guest's game plays the hit impacts of the host's members' attacks (BUG-0603).

An attack animation's event of kind 1 makes one effect per target of the animation (the victim's hit spot: the
blow's flash and its sound). The host's game runs the members, so their animations have targets there; a guest's
mirrors play the relayed animations (BUG-0569) without targets, and the guest's game made none of the impacts.
The lab games run muted, so the test reads the game's own audio commands (`coop.audio`: every play the game's
AudioSystem submitted, with the caller) and counts the sounds of those impacts (0xD05B58ED = effect 0x682E8D4B,
its siblings 0xF0D5A994 / 0xCFECC623, 0xA7E3E1FB), played through an effect's sound component (0xA3749B).

1. Two games on the tribe fixture, 6 more members; an NPC tribe's member put 9 m from the player tribe's; both
   audio lists marked; the host's player members attack it (the player's attack order) and fight for 40 s.
2. PASS: the host's game played at least 4 impact sounds; the guest's game played each impact sound id at least
   80 % as often as the host's (counted per id, at most the host's count each).

SPORE_FIX_OFF=effects.hit_targets: the guest's game plays none of them (FAIL). HIT_LOG=1: `coop.effects log on`.

  SPORE_LANE=9 python3 tools/tests/coop_tribe_hit_impacts.py
"""
import collections
import os
import re
import time

from coop_tribe_fight_sounds import EFFECT_SOUND, PER_TARGET
from coop_tribe_gameover import player_members
from coop_tribe_gather_stall import pos_of
from twoinst import Checks, start

HITS = PER_TARGET | {'F0D5A994', 'CFECC623'}


def hits(con):
    """Counter of the impact sound ids the game's audio system played since the mark"""
    out = con.call('coop.audio list 20000')['out']
    c = collections.Counter()
    for m in re.finditer(r'cmd=03475365 sound=([0-9A-F]{8}) .* from=([0-9A-F]+),', out):
        if m.group(2) == EFFECT_SOUND and m.group(1) in HITS:
            c[m.group(1)] += 1
    return c


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    print(' ', host.call('coop.tribe spawn 6')['out'].strip())
    time.sleep(6)
    own, enemy = player_members(host)
    ck(own and enemy, f'the player tribe ({len(own)}) and another tribe ({len(enemy)}) have members')
    if not (own and enemy):
        return ck.result()
    for c in (host, guest):
        print(' ', c.call('coop.audio mark')['out'].strip())
        if os.environ.get('HIT_LOG'):  # diagnostics: every effect creation with its callers in the game logs
            c.call('coop.effects log on')
    # the victim next to the attackers (setup: the fight starts at once instead of after a walk to its village,
    # which some runs did not finish in 40 s)
    p = pos_of(host, sorted(own)[0])
    if p:
        print(' ', host.call(f'coop.tribe put {enemy[0]} {p[0] + 8:.1f} {p[1]:.1f} {p[2] + 4:.1f}')['out'].strip())
        time.sleep(1)
    for i in own:
        host.call(f'coop.tribe select {i} 1')
    print('  attack:', host.call(f'coop.tribe act 7 {enemy[0]}')['out'].strip())
    if os.environ.get('HIT_LOG'):  # diagnostics: where the victim and the attackers are (the game logs' clock)
        for _ in range(8):
            time.sleep(5)
            print('  at', time.strftime('%H:%M:%S'), 'victim', pos_of(host, enemy[0]),
                  'attackers', [pos_of(host, i) for i in sorted(own)[:5]])
    else:
        time.sleep(40)
    h, g = hits(host), hits(guest)
    print('  host  ', dict(sorted(h.items())))
    print('  guest ', dict(sorted(g.items())))
    for name, c in (('host', host), ('guest', guest)):
        st = c.call('coop.effects')['out']
        print(f'  {name} ' + ' | '.join(l for l in st.splitlines() if 'hits' in l or l.startswith('effects:')))
    ht = sum(h.values())
    gt = sum(min(v, g[k]) for k, v in h.items())
    ck(ht >= 4, f'the host\'s game plays the hit impacts of its members\' attacks ({ht} >= 4)')
    ck(gt >= 0.8 * ht, f'the guest\'s game plays them too ({gt} of {ht} >= 80 %, per sound id)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
