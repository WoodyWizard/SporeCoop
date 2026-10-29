#!/usr/bin/env python3
"""Integration test for any game mode: instance 1 (host) plays a saved game, instance 2
(guest) starts in the galaxy menu with an empty profile, joins, receives and auto-loads the
host world, then both must see each other's puppet.

usage: coop_mode.py [--fixture NAME [--planet SLOT/PLANET]] [--keep-guest] [--move X,Y]
  --fixture      restart both games, restore out/fixtures/NAME.tgz for the host and load
                 SLOT/PLANET there (default: its planet in tools/tests/fixtures.py). Without it the
                 host must already be playing. (Quitting one Wine instance of the bottle kills
                 the others, so the guest is only restarted together with the host.)
  --keep-guest   reuse a running guest
  --move X,Y     host clicks there to move (default 1000,760; Cell: 1250,540)
  --net-pos      the old move check (the guest's received network sample, before BUG-0251)
  --hold         seeded failure: the guest does not move the remote puppets (`coop.puppet hold on`):
                 the Cell and Space check must FAIL with it, --net-pos passes it

The move check (BUG-0251): in Cell and Space the host's puppet on the guest is a game object (a cell,
a UFO); the test asks the guest's game where it is (`puppet pos=` of the host's `remote 0` line in
coop.status), not the network sample. In Tribe and Civ the "puppet" is a cursor marker of the mod
placed at the network sample, so there the check is only that the cursor is delivered.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from twoinst import Checks, wait_guest_in, watch  # noqa: E402
from sporectl import Console, recompress_png  # noqa: E402
import lane  # noqa: E402
import fixtures  # noqa: E402


def up(inst):
    try:
        return Console(inst, timeout=5).call('ping')['ok']
    except OSError:
        return False


def out(con, line):
    return con.call(line).get('out', '')


def shot(con, name):
    path = os.path.join(lane.out_dir('shots'), name)
    if con.call('shot ' + path)['ok']:
        recompress_png(path, 0.5)
    return path


def puppet_line(con):
    for l in out(con, 'coop.status').splitlines():
        if 'puppet pos' in l:
            return l.strip()
    return None


def avatar_pos(con):
    for l in out(con, 'coop.avatar').splitlines():
        if l.startswith('pos='):
            return [float(v) for v in l[5:].split(')')[0].split()]
    return None


def remote_pos(con):
    for l in out(con, 'coop.status').splitlines():
        if l.startswith('remote ') and 'pos=(' in l:
            return [float(v) for v in l.split('pos=(')[1].split(')')[0].split()]
    return None


def puppet_pos(con, rid=0):
    """the game position of remote player <rid>'s puppet (the `puppet pos=` line right after its
    `remote <rid> ` line; the host is player 0), None when it has none"""
    lines = out(con, 'coop.status').splitlines()
    for i, l in enumerate(lines):
        if l.startswith(f'remote {rid} ') and i + 1 < len(lines) and 'puppet pos=(' in lines[i + 1]:
            try:
                return [float(v) for v in lines[i + 1].split('puppet pos=(')[1].split(')')[0].split()]
            except ValueError:
                return None
    return None


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


TOL = {'Cell': 1.5, 'Space': 8.0}  # ~6x the lag of the 0.10 s interpolation delay at each stage's speed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--keep-guest', action='store_true')
    ap.add_argument('--fixture')
    ap.add_argument('--adventure', action='store_true', help='restart both and start the tutorial adventure on both')
    ap.add_argument('--planet')
    ap.add_argument('--port', type=int, default=lane.PORT)
    ap.add_argument('--move', default=None)
    ap.add_argument('--net-pos', action='store_true')
    ap.add_argument('--hold', action='store_true')
    a = ap.parse_args()
    if a.fixture and not a.planet:
        a.planet = fixtures.planet_of(a.fixture)
    spore = os.path.join(ROOT, 'tools', 'spore.sh')
    if a.adventure:
        subprocess.run([spore, 'kill'], stdout=subprocess.DEVNULL)
        time.sleep(2)
        subprocess.run([spore, 'deploy'], check=True, stdout=subprocess.DEVNULL)
        # the Galactic Adventures "play" button offers the next adventure of the profile's
        # campaign: give the guest the host's progress so both get the same adventure
        src, dst = lane.HOST_DIR, lane.GUEST_DIR
        for f in os.listdir(src):
            if f.endswith('.package'):
                shutil.copy2(os.path.join(src, f), os.path.join(dst, f))
        lane.run_guest()
        for _ in range(120):
            if up(lane.GUEST):
                break
            time.sleep(1)
        time.sleep(5)
        lane.run_host()
        for _ in range(120):
            if up(lane.HOST):
                break
            time.sleep(1)
        procs = [subprocess.Popen([sys.executable, os.path.join(HERE, 'adventure_start.py'), '-i', str(i)])
                 for i in (lane.HOST, lane.GUEST)]
        for p in procs:
            p.wait()
        a.keep_guest = True
    if a.fixture:
        subprocess.run([spore, 'kill'], stdout=subprocess.DEVNULL)
        time.sleep(2)
        subprocess.run([spore, 'deploy'], check=True, stdout=subprocess.DEVNULL)
        shutil.rmtree(os.path.join(lane.HOST_DIR, 'Games'), ignore_errors=True)
        subprocess.run([spore, 'fixture', 'restore', a.fixture, lane.HOST_PROFILE], check=True,
                       stdout=subprocess.DEVNULL)
        for d in ('Games', 'Games-sporecoop-backup'):
            shutil.rmtree(os.path.join(lane.GUEST_DIR, d), ignore_errors=True)
        lane.run_guest()
        for _ in range(120):
            if up(lane.GUEST):
                break
            time.sleep(1)
        time.sleep(5)
        lane.run_host()
        for _ in range(120):
            if up(lane.HOST):
                break
            time.sleep(1)
        h = Console(lane.HOST)
        for _ in range(180):
            if 'GalaxyGameEntry' in out(h, 'game.mode'):
                break
            time.sleep(1)
        h.call('wait.frames 900 90000')
        slot, planet = a.planet.split('/')
        print(out(h, f'coop.load {slot} {planet}'))
        t0 = time.time()
        while time.time() - t0 < 240 and not h.call('coop.avatar')['ok']:
            time.sleep(2)
        h.call('wait.frames 600 90000')
        a.keep_guest = True  # fresh guest already waits in the menu
    host = Console(lane.HOST)
    hav = host.call('coop.avatar')
    assert hav['ok'], 'host is not playing: ' + hav.get('out', '')
    mode = hav['out'].split(':')[0]
    print('host plays', mode)
    out(host, 'coop.leave')
    print(out(host, f'cheat coop host {a.port} Alice'))
    if not a.keep_guest:
        if up(lane.GUEST):
            Console(lane.GUEST).call('quit')
            for _ in range(60):
                if not up(lane.GUEST):
                    break
                time.sleep(1)
            time.sleep(3)
        for d in ('Games', 'Games-sporecoop-backup'):
            shutil.rmtree(os.path.join(lane.GUEST_DIR, d), ignore_errors=True)
        lane.run_guest()
        for _ in range(120):
            if up(lane.GUEST):
                break
            time.sleep(1)
    guest = Console(lane.GUEST)
    for _ in range(180):
        m = out(guest, 'game.mode')
        if 'GalaxyGameEntry' in m or guest.call('coop.avatar')['ok']:
            break
        time.sleep(1)
    guest.call('wait.frames 700 60000')
    print(out(guest, f'cheat coop join 127.0.0.1:{a.port} Bob'))
    t0 = time.time()
    # host saved before sharing: close the confirmation box (Enter) whenever it stops the host's
    # frames; a click could land in the game while the host is still saving
    wait_guest_in(host, guest, 300)
    gav = guest.call('coop.avatar')
    print(f'guest after {time.time() - t0:.0f}s:', gav['out'].splitlines()[0] if gav['out'] else '?')
    ok = gav['ok'] and gav['out'].split(':')[0] == mode
    guest.call('wait.frames 400 60000')
    watch(lane.HOST, lane.GUEST)  # freezes and dead consoles fail the test (Checks.faults, BUG-0042)
    for name, con in (('host', host), ('guest', guest)):
        l = puppet_line(con)
        print(f'{name}: {l or "NO PUPPET"}')
        ok &= l is not None
    # host moves; the guest must follow the host's position
    # Cell: the cell fixture's spawn (100, 100) has an obstacle below it: held towards 1000,760 the
    # cell was pushed back and moved 0.1-0.6 m in 2.5 s (2 of 6 runs under the 0.3 m limit, lab
    # 20261028-052417); to the right it swims about 6 m (docs/06-testing.md#testing-cell-move-direction)
    mx, my = map(int, (a.move or ('1250,540' if mode == 'Cell' else '1000,760')).split(','))
    p0 = avatar_pos(host)
    game_obj = mode in TOL and not a.net_pos
    q0 = puppet_pos(guest) if game_obj else None
    if a.hold:
        print('guest: ' + out(guest, 'coop.puppet hold on').strip())
    if mode == 'Cell':  # a cell swims while the button is held (a click moves it ~0.1, BUG-0042)
        host.call(f'button {mx} {my} down')
        time.sleep(2.5)
        host.call(f'button {mx} {my} up')
        time.sleep(1.5)
    else:
        host.call(f'click {mx} {my}')
        time.sleep(4)
    p1 = avatar_pos(host)
    r1 = remote_pos(guest)
    if game_obj:
        # the guest's game must have moved the host's cell / UFO: near the host's new place, nearer
        # to it than to the old one, by a good part of the host's move. Host and guest are read
        # back to back (the host may still drift); a missing puppet is "not yet" until the deadline.
        good, rows, t0 = False, [], time.time()
        while time.time() - t0 < 8.0 and not good:
            p, q = avatar_pos(host), puppet_pos(guest)
            rn = remote_pos(guest)
            if p0 and q0 and p and q:
                moved, err = dist(p0, p), dist(p, q)
                rows.append(f'host moved {moved:.1f} m, guest\'s puppet {err:.2f} m off, puppet moved {dist(q0, q):.1f} m, '
                            f'old place {dist(p0, q):.1f} m, network sample {dist(p, rn) if rn else -1:.2f} m off')
                good = moved > 0.3 and err < TOL[mode] and dist(q0, q) >= 0.5 * moved and err < dist(p0, q)
            else:
                rows.append(f'positions missing host {p0} {p}, puppet {q0} {q}')
            if not good:
                time.sleep(0.5)
        print('\n'.join(rows if not good else rows[-1:]))
        ok &= good
    elif p0 and p1 and r1:
        moved = sum((x - y) ** 2 for x, y in zip(p0, p1)) ** 0.5
        err = sum((x - y) ** 2 for x, y in zip(p1, r1)) ** 0.5
        stale = sum((x - y) ** 2 for x, y in zip(p0, r1)) ** 0.5
        # Tribe / Civ: no game object for the other player (rts_* adapter): the marker is the network
        # sample, so this checks only that the cursor reaches the guest (BUG-0251)
        what = 'network sample' if mode in TOL else 'cursor marker (network)'
        print(f'{what}: host moved {moved:.1f} m, guest has it {err:.1f} m off ({stale:.1f} m from the start)')
        # the host really moved, and the guest shows it at the new place, not the old one (BUG-0042)
        ok &= err < 5.0 and moved > 0.3 and err < stale
    else:
        print('positions missing', p0, p1, r1)
        ok = False
    l = puppet_line(guest)
    print('guest puppet:', l)
    if a.hold:
        out(guest, 'coop.puppet hold off')
    print('screens:', shot(host, 'mode_host.png'), shot(guest, 'mode_guest.png'))
    ck = Checks()
    ck(ok, 'the checks above')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
