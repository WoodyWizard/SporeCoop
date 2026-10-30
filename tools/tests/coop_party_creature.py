#!/usr/bin/env python3
"""Creature stage with four players (BUG-0152): two guests and one NPC, and a leave during another
guest's world transfer, checked in each game.

Alice hosts, Bob, Carol and Dave are guests (tools/tests/party.py).

1. Joint hunt: Bob and Carol both stand next to the same host NPC and hit it (`coop.npc hit`, the
   damage filter a real bite goes through), each 60 % of its health: it dies once in the host's
   game, the hunt counts ONE kill (the species table of the host and of every guest), and in Bob's,
   Carol's and Dave's games the NPC is dead (the game's own death flag of their mirror).
2. Two guests impress one NPC: Bob and Carol play the same social ability at the same NPC in the
   same second (`coop.social act`): the host impresses it once (impressed +1, the other game
   refused), the species table is the same on every machine.
3. An NPC between two partners: Bob and Carol stand next to the same host NPC far from the host:
   its behaviour tree gets the "noticed" stimulus from their puppets, only from present players'
   puppets (BUG-0016; SPORE_FIX_OFF=npc.partners must FAIL).
4. A leave during another guest's transfer: Dave leaves and joins again; while his game receives
   the host's world, Carol leaves. Dave still gets into the host's world, and Carol joins again
   and gets in too.
5. A leave during another guest's creation transfer: Bob (own creations) leaves and joins again,
   Carol leaves the moment he is back in the roster and joins again: the host's, Carol's and Dave's
   games show Bob's own creature.

  SPORE_LANE=6 python3 tools/tests/coop_party_creature.py
"""
import re
import time

from coop_creature_kills import family, game_dead, killed, npcs, proxies, stimuli
from party import Party
from twoinst import Checks


def table(con):
    """{species: (relation, impressed, killed)} of this game's relationship table"""
    return {m.group(1): (int(m.group(2)), int(m.group(3)), int(m.group(4))) for m in
            re.finditer(r'([0-9A-F]+)/\S+ relation=(-?\d+) x=\d+ impressed=(\d+) killed=(\d+)',
                        con.call('coop.social species')['out'])}


def num(pat, text, default=0):
    m = re.search(pat, text)
    return int(m.group(1)) if m else default


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def candidates(p, fam, guests):
    """host NPCs alive, not the pack's species, mirrored on every one of these guests"""
    h = npcs(p.host.con)
    seen = None
    for g in guests:
        ids = set(proxies(g.con))
        seen = ids if seen is None else seen & ids
    return sorted((n[3], i, n[1]) for i, n in h.items() if i in (seen or set()) and not n[4] and n[2] > 0 and n[1] != fam)


def hunt(ck, p, fam):
    bob, carol = p.guests[0], p.guests[1]
    cand = candidates(p, fam, p.guests)
    ck(cand, f'[hunt] host NPCs mirrored on every guest ({len(cand)})')
    if not cand:
        return None
    _, nid, sp = cand[0]
    hp = npcs(p.host.con)[nid][2]
    k0 = killed(p.host.con, sp)
    for g in (bob, carol):
        print(f'  [hunt] {g.name}:', g.call(f'coop.social goto {nid}'), flush=True)
    time.sleep(1)
    for g in (bob, carol):
        print(f'  [hunt] {g.name} hits #{nid} ({sp}, hp {hp}):', g.call(f'coop.npc hit {nid} {hp * 0.6:.1f}'), flush=True)
    dead = wait(lambda: (npcs(p.host.con).get(nid) or (0, 0, 0, 0, True))[4], 8)
    ck(dead, f'[hunt] the NPC #{nid} died in the host\'s game from the two guests\' hits')
    time.sleep(2.5)
    k1 = killed(p.host.con, sp)
    ck(k1 == k0 + 1, f'[hunt] one kill counted on the host ({k0} -> {k1})')
    for g in p.guests:
        kg = killed(g.con, sp)
        ck(kg == k1, f'[hunt] {g.name}\'s species table counts the same kills ({kg})')
        px = proxies(g.con).get(nid)
        if px:
            d = game_dead(g.con, px[0], sp)
            ck(d is not False, f'[hunt] {g.name}\'s mirror of #{nid} is dead in its game ({d}, hp {px[2]})')
    return nid


def impress(ck, p, fam, skip):
    bob, carol = p.guests[0], p.guests[1]
    cand = [c for c in candidates(p, fam, (bob, carol)) if c[1] != skip]
    ck(cand, f'[impress] host NPCs mirrored on Bob and Carol ({len(cand)})')
    if not cand:
        return
    h0 = p.host.call('coop.social')
    imp0, ref0 = num(r'impressed=(\d+)', h0), num(r'refused=(\d+)', h0)
    for _, nid, sp in cand[:4]:
        t0 = table(p.host.con).get(sp, (0, 0, 0))
        for g in (bob, carol):
            g.call(f'coop.social goto {nid}')
        time.sleep(1)
        sent = []
        for _ in range(4):  # one round for a normal member, two for an alpha; the ability cools down
            r = [g.call(f'coop.social act {nid} 0') for g in (bob, carol)]
            sent.append(['sent' in x for x in r])
            time.sleep(3)
            if num(r'impressed=(\d+)', p.host.call('coop.social')) > imp0:
                break
        h = p.host.call('coop.social')
        imp, ref = num(r'impressed=(\d+)', h), num(r'refused=(\d+)', h)
        print(f'  [impress] #{nid} {sp}: sent {sent}, impressed {imp0} -> {imp}, refused {ref0} -> {ref}', flush=True)
        if imp == imp0:
            continue  # this one did not take (walked away, an alpha): the next NPC
        ck(imp == imp0 + 1, f'[impress] the host impressed #{nid} once ({imp0} -> {imp})')
        time.sleep(2.5)
        t1 = table(p.host.con).get(sp, (0, 0, 0))
        ck(t1[1] == t0[1] + 1, f'[impress] the species counts one more impressed creature ({t0} -> {t1})')
        for g in p.guests:
            ck(table(g.con).get(sp) == t1, f'[impress] {g.name}\'s table shows the same ({table(g.con).get(sp)})')
        return
    ck(False, '[impress] no NPC was impressed by the two guests (4 tried)')


