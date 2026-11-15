#!/usr/bin/env python3
"""A guest leaves the session in the middle of a social game (BUG-0808 guest side, BUG-0831 host side).

Real engagement on the guest (the social stance, a click on a Kook, coop_creature_social_ally.engage): the guest's
game opens the game's social meter (BUG-0179) and the host's game starts the social game with its prompt list
(BUG-0525: the host's Kook performs one prompt after another). After the first prompt the guest leaves (`coop.leave`).

--meter (BUG-0808): checked on the guest's own game: the game's social meter object is hidden (its layout's visible
  byte 0) within 3 s after the leave. SPORE_FIX_OFF=social.meter_end: the meter stays on its screen, FAILS.
default (BUG-0831): checked on the host's game for 30 s after the leave: its Kook performs none of that game's prompts
  any more and no fail reaction (`coop.npc cur`, the game's current animation of the creature). SPORE_FIX_OFF=
  social.forget_leaver: the Kook goes on with the departed guest's game, FAILS.

  SPORE_LANE=7 python3 tools/tests/coop_creature_social_leave.py [--meter]
"""
import re
import sys
import time

from coop_creature_social import KOOK, num, social
from coop_creature_social_ally import engage, guest_list, host_list, pos
from coop_creature_social_prompts import cur_anim
from twoinst import Checks, shot, start


def meter(con):
    return con.call('coop.social meter')['out']


def fam(a, prompts):
    """a prompt's animation or a variant of its family (docs/re/creature.md#creature-bt-anims: 0x42A1CA0 + n)"""
    return any(abs(int(a, 16) - int(p, 16)) < 0x20 for p in prompts)


def games(host):
    out = host.call('coop.social games')['out']
    g = {}
    for m in re.finditer(r'^game player=(\d+) npc=(\d+) idx=(-?\d+) prompts=(\S*)', out, re.M):
        g[int(m.group(2))] = [a for a in m.group(4).split(',') if a]
    fail = re.search(r'fail_anim=([0-9A-F]{8})', out)
    return g, fail.group(1) if fail else None


def main():
    want_meter = '--meter' in sys.argv
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
    time.sleep(2)
    gl = guest_list(guest)
    kooks = [i for i, v in host_list(host).items() if v[0] == KOOK and not v[2] and v[1] > 0 and not v[3] and gl.get(i, (False,))[0]]
    ck(kooks, f'the guest mirrors Kooks ({kooks})')
    if not kooks:
        ck.faults(host=host, guest=guest)
        return ck.result()
    host.call(f'coop.npc gonpc {kooks[0]} 10')
    time.sleep(1)
    hp = pos(host)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (hp[0] + 2, hp[1], hp[2]))
    time.sleep(3)
    nid = None
    for k in kooks[:3]:
        if engage(guest, k, 40):
            nid = k
            break
    ck(nid is not None, f'the guest engaged a Kook (#{nid})')
    if nid is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    # the host's game runs the social game: its prompts (wait for the first one on the host's Kook)
    prompts, fail = [], None
    t0 = time.time()
    while time.time() - t0 < 10 and not prompts:
        g, fail = games(host)
        prompts = g.get(nid, [])
        time.sleep(0.5)
    print(f'  host social game of #{nid}: prompts {prompts}, fail reaction {fail}', flush=True)
    ck(prompts, "the host's game runs the guest's social game with prompts")
    t0 = time.time()
    seen = set()
    while time.time() - t0 < 8:
        a = cur_anim(host, nid)
        if a:
            seen.add(a)
        if any(fam(a, prompts) for a in seen):
            break
        time.sleep(0.2)
    print(f'  the host\'s Kook played {sorted(seen)} before the leave', flush=True)
    m1 = meter(guest)
    print(f'  guest meter before the leave: {m1.strip()}', flush=True)
    if want_meter:
        ck(num(r' visible=(-?\d+)', m1, 0) == 1, "the game's social meter is up on the guest's screen")
        shot(guest, 'social_leave_meter_before.png')
    # the guest leaves
    print('  guest:', guest.call('coop.leave')['out'].strip()[:100], flush=True)
    if want_meter:
        vis = None
        t0 = time.time()
        while time.time() - t0 < 3:
            time.sleep(0.5)
            vis = num(r' visible=(-?\d+)', meter(guest), None)
            if vis == 0:
                break
        time.sleep(1)
        m2 = meter(guest)
        print(f'  guest meter after the leave: {m2.strip()}', flush=True)
        shot(guest, 'social_leave_meter_after.png')
        ck(num(r' visible=(-?\d+)', m2, 1) != 1, "the game's social meter is hidden on the guest's screen after the leave")
    else:
        # what the Kook starts after the leave: a prompt it was performing at the leave may play to its end, a prompt
        # that starts again (after another animation) or the fail reaction is the departed guest's game going on
        after, prev = [], cur_anim(host, nid)
        t0 = time.time()
        while time.time() - t0 < 30:
            a = cur_anim(host, nid)
            if a and a != prev and (fam(a, prompts) or a == fail):
                after.append(a)
            if a:
                prev = a
            time.sleep(0.2)
        print(f'  the host\'s Kook after the leave: prompts / fail reaction started {after}; host '
              f'{host.call("coop.social games")["out"].strip()}', flush=True)
        ck(not after, f"the host's Kook stops the departed guest's social game (it played {after})")
        print('  host held of player 1:', host.call('coop.players held 1')['out'].strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
