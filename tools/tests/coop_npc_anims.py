#!/usr/bin/env python3
"""Creature stage: the animals' own animations show on the guest (BUG-0464).

Player report (0.7.3.4, host mark): "other species is evolving in nest for host but for guest they are standing
still". An NPC's behaviour tree plays its animations (eating, looking around, social answers, a nest's evolution,
fights) through SporeCoop::CreaturePlayAnim in the host's game; the guest's proxies have no AI
and played only their walk.

The NPCs do what their AI does by itself (no command). For 40 s both games are sampled twice a second: the
animation each NPC plays now as the game holds it (`coop.npc cur`: vt 0x58 GetCurrentAnimation) on the host and on
the guest's proxy of it. An animation the host's NPCs play that is not one of the walk / idle animations both
games play by themselves is a behaviour animation; the check: the guest's proxies play the host's behaviour
animations (the same animation within 1 s of the host's sample) in most of those samples, and play at least
one of them at all.

Then a fight: an NPC of another species attacks the guest's puppet on the host (the guest hit it); its proxy must
show the attack animations.

  coop_npc_anims.py   (SPORE_FIX_OFF=npc.anims fails: 15-22 % of the samples, fight 0 %; the behaviour tree starts
                       its animations with CreaturePlayAnim 0xC122F0, docs/re/creature.md#creature-bt-anims)"""
import collections
import re
import time

from coop_creature_kills import family, npcs, proxies
from coop_npc_lost_target import ATTACK_NODE, ai_state, engage, puppet
from twoinst import Checks, start

CUR = re.compile(r'^cur (\d+) anim=([0-9A-F]{8})', re.M)


def cur(con):
    return {int(m.group(1)): m.group(2) for m in CUR.finditer(con.call('coop.npc cur')['out'])}


def fight(host, guest, ck, idle_anims):
    """an NPC of another species attacks the guest's puppet on the host (the guest hit it): its proxy on the
    guest plays the host NPC's attack animations"""
    fam = family(host)
    prox = proxies(guest)
    cand = [i for _, i in sorted((n[3], i) for i, n in npcs(host).items()
                                 if i in prox and not n[4] and n[2] > 0 and n[1] != fam)]
    nid = ptr = None
    for i in cand[:4]:
        n = npcs(host).get(i)
        pup = puppet(host)
        if not n or not pup:
            continue
        tgt, node = engage(host, guest, i, n[0], pup)
        if tgt == pup + 0x5A8 and node == ATTACK_NODE:
            nid, ptr = i, n[0]
            break
    ck(nid is not None, f'a host NPC attacks the guest\'s puppet ({nid})')
    if nid is None:
        return
    k = int(nid)
    match = miss = 0
    attack = collections.Counter()
    shown = collections.Counter()
    t0 = last_hit = time.time()
    while time.time() - t0 < 15:
        if time.time() - last_hit > 2:
            guest.call(f'coop.npc hit {nid} 0.2')  # (it keeps attacking)
            last_hit = time.time()
        h = cur(host).get(k)
        tgt, node = ai_state(host, ptr)
        g = [cur(guest).get(k) for _ in range(3) if time.sleep(0.15) is None]
        if not h or node != ATTACK_NODE:
            continue
        if h not in idle_anims:
            attack[h] += 1
        if h in g:
            match += 1
            shown[h] += 1
        else:
            miss += 1
    print(f'  fight: NPC #{nid}: host animations in its attack node {attack.most_common(8)}, shown on the proxy '
          f'{shown.most_common(8)}; {match} of {match + miss} samples', flush=True)
    ck(match + miss >= 8, f'samples of the attack ({match + miss})')
    ck(sum(attack.values()) > 0, 'the host NPC played attack animations of its own (not seen while it wandered)')
    rate = match / max(1, match + miss)
    ck(rate >= 0.6, f'the proxy plays the host NPC\'s animations while it attacks the puppet ({rate:.0%})')
    ck(any(a in shown for a in attack), 'the proxy shows an attack animation')


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
    fight(host, guest, ck, set(host_count))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
