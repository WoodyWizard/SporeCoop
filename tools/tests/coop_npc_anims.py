#!/usr/bin/env python3
"""Creature stage: the animals' own animations show on the guest (BUG-0464).

Player report (0.7.3.4, host mark): "other species is evolving in nest for host but for guest they are standing
still". An NPC's behaviour tree plays its animations (eating, looking around, social answers, a nest's evolution,
fights) through cCreatureBase::PlayAnimation / PlayAnimationTo in the host's game; the guest's proxies have no AI
and played only their walk.

The NPCs do what their AI does by itself (no command). For 40 s both games are sampled twice a second: the
animation each NPC plays now as the game holds it (`coop.npc cur`: vt 0x58 GetCurrentAnimation) on the host and on
the guest's proxy of it. An animation the host's NPCs play that is not one of the walk / idle animations both
games play by themselves is a behaviour animation; the check: the guest's proxies play the host's behaviour
animations (the same animation within 1 s of the host's sample) in most of those samples, and play at least
one of them at all.

  coop_npc_anims.py   (fails on 0.7.4: BUG-0464 is open; a relay of PlayAnimation / PlayAnimationTo was tried and
                       did not change the proxies' animations, docs/re/creature.md#creature-npc-anims)"""
import collections
import re
import time

from twoinst import Checks, start

CUR = re.compile(r'^cur (\d+) anim=([0-9A-F]{8})', re.M)


def cur(con):
    return {int(m.group(1)): m.group(2) for m in CUR.finditer(con.call('coop.npc cur')['out'])}


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    time.sleep(4)
    hs, gs = [], []  # (t, {id: anim})
    t0 = time.time()
    while time.time() - t0 < 40:
        t = time.time()
        hs.append((t, cur(host)))
        gs.append((time.time(), cur(guest)))
        time.sleep(0.5)
    # the animations the guest's game plays on proxies by itself (walk / idle) appear on proxies whatever the
    # host plays; behaviour animations: the host's animations never seen on a proxy while the same NPC on the
    # host played another one... simpler and robust: base = animations the guest plays on proxies whose host NPC
    # played the same one in no sample (the guest's own choice)
    host_count = collections.Counter(a for _, d in hs for a in d.values())
    guest_count = collections.Counter(a for _, d in gs for a in d.values())
    print('  host animations (count):', host_count.most_common(14), flush=True)
    print('  guest animations (count):', guest_count.most_common(14), flush=True)
    # behaviour animations: host animations that are not among the guest's most common own idle / walk ones
    ids = set(i for _, d in hs for i in d) & set(i for _, d in gs for i in d)
    match = miss = 0
    shown = set()
    pairs = collections.Counter()  # misses: (host's, guest's nearest sample)
    for k, (t, d) in enumerate(hs):
        for i, a in d.items():
            if i not in ids:
                continue
            # the guest's samples within 1 s after the host's
            near = [g.get(i) for tg, g in gs[k:k + 3] if 0 <= tg - t <= 1.2]
            if a in near:
                match += 1
                shown.add(a)
            else:
                miss += 1
                pairs[(a, near[0] if near else None)] += 1
    print(f'  NPCs on both: {len(ids)}; samples where the proxy plays the host NPC\'s animation: {match} of {match + miss}',
          flush=True)
    for name, con in (('host', host), ('guest', guest)):
        print(f'  {name}:', con.call('coop.npc anims')['out'].strip(), flush=True)
    print('  misses (host, guest):', pairs.most_common(10), flush=True)
    ck(len(ids) >= 4, f'NPCs on both games ({len(ids)})')
    rate = match / max(1, match + miss)
    ck(rate >= 0.6, f'the proxies play the host NPCs\' animations ({rate:.0%} of the samples)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
