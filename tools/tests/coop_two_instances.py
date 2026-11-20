#!/usr/bin/env python3
"""Integration test: two game instances on this machine, host + guest.

1. kill old games, deploy build, restore the 'creature' fixture into both test profiles
2. start instance 2 (profile inst2) and instance 1 (profile test), load the saved game
3. instance 1 hosts, instance 2 joins (through the in-game cheat console bridge)
4. move the guest, check both sides see a puppet tracking the remote player
5. M2: NPC proxies and host-authoritative damage
6. M3: DNA (WP-D of 0.7.16, BUG-1291): own DNA per player (the default): the host gains 4, the guest spends 4 (its
   wallet now 4 under the host's), the guest gains 5: each game's own DNA rises by 5 (the host's DNA does not jump to
   the guest's), the difference stays, the evolution bars are equal (the party's); then the shared pool
   (all.shared_dna forced on): the guest's DNA becomes the host's, a gain of 5 of the guest raises both alike.
   FAIL: SPORE_FIX_OFF=creature.own_dna (one pool already in the first half: the guest's DNA is the host's)
7. screenshots -> out/shots/two_host.png, two_guest.png

usage: coop_two_instances.py [--no-restart] [--guest-menu] [--no-walk]
  --guest-menu  the guest stays in the galaxy menu with an empty profile and must
                receive + auto-load the host world after joining
  --no-walk     the guest gets no walk input (seeded failure: the walk check must fail)
"""
import os
import subprocess
import re
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sporectl import Console, recompress_png  # noqa: E402
import lane  # noqa: E402
import settings_lib  # noqa: E402
import twoinst  # noqa: E402
from twoinst import Checks, keyframe, watch  # noqa: E402


def sh(*args):
    lane.sh(*args)


def load(inst, results):
    r = subprocess.run([sys.executable, os.path.join(HERE, 'load_game.py'), '-i', str(inst)],
                       capture_output=True, text=True, timeout=600)
    results[inst] = r.stdout + r.stderr


def wait_up(inst, t=120):
    t0 = time.time()
    while time.time() - t0 < t:
        try:
            if Console(inst).call('ping')['ok']:
                return True
        except OSError:
            pass
        time.sleep(1)
    return False


def shot(con, name):
    path = os.path.join(lane.out_dir('shots'), name)
    r = con.call('shot ' + path)
    if r['ok']:
        recompress_png(path, 0.5)
    return path


def avatar_pos(con):
    for l in con.call('coop.avatar').get('out', '').splitlines():
        if l.startswith('pos='):
            return [float(v) for v in l[5:].split(')')[0].split()]
    return None


def puppet_pos(con):
    """where this game shows the other player (the "puppet pos" line of coop.status)"""
    for l in con.call('coop.status').get('out', '').splitlines():
        if 'puppet pos=(' in l:
            return [float(v) for v in l.split('puppet pos=(')[1].split(')')[0].split()]
    return None


def dna_bar(con):
    """(DNA, evolution bar) of this game: the game's own values (`coop.dna bar`)"""
    out = con.call('coop.dna bar').get('out') or ''
    d, b = re.search(r'dna=(-?[\d.]+)', out), re.search(r'bar=(-?[\d.]+)', out)
    return (float(d.group(1)) if d else None), (float(b.group(1)) if b else None)


def near(a, b, tol=0.01):
    return a is not None and b is not None and abs(a - b) < tol


def settle_dna(host, guest, cond, secs=10):
    """both games' (DNA, bar) once cond(host's, guest's) holds and stays so for 2 s (the host resends every 2 s), or
    the last reading after secs"""
    t0, since, last = time.time(), None, None
    while time.time() - t0 < secs:
        now = (dna_bar(host), dna_bar(guest))
        if cond(*now) and now == last:
            since = since or time.time()
            if time.time() - since >= 2:
                return now
        else:
            since = None
        last = now
        time.sleep(0.5)
    return dna_bar(host), dna_bar(guest)


