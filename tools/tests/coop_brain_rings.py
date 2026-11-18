#!/usr/bin/env python3
"""BUG-1241 (players' video): the host sang to a creature (a social win pays DNA) and the guest's creature started an
animation of its own with a rings effect and kept the rings while it ran about. The shared DNA crosses a brain
threshold on both games: each game plays the brain upgrade on its own creature (the cinematic and the effect
D7BE955C in pool 2, key 02C4C076, docs/re/creature.md#creature-effects-relay) and the effects relay shows it on the
partner's puppet. Here the partner's picture must follow the partner's own game: once a player's own creature has no
brain-up effect any more, its puppet on the other game has none either.

Creature fixture, the guest 4 m from the host. The host's game gains DNA past the brain threshold (`coop.dna add`,
the shared total); each player confirms its caption like a player (Enter), the guest 8 s after the host; then the
guest runs (W held). Read on both games (`coop.effects probe 02C4C076`: the game's own pool entry and whether the
effect is active, on the own avatar and on each puppet) after the captions, after the run and 30 s later.

  coop_brain_rings.py
"""
import re
import time

from harness import avatar_pos, dna, waiter
from twoinst import Checks, keyframe, start

wait = waiter(step=0.5)
KEY = '02C4C076'


def probe(con):
    """{'avatar': active, '<id>': active} of the brain-up effect key on this game"""
    out = con.call(f'coop.effects probe {KEY}')['out']
    d = {m.group(1): int(m.group(2)) for m in re.finditer(r'^probe (\w+) key=\S+ game=\S+ active=(\d)', out, re.M)}
    return d


def caption(con):
    return bool(re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out']))


def state(host, guest, gid):
    h, g = probe(host), probe(guest)
    return dict(host_own=h.get('avatar'), host_puppet_of_guest=h.get(gid), guest_own=g.get('avatar'),
                guest_puppet_of_host=g.get('0'))


def mirrored(s):
    """each game shows the partner's brain-up effect only while the partner's own game has it"""
    return (not s['host_puppet_of_guest'] or s['guest_own']) and (not s['guest_puppet_of_host'] or s['host_own'])


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
        c.call('coop.effects log on')
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gid.group(1) if gid else '1'
    hp = avatar_pos(host)
    if hp:
        guest.call('coop.abilities goto %.1f %.1f %.1f' % (hp[0] + 4, hp[1], hp[2]))
    time.sleep(4)
    print('  before:', state(host, guest, gid), f'dna host {dna(host)} guest {dna(guest)}', flush=True)
    print('  host: ' + host.call('coop.dna add 150')['out'].strip(), flush=True)
    up = wait(lambda: caption(host) and caption(guest), 30)
    print(f'  brain-up captions on both: {bool(up)}; dna host {dna(host)} guest {dna(guest)}', flush=True)
    ck(up, 'the shared DNA plays the brain upgrade (its caption) on both games')
    time.sleep(3)
    keyframe('brain_up', 'the brain upgrade plays on both games, each on its own creature', settle=0)
    print('  in the cinematic:', state(host, guest, gid), flush=True)
    host.call('key enter')
    time.sleep(8)
    print('  the host confirmed:', state(host, guest, gid), flush=True)
    guest.call('key enter')
    wait(lambda: not caption(host) and not caption(guest), 20)
    time.sleep(5)
    s1 = state(host, guest, gid)
    print('  both confirmed:', s1, flush=True)
    ck(mirrored(s1), f'after the captions each game shows the partner\'s brain-up effect only while the partner\'s own game has it ({s1})')
    guest.call('key w down')
    time.sleep(6)
    keyframe('guest_runs', "the guest runs off after its brain upgrade: no rings on its creature on either game unless its own game shows them", settle=0)
    guest.call('key w up')
    s2 = state(host, guest, gid)
    print('  after the guest ran:', s2, flush=True)
    ck(mirrored(s2), f'while the guest runs the host\'s game shows no brain-up effect its own game does not have ({s2})')
    time.sleep(30)
    s3 = state(host, guest, gid)
    print('  30 s later:', s3, flush=True)
    ck(mirrored(s3), f'30 s later the same ({s3})')
    ck(not s3['host_puppet_of_guest'] and not s3['guest_puppet_of_host'], f'30 s later no puppet keeps the brain-up effect ({s3})')
    keyframe('later', '30 s later: no rings on either player\'s creature', calm=3)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