def noticed(ck, p, fam, skip):
    """an NPC between two partner puppets: Bob and Carol stand next to the same host NPC far from the
    host; the NPC's behaviour tree gets the "noticed" stimulus (0x4000000) from their puppets (the
    host's NPC AI treats a partner like the player, BUG-0016), only from live puppets of present
    players. SPORE_FIX_OFF=npc.partners must FAIL here."""
    bob, carol = p.guests[0], p.guests[1]
    st = p.host.call('coop.status')
    pups = {int(m.group(1)): int(m.group(2), 16) for m in re.finditer(r"^remote (\d+) '[^']*' puppet=(?:0x)?([0-9A-Fa-f]+)", st, re.M)}
    mine = {pups.get(bob.pid), pups.get(carol.pid)} - {None, 0}
    h = npcs(p.host.con)
    seen_by = set(proxies(bob.con)) & set(proxies(carol.con))
    far = sorted((n[3], i) for i, n in h.items() if i in seen_by and not n[4] and n[1] != fam and n[3] > 45 and i != skip)
    ck(far and len(mine) == 2, f'[notice] an NPC far from the host ({len(far)}) and both guests\' puppets on the host ({pups})')
    if not far or len(mine) < 2:
        return
    fid = far[0][1]
    ptr = h[fid][0]
    for g in (bob, carol):
        g.call(f'coop.social goto {fid}')
    senders, t0 = {}, time.time()
    while time.time() - t0 < 10:
        for lo, hi, snd in stimuli(p.host.con, ptr):
            if lo & 0x4000000:
                senders[snd] = senders.get(snd, 0) + 1
        time.sleep(0.25)
    print(f'  [notice] #{fid}: noticed from {{{", ".join(f"{k:08X}: {v}" for k, v in senders.items())}}} '
          f'(puppets {[f"{x:08X}" for x in mine]})', flush=True)
    ck(any(s in mine for s in senders), f'[notice] host NPC #{fid} noticed a partner puppet standing next to it')
    stale = [f'{s:08X}' for s in senders if s not in mine and s not in pups.values() and s != 0]
    ck(not stale, f'[notice] it noticed nothing but present players\' puppets ({stale})')


def leave_during_transfer(ck, p):
    carol, dave = p.guests[1], p.guests[2]
    ck(p.leave(dave), '[transfer] Dave leaves')
    time.sleep(2)
    dave.call(dave.join)
    rec = wait(lambda: 'receiving world' in dave.call('coop.status'), 40, 0.25)
    print(f'  [transfer] Dave receiving the world: {bool(rec)}; Carol leaves now', flush=True)
    ck(rec, '[transfer] Dave\'s game receives the host\'s world')
    carol.call('coop.leave')
    ok = p.wait_in(dave, 240, stable=4)
    ck(ok, '[transfer] Dave gets into the host\'s world although Carol left during his transfer')
    ck(p.join(carol), '[transfer] Carol joins again and gets in')
    time.sleep(4)
    p.check_together(ck, 'transfer')
    p.check_partners(ck, 'transfer')


def applied(con, pid):
    """the creature key this game shows for player pid (creations: 'player <id> creature: <key>')"""
    m = re.search(rf'^\s*player {pid} creature: ([0-9A-F]{{8}})', con.call('coop.creations')['out'], re.M)
    return m.group(1) if m else None


def leave_during_creation(ck, p):
    """Bob (own creations) leaves and joins again: his creature goes to every machine. Carol leaves
    the moment Bob is back in the roster (his creation bundle on the way to her) and joins again:
    her game, Dave's and the host's show Bob's own creature"""
    bob, carol, dave = p.guests
    key0 = applied(p.host.con, bob.pid)
    print(f'  [creation] Bob\'s creature on the host: {key0}', flush=True)
    ck(key0, '[creation] the host shows Bob\'s own creature')
    ck(p.leave(bob), '[creation] Bob leaves')
    time.sleep(2)
    bob.call(bob.join)
    wait(lambda: bob.netname in p.ids(), 30, 0.25)
    carol.call('coop.leave')
    print(f'  [creation] Carol left as Bob came back ({p.host.call("coop.creations").splitlines()[0][:100]})', flush=True)
    ck(p.wait_in(bob, 240, stable=4), '[creation] Bob is back in the host\'s world')
    ck(p.join(carol), '[creation] Carol joins again')
    p.ids()
    time.sleep(4)
    for x in p.all:
        if x is bob:
            continue
        k = wait(lambda: applied(x.con, bob.pid), 20)
        ck(k == key0, f'[creation] {x.name}\'s game shows Bob\'s own creature ({k}, want {key0})')
    p.check_partners(ck, 'creation')


def main():
    ck = Checks()
    p = Party.start('creature', 4)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        for x in p.all:
            x.call('coop.team hp 1000')  # nests attack now and then: nobody dies by accident
        p.ids()
        fam = family(p.host.con)
        time.sleep(3)
        nid = hunt(ck, p, fam)
        impress(ck, p, fam, nid)
        noticed(ck, p, fam, nid)
        leave_during_transfer(ck, p)
        leave_during_creation(ck, p)
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
