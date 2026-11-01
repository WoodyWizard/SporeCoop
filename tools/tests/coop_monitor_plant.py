#!/usr/bin/env python3
"""The invariant monitor catches what players would see (docs/06-testing.md#testing-monitor).

Every other test passes only while the monitor (tools/tests/monitor.py, the thread twoinst.start
runs over the games' logs) reports nothing. A rule that never fires proves nothing: this test plants
one problem per rule over the mod's monitor fields in two games on the creature fixture and checks
that the monitor reports each of them (the lab's monitor gate is off for this test, it judges the
violations itself):

  unlocks       the host unlocks 3 creature parts in its game only (`coop.parts unlock 3` with
                parts.shared off on the host, BUG-0401): the guest's
                `parts=` stays under the host's for over a minute
  posse_damage  the host's pack member hits the guest's creature (`coop.hp packhit`) with
                npc.party_peace off: the host forwards it (`damage: ... posse=1`); first with the fix on:
                dropped, no violation
  puppet_hp     creature.partner_hp off on the host (the guest's creature pinned to full health) while
                the guest's own health is held at half (`coop.team hp`)
  drawn         the guest's game stops driving puppets (`coop.puppet hold on`) while the host walks away
  proxies       the guest walks one NPC proxy away from the host's NPC (`coop.npc pwalk`)

ground is planted by puppet_under with SPORE_FIX_OFF=creature.puppet_ground (the guest's states 1 m
under the host's ground while it walks; here a standing puppet is put back on the ground by the game and
the driving: `coop.track lift -0.9` / `-3` never showed under). The switches are put back at the end. On a build without the monitor fields this test fails (no
rule can fire)."""
import re
import time

import twoinst
from twoinst import Checks, start

WANT = ('unlocks', 'posse_damage', 'puppet_hp', 'drawn', 'proxies')


def seen():
    w = twoinst._watch
    return {inv for inv, _, _ in w.violations} if w else set()


def max_hp(con):
    m = re.search(r'^own: hp=([\d.]+)/([\d.]+)', con.call('coop.hp')['out'], re.M)
    return float(m.group(2)) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(twoinst._watch is not None, 'the invariant monitor runs')
    ck(guest.call('coop.avatar')['ok'], 'guest is in the host world')
    gid = '1'  # the guest of a 2-player session
    print('  host parts:', host.call('coop.parts')['out'].strip(), '| guest:', guest.call('coop.parts')['out'].strip(), flush=True)

    # all but posse_damage at once (a pack member that fought the puppet keeps it in combat: its knockback
    # undoes the placing under the ground, so the pack comes last)
    # BUG-0401: the party shares its parts now; the plant is an unlock the guest misses (the host's sync off)
    print('  coop.fix parts.shared off:', host.call('coop.fix parts.shared off')['out'].strip()[:80], flush=True)
    print('  unlock:', host.call('coop.parts unlock 3')['out'].strip(), flush=True)
    for con, cmd in ((host, 'coop.fix creature.partner_hp off'), (guest, 'coop.puppet hold on')):
        print(f'  {cmd}: {con.call(cmd)["out"].strip()[:120]}', flush=True)
    mx = max_hp(guest) or 10.0
    nl = re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out'])
    if nl:
        p = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', guest.call('coop.avatar')['out'])
        x, y, z = (float(v) for v in p.groups()) if p else (0, 0, 0)
        print('  pwalk:', guest.call(f'coop.npc pwalk {nl[0]} {x + 25:.1f} {y:.1f} {z:.1f} 4 60 1')['out'].strip(), flush=True)
    host.call('key w down')
    time.sleep(6)
    host.call('key w up')
    t0 = time.time()
    hp_at = 0.0

    def hold_hp():
        nonlocal hp_at
        if time.time() - hp_at > 1.0:
            guest.call('coop.team hp %.2f' % (0.5 * mx))  # held at half (the regen next to the partner)
            hp_at = time.time()
    while time.time() - t0 < 150 and not (set(WANT) - {'posse_damage'}) <= seen():
        hold_hp()
        time.sleep(1)
    got = seen()
    print(f'  caught after {time.time() - t0:.0f} s: {sorted(got)}', flush=True)

    host.call('coop.fix creature.partner_hp on')
    host.call('coop.fix parts.shared on')
    guest.call('coop.puppet hold off')
    guest.call('coop.team hp %.2f' % mx)

    # posse_damage: a pack member of the host (a recruited wild NPC); with the fix on the hit is dropped
    hsp = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    hsp = hsp.group(1).zfill(8) if hsp else ''
    nid = None
    for _ in range(20):
        for line in host.call('coop.npc list')['out'].splitlines():
            m = re.match(r'\s*#(\d+) \S+ species=(\S+) hp=([\d.]+) dist=([\d.]+)', line)
            if m and m.group(2) != hsp and float(m.group(3)) > 3 and 'DEAD' not in line:
                nid = m.group(1)
                break
        if nid:
            break
        time.sleep(1)
    ck(nid is not None, f'a wild NPC to recruit (#{nid})')
    if nid:
        print(' ', host.call(f'coop.npc recruit {nid}')['out'].strip(), flush=True)
        time.sleep(2)
        print('  fix on: ', host.call(f'coop.hp packhit {gid} 0.5')['out'].strip(), flush=True)
        time.sleep(2)
        twoinst._watch.step()
        ck('posse_damage' not in seen(), 'a pack hit the fix drops is no violation')
        host.call('coop.fix npc.party_peace off')
        print('  fix off:', host.call(f'coop.hp packhit {gid} 0.5')['out'].strip(), flush=True)
        t1 = time.time()
        while time.time() - t1 < 15 and 'posse_damage' not in seen():
            time.sleep(1)
        host.call('coop.fix npc.party_peace on')

    got = seen()
    for inv in WANT:
        ck(inv in got, f'the monitor caught the planted {inv} problem')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