def dna_m3(host, guest):
    """M3 (BUG-1291): own DNA per player, then the shared pool; True when every step held"""
    ok = True
    shared0 = settings_lib.get(host, 'all.shared_dna')
    print(f'dna: all.shared_dna={shared0} on the host (0: own DNA per player, the default)')
    ok &= shared0 == 0
    any_dna = lambda h, g: h[0] is not None and g[0] is not None
    h0, g0 = settle_dna(host, guest, any_dna)
    host.call('coop.dna add 4')  # both wallets +4: the guest can spend 4 even if its wallet started at 0
    h1, g1 = settle_dna(host, guest, lambda h, g: near(h[0], h0[0] + 4) and near(g[0], g0[0] + 4))
    guest.call('coop.dna spend 4')  # off the guest's own DNA only (the game's way: SetEvolutionPoints)
    h2, g2 = settle_dna(host, guest, lambda h, g: near(g[0], g1[0] - 4))
    guest.call('coop.dna add 5')  # the guest's gain: the party's income, every wallet +5
    h3, g3 = settle_dna(host, guest, lambda h, g: near(h[0], h2[0] + 5) and near(g[0], g2[0] + 5))
    print(f'dna own: host {h0[0]} -> +4 {h1[0]} -> guest spent 4 {h2[0]} -> guest +5 {h3[0]}; '
          f'guest {g0[0]} -> {g1[0]} -> {g2[0]} -> {g3[0]}; bars host {h3[1]} guest {g3[1]}')
    for l in (host.call('coop.dna own').get('out') or '').strip().splitlines()[1:4]:
        print('  host wallets:', l)
    steps = [(near(h1[0], h0[0] + 4) and near(g1[0], g0[0] + 4), "the host's gain of 4 raised both games' DNA by 4"),
             (near(h2[0], h1[0]) and near(g2[0], g1[0] - 4), "the guest's spend of 4 came off the guest's DNA only"),
             (near(h3[0], h2[0] + 5), "the guest's gain of 5 raised the host's DNA by 5"),
             (near(g3[0], g2[0] + 5), "the guest's gain of 5 raised the guest's own DNA by 5"),
             (near(g3[0], h3[0] - 4 + (g0[0] - h0[0])), "the guest's DNA kept its own difference to the host's"),
             (near(h3[1], g3[1], 1.01), 'both evolution bars are the party\'s (equal)')]
    for good, what in steps:
        print(f'  {"ok  " if good else "FAIL"} {what}')
        ok &= good
    keyframe('own_dna', f"Creature, own DNA: the host's HUD shows about {h3[0]:.0f} DNA, the guest's about {g3[0]:.0f} "
             "(the guest spent 4 of its own); the evolution bars equally full", {'host': host, 'guest': guest})
    # the shared pool (all.shared_dna on): one DNA for the party, as 0.7.15
    try:
        forced = settings_lib.force(host, [guest], 'all.shared_dna', 'on')
        print(f'dna shared: the guest plays with all.shared_dna on: {forced}')
        ok &= forced
        s0 = settle_dna(host, guest, lambda h, g: near(h[0], g[0]))
        guest.call('coop.dna add 5')
        s1 = settle_dna(host, guest, lambda h, g: near(h[0], s0[0][0] + 5) and near(g[0], h[0]))
        print(f'dna shared: host {s0[0][0]} -> {s1[0][0]}, guest {s0[1][0]} -> {s1[1][0]}')
        for good, what in ((near(s0[0][0], s0[1][0]), "shared: the guest's DNA became the host's"),
                           (near(s1[0][0], s0[0][0] + 5) and near(s1[1][0], s1[0][0]),
                            "shared: the guest's gain of 5 raised the one pool (the same DNA on both)")):
            print(f'  {"ok  " if good else "FAIL"} {what}')
            ok &= good
    finally:
        settings_lib.reset([host, guest], ('all.shared_dna',))
        twoinst.forget_settings()
    return ok


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def main():
    restart = '--no-restart' not in sys.argv
    guest_menu = '--guest-menu' in sys.argv
    if restart:
        sh('kill')
        time.sleep(2)
        # every test starts without the per-player store and the session settings an earlier test of the lane left
        # (E1, BUG-1149; BUG-0762): the guest's own DNA wallet of an earlier test (250, lab 20261119-214823) was
        # loaded from the host's store; this test does not go through twoinst.start, which forgets them
        twoinst.forget_store()
        twoinst.forget_settings()
        sh('deploy')
        sh('fixture', 'restore', 'creature', lane.HOST_PROFILE)
        if guest_menu:
            subprocess.run(['rm', '-rf', lane.GUEST_DIR + '/Games', lane.GUEST_DIR + '/Games-sporecoop-backup'])
        else:
            sh('fixture', 'restore', 'creature', lane.GUEST_PROFILE)
        lane.run_guest()
        assert wait_up(lane.GUEST), f'instance {lane.GUEST} did not start'
        time.sleep(5)
        lane.run_host()
        assert wait_up(lane.HOST), f'instance {lane.HOST} did not start'
        res = {}
        th = [threading.Thread(target=load, args=(i, res))
              for i in ((lane.HOST,) if guest_menu else (lane.HOST, lane.GUEST))]
        for t in th:
            t.start()
        for t in th:
            t.join()
        for i in (lane.HOST, lane.GUEST):
            print(f'instance {i}:', res.get(i, '').splitlines()[:1])
    host, guest = Console(lane.HOST), Console(lane.GUEST)
    # SPORE_FIX_OFF (the twin of M3: creature.own_dna): this test does not go through twoinst.start, which applies it
    twoinst.apply_fix_off(host, 'host')
    twoinst.apply_fix_off(guest, 'guest')
    print(host.call(lane.HOST_CMD)['out'])
    if guest_menu:
        for _ in range(120):
            if 'GalaxyGameEntry' in guest.call('game.mode')['out']:
                break
            time.sleep(1)
        guest.call('wait.frames 700 60000')
    print(guest.call(lane.JOIN)['out'])
    if guest_menu:
        t0 = time.time()
        while time.time() - t0 < 240 and not guest.call('coop.avatar')['ok']:
            time.sleep(2)
        print(f'guest in the host world after {time.time() - t0:.0f}s:', guest.call('coop.avatar')['out'].splitlines()[:1])
        guest.call('wait.frames 300 60000')
    time.sleep(3)
    watch(lane.HOST, lane.GUEST)  # freezes and dead consoles fail the test (Checks.faults, BUG-0042)
    # walk the guest: W held for 2 s (about 8 m). A click on the ground left of the screen
    # (300,700) was used before: from the guest's spawn at about (-168.8 -468.6 -84.5) that point
    # is on the nest, the creature turns towards it and stays (2 of 14 runs; W from there walks 8 m,
    # BUG-0042 History). --no-walk sends no input (the check must then fail).
    g0 = avatar_pos(guest)
    if '--no-walk' not in sys.argv:
        guest.call('key w down')
        time.sleep(2)
        guest.call('key w up')
    ok = True
    for _ in range(6):
        time.sleep(1)
        for name, con in (('host', host), ('guest', guest)):
            st = con.call('coop.status')['out']
            line = [l for l in st.splitlines() if 'puppet pos' in l]
            print(f'{name}: {line[0].strip() if line else "NO PUPPET"}')
            ok &= bool(line)
    # what the host sees: the guest walked, and the host shows it at the new place, not the old one
    g1, seen = avatar_pos(guest), puppet_pos(host)
    if g0 and g1 and seen:
        moved, err, stale = dist(g0, g1), dist(g1, seen), dist(g0, seen)
        print(f'the guest walked {moved:.1f} m; the host shows it {err:.1f} m off ({stale:.1f} m from the start)')
        ok &= moved > 0.5 and err < stale
    else:
        print('positions missing', g0, g1, seen)
        ok = False
    # M2: NPC proxies + host-authoritative damage
    time.sleep(6)
    glist = guest.call('coop.npc list')['out'].splitlines()
    spawned = [l for l in glist if l.strip().startswith('#') and 'spawned' in l and 'DEAD' not in l]
    print('guest:', glist[0] if glist else '?')
    ok &= len(spawned) > 0
    # BUG-0390: a partner's hit on the host's pack or family (the avatar's herd) is dropped on purpose
    # (npc.party_peace, BUG-0374): the hit goes to an NPC outside both, as the host lists them now
    party = set()
    for l in host.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) .*? (pack|family)\b', l)
        if m:
            party.add(m.group(1))
    targets = [l for l in spawned if l.split()[0].lstrip('#') not in party]
    first = spawned[0].split()[0].lstrip('#') if spawned else None
    print(f'host pack / family: {sorted(party, key=int)}; targets {len(targets)} of {len(spawned)} '
          f'(the first listed, #{first}, {"is" if first in party else "is not"} one of them)')
    ok &= len(targets) > 0
    spawned = targets
    if spawned:
        nid = spawned[0].split()[0].lstrip('#')

        def host_hp():
            for l in host.call('coop.npc list')['out'].splitlines():
                if l.strip().startswith('#' + nid + ' '):
                    return float(l.split('hp=')[1].split()[0])
            return None
        def applied():
            return int(host.call('coop.npc')['out'].split('hits_applied=')[1].split()[0])
        hp0, a0 = host_hp(), applied()
        guest.call(f'coop.npc hit {nid} 1')
        time.sleep(0.5)
        hp1, a1 = host_hp(), applied()
        print(f'npc #{nid}: host hp {hp0} -> {hp1}, hits applied {a0} -> {a1}')
        # the NPC may flee out of range right after the hit; the host must have applied it
        ok &= a1 == a0 + 1 and (hp1 is None or hp1 < hp0)
    # M3: DNA. Until 0.7.16 one shared pool (the guest's DNA == the host's after a gain); WP-D (BUG-1291) gives every
    # player its own DNA by default: the old check (guest == host) asserted the shared pool and failed on the guest's
    # own wallet (lab 20261119-214823: "dna host 0.0 -> 5.0, guest 255.0"). Now each game's own DNA and the party's
    # bar, then the shared pool with the setting on.
    ok &= dna_m3(host, guest)
    print('screens:', shot(host, 'two_host.png'), shot(guest, 'two_guest.png'))
    ck = Checks()
    ck(ok, 'the checks above')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
