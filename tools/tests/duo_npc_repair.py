"""Seed an extra NPC village in a disposable guest, then verify the host world can repair it."""
import argparse
import math
import re
import time

from duo_npc_villages import roster
from duo_orders import wait_until
from duo_villages import Console, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--manual', action='store_true', help='ask the host to reshare after the divergence')
    args = p.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    for client in (host, guest):
        if "world 'Clyth.spo'" not in command(client, 'coop.avatar') or '0680BF38' not in command(client, 'coop.founders'):
            raise RuntimeError('Expected the disposable Clyth fixture with its two founders.')
    original = roster(host)
    if original != roster(guest):
        raise RuntimeError('The starting village rosters already differ.')
    command(guest, 'coop.creations choose creature 0680BF38')
    command(host, 'coop.tribe village_orders on')
    time.sleep(1)
    centers = [tuple(map(float, v.split())) for v in re.findall(r'^tribe \d+.*? at \(([-.\d ]+)\)', command(guest, 'coop.tribe'), re.M)]
    trees = re.findall(r'^([0-9A-Fa-f]+) d=\d+ pos=\(([-.\d ]+)\)', command(guest, 'coop.tribe trees 250'), re.M)
    if not centers or not trees:
        raise RuntimeError('Cannot identify a native spawn site for the test.')
    address = next((int(addr, 16) + 0x10 for addr, xyz in trees
                    if all(math.dist(tuple(map(float, xyz.split())), c) > 90 for c in centers)), None)
    if not address:
        raise RuntimeError('No native site is far enough away from the existing villages.')
    command(guest, f'call Simulator::SpawnNpcTribe cdecl 0x{address:X} 0 3 12 0 0')
    divergent = roster(guest)
    if len(divergent) < len(original) + 1 or (not args.manual and len(divergent) != len(original) + 1) or roster(host) != original:
        raise RuntimeError(f'Isolated spawn failed: host {roster(host)}, guest {divergent}')
    print(f'Seeded guest-only village: host {original}, guest {divergent}', flush=True)
    if args.manual:
        command(host, 'coop.share due')
    else:
        for _ in range(4):
            time.sleep(.8)
            if len(roster(guest)) != len(original) + 1:
                raise RuntimeError('The native controller formed additional guest villages, or an early reload occurred.')
    def restored():
        avatar = guest.call('coop.avatar')  # no controlled entity while the guest changes worlds
        return avatar['ok'] and "world 'Clyth.spo'" in avatar['out'] and roster(guest) == roster(host) == original
    wait_until(restored, 'Guest never recovered the original native villages.', 100)
    if command(host, 'coop.founders').strip() != command(guest, 'coop.founders').strip():
        raise RuntimeError('Founders changed after the repair.')
    print('PASS: host villages were untouched and the guest recovered its two registered villages.')


if __name__ == '__main__':
    main()
