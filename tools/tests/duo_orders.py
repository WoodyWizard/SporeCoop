"""Exercise a guest village walk on two connected instances of the disposable Clyth save."""
import argparse
import math
import re
import time

from duo_villages import Console, command


def inspect(client):
    state = command(client, 'coop.tribe')
    tribes = {}
    for line in state.splitlines():
        header = re.match(r'tribe \d+.* species=([0-9A-F]+)', line)
        member = re.match(r'  #(\d+) \S+ owner=(\d+).* pos=\(([-.\d ]+)\)', line)
        if header:
            group = tribes.setdefault(header[1], {})
        elif member:
            group[int(member[1])] = (int(member[2]), tuple(map(float, member[3].split())))
    done = re.search(r'cmds_done=(\d+)', state)
    return tribes, int(done[1]) if done else None


def wait_until(probe, label, seconds=20):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        result = probe()
        if result:
            return result
        time.sleep(0.25)
    raise RuntimeError(label)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', required=True, type=int)
    p.add_argument('--guest', required=True, type=int)
    args = p.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    home, other = '067B3E4A', '0680BF38'
    for client in (host, guest):
        if "world 'Clyth.spo'" not in command(client, 'coop.avatar') or other not in command(client, 'coop.founders'):
            raise RuntimeError('This test requires the registered disposable Clyth planet.')
    if not command(host, 'coop.tribe village_orders on').startswith('Village walking orders enabled'):
        raise RuntimeError('The host did not enable the experimental orders.')
    try:
        def owned():
            for client in (host, guest):
                groups, _ = inspect(client)
                if len(groups.get(other, {})) != 3 or not groups.get(home):
                    return False
                if any(v[0] != 0 for v in groups[home].values()) or any(v[0] != 1 for v in groups[other].values()):
                    return False
            return True
        wait_until(owned, 'Expected only the second village to have guest ownership.')
        groups, before = inspect(host)
        foreign = next(iter(groups[home]))
        if 'selected=0' not in command(guest, f'coop.tribe select {foreign} 1'):
            raise RuntimeError('Guest could select a host citizen.')
        if 'selected: 3' not in command(guest, 'coop.tribe select_village'):
            raise RuntimeError('Guest did not select its three citizens.')
        start = {key: row[1] for key, row in groups[other].items()}
        center = [sum(v[j] for v in start.values()) / len(start) for j in range(3)]
        dest = [center[0] + 26, center[1], center[2] - 20]
        scale = math.hypot(*center) / math.hypot(*dest)
        dest = [v * scale for v in dest]
        command(guest, 'coop.tribe walk ' + ' '.join(f'{v:.2f}' for v in dest))
        wait_until(lambda: inspect(host)[1] == before + 3, 'Host did not execute exactly three commands.')
        def advanced(client):
            rows = inspect(client)[0].get(other, {})
            return all(k in rows and math.dist(start[k], dest) - math.dist(rows[k][1], dest) > 4 for k in start)
        for c in (host, guest):
            wait_until(lambda: advanced(c), 'The three villagers did not advance toward the destination.', 30)
        ui = command(guest, 'coop.uistate')
        viewport = re.search(r'screen (\d+) (\d+)', ui)
        if 'open=0' not in ui or not viewport:
            raise RuntimeError('Close the co-op window to test a game-window click.')
        w, h = map(int, viewport.groups())
        objects = [tuple(map(int, xy)) for xy in re.findall(r'(-?\d+) (-?\d+)$', command(guest, 'coop.targets objs'), re.M)]
        points = [(int(w * x), int(h * y)) for x in (.3, .4, .5, .6, .7) for y in (.4, .5, .6)]
        x, y = max(points, key=lambda p: min((math.dist(p, o) for o in objects), default=0))
        command(guest, 'cheat coop tribe march')
        command(guest, f'click {x} {y}')
        wait_until(lambda: inspect(host)[1] == before + 6, 'In-game march and ground click did not reach the host.')
        command(guest, f'click {x} {y}')
        time.sleep(.8)
        if inspect(host)[1] != before + 6:
            raise RuntimeError('A second click repeated a one-shot march.')
        command(guest, f'coop.creations choose creature {home}')
        def guest_unassigned():
            groups, _ = inspect(guest)
            return all(row[0] == 254 for row in groups[other].values())
        wait_until(guest_unassigned, 'Switching to another species did not relinquish control.')
        print('PASS: only the 3 guest citizens walked on both games. The in-game march accepted one click. Switching species revoked ownership.')
    finally:
        command(host, 'coop.tribe village_orders off')
        command(guest, f'coop.creations choose creature {other}')


if __name__ == '__main__':
    main()
