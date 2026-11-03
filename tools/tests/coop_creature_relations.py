#!/usr/bin/env python3
"""Creature stage: every NPC has the same relation to the players on both games (BUG-0462).

Player report (0.7.3.4, host mark): "creatures which show neutral for host are hostile for guest". The game decides
an NPC's relation to the player in cSpeciesRelationshipManager::GetRelationship 0xBA40E0: the species record (shared
by social.cpp, MSG_SPECIES) or, when the record is 0, the creature's archetype tuning +0xE84 (+0x390); the click and
the face use CreatureSocialMode 0xC0E800 on top of it.

The guest's proxies are made again after the test's switches (forgotten, resent by the host), then each is
put into the state Create leaves a proxy in when it is made in a herd of ours (no tuning, `coop.npc untune`). For every NPC
the host has near the guest and the guest has a proxy of, read in both games (`coop.social rel`: the
game's own GetRelationship(avatar, npc) and CreatureSocialMode(npc)):
  the relation and the social mode on the guest's proxy equal the host's NPC.

  coop_creature_relations.py   (SPORE_FIX_OFF=creature.relations and SPORE_FIX_OFF=npc.tuning must FAIL)"""
import re
import time

from twoinst import Checks, start

ROW = re.compile(r'^rel (\d+) species=(\S+) tuning=(\S+) default=(-?\d+) relation=(-?\d+) mode=(-?\d+) herd=(\S+) arch=(\S+) tkey=(\S+)', re.M)


def rel(con):
    return {int(m.group(1)): {'sp': m.group(2), 'tuning': m.group(3), 'def': int(m.group(4)), 'rel': int(m.group(5)),
                              'mode': int(m.group(6)), 'arch': m.group(8), 'tkey': m.group(9)}
            for m in ROW.finditer(con.call('coop.social rel')['out'])}


def compare(host, guest):
    h, g = rel(host), rel(guest)
    both = sorted(set(h) & set(g))
    diff = [(i, h[i], g[i]) for i in both if (h[i]['rel'], h[i]['mode']) != (g[i]['rel'], g[i]['mode'])]
    return both, diff, h, g


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    both = []
    t0 = time.time()
    while time.time() - t0 < 30:
        both, diff, h, g = compare(host, guest)
        if len(both) >= 6:
            break
        time.sleep(2)
    # the guest's proxies made again after the test's switches (SPORE_FIX_OFF acts after the join): the guest
    # forgets them, gets their states, asks the host for the spawns again (npc.resync, BUG-0487)
    ids = sorted(rel(guest))
    for i in ids:
        guest.call(f'coop.npc forget {i}')
    t0 = time.time()
    while time.time() - t0 < 30 and len(set(rel(guest)) & set(ids)) < len(ids) * 0.8:
        time.sleep(1)
    time.sleep(3)
    print(f'  proxies made again: {len(set(rel(guest)) & set(ids))} of {len(ids)}', flush=True)
    # and every proxy as Create makes one in a herd of ours (no tuning: the species' nest herd not found yet,
    # seen after a join on lane 9 for species 06805133 without any test command)
    for i in sorted(rel(guest)):
        guest.call(f'coop.npc untune {i}')
    time.sleep(2)
    both, diff, h, g = compare(host, guest)
    print(f'  host NPCs {len(h)}, guest proxies {len(g)}, on both {len(both)}', flush=True)
    for i in both:
        print(f'    {i}: host {h[i]}  guest {g[i]}', flush=True)
    ck(len(both) >= 4, f'NPCs on both games ({len(both)})')
    for i, a, b in diff:
        print(f'  differs {i} species {a["sp"]}: host relation {a["rel"]} mode {a["mode"]} default {a["def"]} arch {a["arch"]}, '
              f'guest relation {b["rel"]} mode {b["mode"]} default {b["def"]} arch {b["arch"]}', flush=True)
    if diff:
        print('  host species records:', host.call('coop.social species')['out'].strip().replace('\n', ' | '), flush=True)
        print('  guest species records:', guest.call('coop.social species')['out'].strip().replace('\n', ' | '), flush=True)
        print('  guest:', guest.call('coop.social')['out'].strip().replace('\n', ' | '), flush=True)
    print('  guest:', guest.call('coop.social rel')['out'].strip().splitlines()[-1], flush=True)
    ck(not diff, f'every NPC has the same relation and social mode on the guest as on the host ({len(diff)} of {len(both)} differ)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
