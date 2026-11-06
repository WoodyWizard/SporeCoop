#!/usr/bin/env python3
"""BUG-0557 / BUG-0383: the guest eats bodies of creatures the players killed, and both games show the same body.

Two NPCs near both players are killed by the host. On each game the body gets the Creature stage's own cursor
kind (0xD30830 over it: 8 is "eat") and the player's click on it (0xD32FD0, the game's own action: the creature
walks there and eats), `coop.rules cursor | click <id>`. Checked on the game's own state:
  * the guest's game offers "eat" over the body (cursor kind 8) as the host's does over its own;
  * the guest eats its body: its meat (+0xB84) goes down in the guest's game, and the host's body of the same
    NPC follows (the meat of a body is one value for the party: the host's and the guest's within 5 % of the
    full meat 10 s after the bites stop);
  * the host eats the other body: the guest's body of it shows the same meat.
"""
import argparse
import re
import time

from coop_npc_corpse import Corpses, host_npcs, pos, proxies
from twoinst import Checks, start


VERBOSE = [True]


def cursor(con, nid, click=False):
    out = con.call(f'coop.rules {"click" if click else "cursor"} {nid}')['out']
    if click or VERBOSE[0]:
        print('    ' + out.strip(), flush=True)
    m = re.search(r'kind=(-?\d+) meat=([-\d.]+) dead=(\d)', out)
    return (int(m.group(1)), float(m.group(2)), m.group(3) == '1') if m else (None, None, None)


def meat(con, nid):
    m = re.search(r'meat=([-\d.]+) dead=(\d)', con.call(f'coop.rules meat {nid}')['out'])
    return float(m.group(1)) if m else None


def eat_body(ck, eater, other, nid, who, secs=25.0):
    k, m0, d = cursor(eater, nid)
    print(f'  {who} eats #{nid}: cursor kind {k}, meat {m0}, dead {d}; the other game\'s meat {meat(other, nid)}', flush=True)
    ck(k == 8, f'{who}: the game offers "eat" over the body #{nid} (cursor kind {k}, 8 = eat)')
    if k != 8:
        return
    t0, last, rows = time.time(), m0, []
    while time.time() - t0 < secs:
        if time.time() - t0 < 12:
            cursor(eater, nid, click=True)  # the player clicks again while it eats (as players do)
        time.sleep(1.0)
        me, mo = meat(eater, nid), meat(other, nid)
        rows.append((round(time.time() - t0, 1), me, mo))
        if me is None or me <= 0:
            break
    print(f'  {who} #{nid}: (s, meat in the eater\'s game, in the other) {rows}', flush=True)
    me = rows[-1][1] if rows else None
    ck(me is not None and m0 is not None and me < m0 - 1, f'{who}: its game ate from the body #{nid} (meat {m0} -> {me})')
    time.sleep(3.0)
    me, mo = meat(eater, nid), meat(other, nid)
    full = m0 or 100.0
    ck(me is not None and mo is not None and abs(me - mo) <= 0.05 * full or (me in (None, 0.0) and mo in (None, 0.0)),
       f'{who}: the other game shows the body #{nid} eaten as far (meat {me} here, {mo} there)')


def main():
    ap = argparse.ArgumentParser()
    ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 30 and not any(s for s, _ in proxies(guest).values()):
        time.sleep(1)
    for c in (host, guest):
        c.call('coop.team hp 1000')
        # the creature fixture's species eat no meat (+0x608 0: the eat node never bites): both players eat meat here
        print('  ' + c.call('coop.rules diet 1')['out'].strip(), flush=True)
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
    bodies = []
    for _, nid, hp, p in C.candidates(near=hp0, maxd=30)[:2]:
        C.kill(nid, 'eat', hp)
        if C.dead_on_host(nid):
            bodies.append(nid)
    time.sleep(4.0)
    print(f'  bodies {bodies}; guest proxies {proxies(guest)}', flush=True)
    ck(len(bodies) == 2, f'two bodies made ({bodies})')
    if len(bodies) == 2:
        eat_body(ck, guest, host, bodies[0], 'the guest')
        eat_body(ck, host, guest, bodies[1], 'the host')
    print('  host npcs: ' + str({i: v[:3] for i, v in host_npcs(host).items() if i in bodies}), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
