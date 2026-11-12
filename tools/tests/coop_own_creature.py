#!/usr/bin/env python3
"""O1 of 0.7.13: a guest that plays a Creature world of its own while it stays in the session (BUG-0819, BUG-0801,
BUG-0803).

  1. the guest joins with My Creations that hold another creature under the key of the host's "TigerHorse"
     (coop_nest_species.restart_guest); the host has an NPC of it: in the host's world the guest's game gives the
     host's creation for that key (the BUG-0200 alias, `coop.creations speciesof`)
  2. the guest, still connected, loads a copy of the fixture planet under another name (own_world.load_own)
  3. BUG-0819: in its own world the guest's game gives its own creation for its own key again (the species
     manager's profile: its own body, not aliased)
  4. BUG-0801: the host starts a vote (`coop.vote test`, the host is ready): it ends within 10 s, not after its
     60 s timer (the only guest plays another world and ignores the vote)
  5. BUG-0803: the guest's DNA goes up 250 in its own world: its own game evolves NPC nests (generation +0xF0 of
     its own herds, `coop.nest evolve`) within 40 s

  SPORE_FIX_OFF=creations.alias_own_world coop_own_creature.py  (step 3 FAILS)
  SPORE_FIX_OFF=core.vote_present coop_own_creature.py         (step 4 FAILS)
  SPORE_FIX_OFF=wsync.herd_evo_own coop_own_creature.py        (step 5 FAILS)
"""
import re
import time

import own_world
import twoinst
from coop_creature_desync import wait
from coop_nest_evolve import herds
from coop_nest_species import HOST_ONLY, bem, restart_guest
from twoinst import Checks, start


def speciesof(con):
    m = re.search(r'profile \S+ key ([0-9A-F]{8}) bem ([0-9A-F]{8}) aliased (\d)',
                  con.call(f'coop.creations speciesof {HOST_ONLY:08X}')['out'])
    return m.groups() if m else None


def vote_active(host):
    return host.call('coop.vote')['out'].startswith('vote active')


def main():
    host, guest = start('creature')
    ck = Checks()
    guest, ok = restart_guest(host)
    ck(ok, 'the guest with the colliding My Creations joined')
    for c in (host, guest):
        c.call('coop.npc strays off')
    host_bem, own_bem = bem(host, HOST_ONLY), bem(guest, HOST_ONLY)
    print(f'  {HOST_ONLY:08X}: the host\'s bem {host_bem}, the guest\'s own {own_bem}', flush=True)
    ck(host_bem and own_bem and host_bem != own_bem, 'the guest owns another creation under the key of the host\'s creature')
    print(' ', host.call(f'coop.npc spawn {HOST_ONLY:08X}')['out'].strip(), flush=True)
    p = wait(lambda: (lambda s: s if s and s[1] == host_bem and s[2] == '1' else None)(speciesof(guest)), 40)
    print(f'  in the host\'s world: {guest.call(f"coop.creations speciesof {HOST_ONLY:08X}")["out"].strip()}', flush=True)
    ck(p, 'in the host\'s world the guest\'s game gives the host\'s creation for the host\'s key (BUG-0200)')
    # 2. its own world
    ok = own_world.load_own(guest, 'Scalinus.spo', 'Otherworld', 'Creature')
    ck(ok, 'the guest plays a Creature world of its own')
    ck(own_world.connected(guest), 'the guest is still in the session')
    if not ok:
        return ck.result()
    t0 = time.time()
    while time.time() - t0 < 30 and not guest.call('coop.avatar')['ok']:
        time.sleep(2)
    time.sleep(6)
    print('  guest:', [l for l in guest.call('coop.status')['out'].splitlines() if l.startswith('own world')], flush=True)
    # 3. BUG-0819
    s = wait(lambda: (lambda s: s if s and s[1] == own_bem and s[2] == '0' else None)(speciesof(guest)), 10)
    out = guest.call(f'coop.creations speciesof {HOST_ONLY:08X}')['out'].strip()
    print(f'  in its own world: {out}', flush=True)
    ck(s, f'in its own world the guest\'s game gives its own creation for its key ({out}; its own bem {own_bem})')
    # 4. BUG-0801
    print(' ', host.call('coop.vote test')['out'].strip().splitlines()[0], flush=True)
    t0 = time.time()
    while time.time() - t0 < 70 and vote_active(host):
        time.sleep(0.5)
    took = time.time() - t0
    print(f'  the host\'s vote ended after {took:.1f} s', flush=True)
    ck(took < 10, f'the vote does not wait for the guest that plays another world ({took:.1f} s)')
    # 5. BUG-0803
    g0 = herds(guest)
    print(f'  the guest\'s own nests: {len(g0)}, evolved {sum(1 for v in g0.values() if v[1] > 0)}', flush=True)
    print('  guest:', guest.call('coop.dna add 250')['out'].strip().splitlines()[0], flush=True)
    evolved = 0
    t0 = time.time()
    while time.time() - t0 < 40:
        time.sleep(5)
        st = guest.call('coop.status')['out']
        if 'cinematic' in st and 'playing' in st:
            guest.call('key enter')
        gs = herds(guest)
        evolved = sum(1 for k, v in gs.items() if v[1] > g0.get(k, (None, 0))[1])
        if evolved:
            break
    print(f'  the guest\'s game evolved {evolved} nests of its own world after its DNA', flush=True)
    ck(evolved > 0, f'the guest\'s own nests evolve in its own world ({evolved})')
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    open(twoinst.STATE, 'w').write('{}')  # the guest's My Creations are not the fixture's any more
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
