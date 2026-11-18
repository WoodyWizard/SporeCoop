#!/usr/bin/env python3
"""BUG-1253 (players' video): the guest eats a body; in the host's game its creature stands idle next to it.

The avatar's eat (after the player's click on a body: the game's own action, its behaviour tree's eat node) plays its
animations with SporeCoop::CreaturePlayAnim, which the avatar's animation relay (PlayAnimation only) never saw.
Creature fixture, as creature_eat: the host kills an NPC next to both players, the guest clicks its body
(`coop.rules click <id>`, the game's own click action 0xD32FD0) and eats. Read every ~0.3 s with `coop.npc cur`: the
guest's own avatar and the host's puppet of it. Checked: the animations the guest's own creature plays while it eats
(the ones it did not play before, walking and standing) are played by the host's puppet of it too.
SPORE_FIX_OFF=creature.avatar_bhv_anims must FAIL (the puppet shows none of them)."""
import re
import time

from coop_creature_eat import cursor, meat
from coop_npc_corpse import Corpses, pos, proxies
from twoinst import Checks, keyframe, shot, start


def cur(con, who):
    m = re.search(rf'^cur {who} anim=([0-9A-F]{{8}})', con.call('coop.npc cur')['out'], re.M)
    return m.group(1) if m else None


def sample(host, guest, gid, secs, rows):
    t0 = time.time()
    while time.time() - t0 < secs:
        rows.append((cur(guest, 'avatar'), cur(host, 'p' + gid)))
        time.sleep(0.3)


def main():
    host, guest = start('creature')
    ck = Checks()
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gid.group(1) if gid else '1'
    t0 = time.time()
    while time.time() - t0 < 30 and not any(s for s, _ in proxies(guest).values()):
        time.sleep(1)
    for c in (host, guest):
        c.call('coop.team hp 1000')
        print('  ' + c.call('coop.rules diet 1')['out'].strip(), flush=True)  # (the fixture's species eat no meat)
    C = Corpses(host, guest, ck)
    cand = C.candidates(maxd=110)
    if not cand:
        ck(False, 'an NPC of another species near the players')
        return ck.result()
    host.call(f'coop.npc gonpc {cand[0][1]} 6')
    time.sleep(1.0)
    hp0 = pos(host)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (hp0[0] + 2, hp0[1], hp0[2]))
    time.sleep(3.0)
    body = None
    for _, nid, hp, p in C.candidates(near=hp0, maxd=30)[:2]:
        C.kill(nid, 'eat', hp)
        if C.dead_on_host(nid):
            body = nid
            break
    ck(body is not None, f'a body next to the players ({body})')
    if body is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    time.sleep(3.0)
    before = []
    guest.call('key w down')  # its walk and stand animations: not the eat's
    sample(host, guest, gid, 2.0, before)
    guest.call('key w up')
    sample(host, guest, gid, 3.0, before)
    base = {a for a, _ in before} | {b for _, b in before}
    k, m0, _ = cursor(guest, body)
    ck(k == 8, f'the guest\'s game offers "eat" over the body #{body} (cursor kind {k})')
    eating = []
    t0 = time.time()
    shot_done = False
    while time.time() - t0 < 25:
        if time.time() - t0 < 12 and len(eating) % 10 == 0:
            cursor(guest, body, click=True)  # the player's click (again, as players do)
        sample(host, guest, gid, 1.0, eating)
        own = {a for a, _ in eating} - base
        if own and not shot_done and time.time() - t0 > 4:
            keyframe('eats', 'the guest eats the body: on the host\'s screen the guest\'s creature bends over the body and '
                     'bites, as on the guest\'s own screen (not standing idle next to it)', settle=0)
            shot_done = True
        m = meat(guest, body)
        if m is not None and m0 is not None and m < m0 - 30:
            break
    m1 = meat(guest, body)
    own = {a for a, _ in eating} - base
    pup = {b for _, b in eating}
    print(f'  before (walk, stand): {sorted(base)}', flush=True)
    print(f'  while eating: the guest\'s own creature {sorted({a for a, _ in eating})}, the host\'s puppet {sorted(pup)}; '
          f'meat {m0} -> {m1}', flush=True)
    print('  host: ' + (re.search(r'remote animations.*', host.call('coop.status')['out']) or re.match('', '')).group(0), flush=True)
    print('  guest: ' + (re.search(r'remote animations.*', guest.call('coop.status')['out']) or re.match('', '')).group(0), flush=True)
    ck(m1 is not None and m0 is not None and m1 < m0 - 1, f'the guest ate from the body (meat {m0} -> {m1})')
    ck(own, f'the guest\'s own creature played its eat animations ({sorted(own)})')
    ck(own and own & pup, f'the host\'s puppet of the guest plays the eat animations of the guest\'s own creature '
       f'({sorted(own & pup)} of {sorted(own)})')
    shot(host, 'puppet_eat_host.png')
    shot(guest, 'puppet_eat_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
