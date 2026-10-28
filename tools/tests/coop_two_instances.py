#!/usr/bin/env python3
"""Integration test: two game instances on this machine, host + guest.

1. kill old games, deploy build, restore the 'creature' fixture into both test profiles
2. start instance 2 (profile inst2) and instance 1 (profile test), load the saved game
3. instance 1 hosts, instance 2 joins (through the in-game cheat console bridge)
4. move the guest, check both sides see a puppet tracking the remote player
5. screenshots -> out/shots/two_host.png, two_guest.png

usage: coop_two_instances.py [--no-restart] [--guest-menu] [--no-walk]
  --guest-menu  the guest stays in the galaxy menu with an empty profile and must
                receive + auto-load the host world after joining
  --no-walk     the guest gets no walk input (seeded failure: the walk check must fail)
"""
import os
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sporectl import Console, recompress_png  # noqa: E402
import lane  # noqa: E402
from twoinst import Checks, watch  # noqa: E402


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


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def main():
    restart = '--no-restart' not in sys.argv
    guest_menu = '--guest-menu' in sys.argv
    if restart:
        sh('kill')
        time.sleep(2)
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
    # M3: shared DNA
    d0 = float(host.call('coop.dna')['out'].split('=')[1])
    guest.call('coop.dna add 5')
    time.sleep(1.5)
    d1 = float(host.call('coop.dna')['out'].split('=')[1])
    d2 = float(guest.call('coop.dna')['out'].split('=')[1])
    print(f'dna host {d0} -> {d1}, guest {d2}')
    ok &= abs(d1 - d0 - 5) < 0.01 and abs(d2 - d1) < 0.01
    print('screens:', shot(host, 'two_host.png'), shot(guest, 'two_guest.png'))
    ck = Checks()
    ck(ok, 'the checks above')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
