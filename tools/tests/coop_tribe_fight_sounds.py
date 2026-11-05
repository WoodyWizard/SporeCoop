#!/usr/bin/env python3
"""Tribe: the guest hears the fight's effect sounds and the creature voices of the host's members (BUG-0600).

The host's game runs the members: their behaviour trees start the fight's effects on the creatures (the game's
effect pools: a tool's swing, the chief's aura, the sprint's dust; each effect plays its own sounds), switch
their sprint on and off and play their combat voice (the growl into a fight, the sigh out of it). A guest's
members are mirrors without a tree: its game played none of them. The lab games run muted, so the test reads
the game's own audio commands (`coop.audio`: every play the game's AudioSystem submitted, with the caller):

  * effect sounds: plays from an effect's sound component (return address 0xA3749B, the effect update), without
    the order marker's (the ordering player's own feedback) and the attack animations' per-target hit impacts
    (BUG-0603, not fixed);
  * voices: plays of Creature_CombatVoice (return address 0xC1B0CC enter, 0xC1B170 leave).

1. Two games on the tribe fixture, 6 more members; both audio lists marked; the host's player members attack an
   NPC tribe's member (the player's attack order) and fight for 40 s.
2. PASS: the host's game played at least 4 such sounds and at least 1 voice; the guest's game played the sound ids
   the host's game played at least 80 % as often (counted per id, at most the host's count each), the voices too.

SPORE_FIX_OFF=mirror.fx: the guest's game plays only what its mirrors' relayed animations make (FAIL).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_fight_sounds.py
"""
import collections
import re
import time

from coop_tribe_gameover import player_members
from twoinst import Checks, start

EFFECT_SOUND = 'A3749B'          # the effect's sound component starts its sound (FUN_00a37430)
VOICE = ('C1B0CC', 'C1B170')     # Creature_CombatVoice's PlayProceduralAudio calls (enter, leave)
# the attack order's marker at the target (effect 0xA231DA2F of the tribe UI's order marker 0xCCBE90, sound 0xCE5F2ED0):
# the feedback of the player who gave the order, in that player's game only (not a member's sound)
ORDER_MARKER = {'CE5F2ED0'}
# the hit impacts of an attack animation's per-target events (0xD05B58ED = effect 0x682E8D4B; 0xA7E3E1FB inferred the
# same): a mirror's relayed animation has no target, not fixed (BUG-0603); left out so the test checks BUG-0600's part
PER_TARGET = {'D05B58ED', 'A7E3E1FB'}


def sounds(con):
    """Counter of (kind, sound id) the game's audio system played since the mark"""
    out = con.call('coop.audio list 20000')['out']
    c = collections.Counter()
    for m in re.finditer(r'cmd=03475365 sound=([0-9A-F]{8}) .* from=([0-9A-F]+),([0-9A-F]+)', out):
        if m.group(1) == '00000000':
            continue
        if m.group(2) == EFFECT_SOUND and m.group(1) not in ORDER_MARKER | PER_TARGET:
            c[('fx', m.group(1))] += 1
        elif m.group(3) in VOICE:
            c[('voice', m.group(1))] += 1
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
    for i in own:
        host.call(f'coop.tribe select {i} 1')
    print('  attack:', host.call(f'coop.tribe act 7 {enemy[0]}')['out'].strip())
    time.sleep(40)
    h, g = sounds(host), sounds(guest)
    print('  host  ', dict(sorted(h.items())))
    print('  guest ', dict(sorted(g.items())))
    for name, c in (('host', host), ('guest', guest)):
        print(f'  {name} ' + c.call('coop.mirrorfx')['out'].strip())
    hv = sum(v for k, v in h.items() if k[0] == 'voice')
    gv = sum(min(v, g[k]) for k, v in h.items() if k[0] == 'voice')
    ht = sum(h.values())
    gt = sum(min(v, g[k]) for k, v in h.items())
    ck(ht >= 4 and hv >= 1, f'the host\'s game plays the fight\'s effect sounds and voices ({ht} >= 4, voices {hv} >= 1)')
    ck(gt >= 0.8 * ht, f'the guest\'s game plays them too ({gt} of {ht} >= 80 %, per sound id)')
    ck(gv >= 0.8 * hv, f'the guest\'s game plays the members\' combat voices ({gv} of {hv} >= 80 %)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
