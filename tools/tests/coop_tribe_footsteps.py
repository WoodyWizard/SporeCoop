#!/usr/bin/env python3
"""Tribe: the guest's game plays the footsteps of the host's epic creature as the host's game does (BUG-0669).

The footsteps of an animated creature (docs/re/tribe.md#tribe-epic-steps): the animation update 0xA01000 calls
FUN_009A6BD0, which plays "foot_step" (FUN_009A6910 -> 0x9A4FA0) per foot channel while the animation moves; it runs
while a gait is blended in, or always for an animation with the epic flag (AnimatedCreature +0x17C -> +0x2D5, set
from the tuning's isEpic at the model's setup). The fixture's planet has an epic (species 0654E5CB) wandering near
the villages: the host's game played 35-52 of its footsteps a minute (audio parameter 0x48DEF6D = the flag = 1),
the guest's game none when its proxy got the host's tuning after its model was set up (made in a herd of ours: flag 0;
in some runs the proxy was made in the species' herd and had the flag from the start). The lab games run
muted: the test reads each game's own audio commands (`coop.audio`) of the footstep path (return address 0x9A6CBC).

1. Two games on the tribe fixture; the guest's proxy of the epic is put into the state of a proxy made in a herd of
   ours (`coop.npc untune`: no tuning, the animation's flag 0); the NPC sync gives it the host's tuning again (BUG-0462).
   Both audio lists marked.
2. 60 s; then each game's footstep plays with the epic flag (parameter 0x48DEF6D = 1).
PASS: the host's game played at least 15, the guest's game from half to twice the host's count.

SPORE_FIX_OFF=npc.epic_steps: the guest's game plays none (FAIL).

  SPORE_LANE=9 python3 tools/tests/coop_tribe_footsteps.py
"""
import collections
import re
import time

from coop_creature_desync import npcs
from twoinst import Checks, start

EPIC = '0654E5CB'  # the fixture's epic near the villages

PATH = '9A6CBC'  # FUN_009A6BD0's call of the footstep (FUN_009A6910)


def steps(con):
    """{(foot sound type 0x39FA0AB, epic flag 0x48DEF6D): plays} of the footstep path since the mark"""
    n = collections.Counter()
    for ln in con.call('coop.audio list 20000')['out'].splitlines():
        if ln.startswith('ev ') and 'cmd=03475365' in ln and PATH in ln.split('from=')[-1]:
            kv = dict(re.findall(r' ([0-9A-F]{8})=([0-9A-F]+)', ln))
            n[(kv.get('039FA0AB', '?'), kv.get('048DEF6D', '?'))] += 1
    return n


def epic(n):
    return sum(v for (t, f), v in n.items() if f == '1')


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    ids = [i for i, (sp, st, p) in npcs(guest).items() if sp == EPIC and st == 'spawned']
    ck(ids, f'the guest has a proxy of the host\'s epic {EPIC} ({ids})')
    for i in ids:
        print(' ', guest.call(f'coop.npc untune {i}')['out'].strip(), flush=True)
    time.sleep(2)
    for c in (host, guest):
        print(' ', c.call('coop.audio mark')['out'].strip(), flush=True)
    time.sleep(60)
    hn, gn = steps(host), steps(guest)
    print(f'  host footsteps {dict(hn)}', flush=True)
    print(f'  guest footsteps {dict(gn)}', flush=True)
    he, ge = epic(hn), epic(gn)
    ck(he >= 15, f'the host\'s game plays the footsteps of its epic ({he})')
    ck(he / 2 <= ge <= he * 2, f'the guest\'s game plays the footsteps of the host\'s epic as often ({ge} vs the host\'s {he})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
