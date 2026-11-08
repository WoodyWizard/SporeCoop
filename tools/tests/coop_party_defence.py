#!/usr/bin/env python3
"""BUG-0735 / BUG-0736 (class BUG-0418): the pack defends a guest, a guest's Summon Flock is the shared world's.

 - BUG-0735: the game's pack defence runs for the avatar only (TakeDamage 0xC075E0 posts 0x400 on the avatar,
   cPosseSimulator 0xD52E40 sends every pack member at the attacker with the stimulus 0x10): when an animal attacked
   the guest's creature (its puppet in the host's game), the host's pack stood by.
 - BUG-0736: a guest's Summon Flock (ability 0x33, Drama Manager event kind 8) was made in the guest's game only.

1. Two games on the creature fixture, the guest next to the host.
2. --flock: the guest plays its own Summon Flock (`coop.rules roar 33`, the ability's first tick through the game's
   own node) or, when its creature has no such ability, starts the game's summon event (`coop.drama start 8`).
   PASS: the host's game made the flock (its pack grows), the guest's game made none of its own (its summon event
   refused, `coop.drama`), and the guest shows the host's new pack members as allies (NF_PACK proxies).
   FAIL: SPORE_FIX_OFF=creature.flock_relay (the guest makes its own flock, the host's pack stays as it was).
3. --defend (default): the host's game summons a flock (its pack, `coop.drama start 8` on the host), then the
   nearest wild animal hits the guest's puppet once (`coop.hp npchit`, through the game's TakeDamage and the damage
   filter). PASS: within 8 s a pack member of the host's game targets that animal (+0xE7C, the target the attack
   node takes) without any command telling it to. FAIL: SPORE_FIX_OFF=npc.pack_defends_partner.

  SPORE_LANE=7 python3 tools/tests/coop_party_defence.py [--flock]
"""
import re
import sys
import time

from twoinst import Checks, start


def pack(con):
    out = con.call('coop.hp pack')['out']
    return [dict(ptr=m.group(1), dead=m.group(2) == '1', target=m.group(3))
            for m in re.finditer(r'^pack (\S+) hp=\S+ from_partner=\S+ dead=(\d) target=(\S+)', out, re.M)]


def drama(con):
    return con.call('coop.drama')['out']


def main():
    flock = '--flock' in sys.argv
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    time.sleep(4)
    if flock:
        p0 = [m for m in pack(host) if not m['dead']]
        d0 = drama(guest)
        r = guest.call('coop.rules roar 33')['out'].strip()
        print('  guest:', r, flush=True)
        if 'no avatar' in r:
            print('  guest:', guest.call('coop.drama start 8')['out'].strip().splitlines()[0], flush=True)
        p1 = p0
        t0 = time.time()
        while time.time() - t0 < 15:
            time.sleep(1)
            p1 = [m for m in pack(host) if not m['dead']]
            if len(p1) > len(p0):
                break
        time.sleep(3)
        d1 = drama(guest)
        print(f'  host pack {len(p0)} -> {len(p1)}', flush=True)
        print('  guest drama:', ' | '.join(ln.strip() for ln in d1.splitlines()), flush=True)
        print('  host drama:', ' | '.join(ln.strip() for ln in drama(host).splitlines()), flush=True)
        print('  rules:', [ln for ln in guest.call('coop.rules')['out'].splitlines() if 'flock' in ln],
              [ln for ln in host.call('coop.rules')['out'].splitlines() if 'flock' in ln], flush=True)
        m0 = re.search(r'summon\s+started=(\d+)', d0)
        m1 = re.search(r'summon\s+started=(\d+)', d1)
        ck(len(p1) > len(p0), f"the host's game made the guest's flock (its pack {len(p0)} -> {len(p1)})")
        ck((int(m1.group(1)) if m1 else 0) == (int(m0.group(1)) if m0 else 0),
           "the guest's own game made no flock of its own (its summon event did not start)")
    else:
        print('  host:', host.call('coop.drama start 8')['out'].strip().splitlines()[0], flush=True)
        members = []
        t0 = time.time()
        while time.time() - t0 < 15 and not members:
            time.sleep(1)
            members = [m for m in pack(host) if not m['dead']]
        ck(members, f"the host's game has a pack ({len(members)} members)")
        time.sleep(6)  # the flock comes to the avatar (and the guest next to it)
        r = host.call('coop.hp npchit 1 0.5')['out'].strip()
        print('  host:', r, flush=True)
        m = re.search(r'animal (\S+)', r)
        attacker = m.group(1) if m else None
        ck(attacker, 'a wild animal near the guest hit the guest\'s creature')
        hit = []
        t0 = time.time()
        while attacker and time.time() - t0 < 8:
            time.sleep(0.5)
            ms = pack(host)
            hit = [x for x in ms if x['target'] == attacker]
            if hit:
                break
        print('  host pack:', pack(host), flush=True)
        print('  host:', [ln for ln in host.call('coop.hp')['out'].splitlines() if ln.startswith('party')], flush=True)
        ck(hit, f"a member of the host's pack goes for the animal that attacked the guest ({len(hit)})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
