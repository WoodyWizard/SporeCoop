#!/usr/bin/env python3
"""BUG-0795: a partner's effect message is never played on its puppet when the game has just destroyed it (Creature).

effects::on_message took the raw puppet pointer of the remote table: when the game destroyed (and later freed) the
partner's puppet between two frames (a teardown, the UFO abduction BUG-0520, the game's own destroy), a start that
was queued in the same frame went into the game's effect code on that creature (cCreatureBase effect pools at
+0xCC8 / +0xD68, CreateEffect on its model); the effect then names a creature that is freed a few frames later.

Two games on the creature fixture. On the host, in one call (`coop.puppet gamekillfx 1 E1A4BD93 0xC0FFEE01 4`): the
game destroys the guest's puppet (DestroyInstance, no bookkeeping of ours) and the guest's pooled creature effect
(pool 1: StartStopEffect into the creature's effect pool) arrives 4 times (`effects::inject`, the network's path).
Then: the game made no effect on the destroyed creature (`coop.effects`: played counts the effects the game's call
returned), and no game faulted (also 8 s later, after the block was freed). (A one-shot effect, pool 3, is refused
by the bone check on a destroyed creature already: `bone=` in the received drops.)
SPORE_FIX_OFF=effects.puppet_live must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_effect_puppet_gone.py"""
import re
import time

from twoinst import Checks, start


def played(con):
    m = re.search(r'played=(\d+)', con.call('coop.effects')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(4)
    print('  host: ' + host.call('coop.staleref on')['out'].strip(), flush=True)
    p0 = played(host)
    out = host.call('coop.puppet gamekillfx 1 E1A4BD93 0xC0FFEE01 4')['out'].strip()
    print('  host: ' + out.replace('\n', ' | '), flush=True)
    m = re.search(r'\(([0-9A-Fa-f]+)\) destroyed by the game', out)
    ck(m, 'the host\'s game destroyed the guest\'s puppet with its effects arriving in the same call')
    p1 = played(host)
    print(f'  host: effects played {p0} -> {p1}', flush=True)
    ck(p1 == p0, f'no effect was played on the destroyed puppet ({p1 - p0})')
    time.sleep(1.0)
    if m:
        ref = host.call(f'coop.staleref {m.group(1)}')['out']
        holders = re.findall(r'^\s+(object|ai) noun=(\S+) \+0x([0-9A-F]+) x(\d+)', ref, re.M)
        print('  host: ' + '\n    '.join(ref.strip().splitlines()[:12]), flush=True)
    time.sleep(8)
    print('  host: ' + host.call('coop.staleref off')['out'].strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
