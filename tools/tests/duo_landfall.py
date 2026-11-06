"""Compare the first guest Creature editor to a genuine solo landfall of the same cell."""
import argparse
import hashlib
import re
import struct
from pathlib import Path

from duo_villages import Console, command
from part_substitutes import entries, read


def editor(c):
    s = command(c, 'coop.editor')
    m = re.search(r'editor FD4902BD key ([0-9A-F]{8})\b', s)
    if not m:
        raise RuntimeError('Open the early Creature editor on each participant.')
    return int(m[1], 16)


def records(database, key):
    result = {}
    for entry in entries(database):
        if entry['inst'] == key and entry['group'] == 0x40626200:
            result[entry['type']] = read(database, entry)
    if 0x1A99B06B not in result:
        raise RuntimeError(f'Missing native editor model {key:08X} in {database.name}')
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--guest-library', type=Path, required=True)
    p.add_argument('--host-library', type=Path, required=True)
    p.add_argument('--control-library', type=Path, required=True)
    p.add_argument('--control-key', type=lambda s: int(s, 16), required=True)
    args = p.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    home, own = editor(host), editor(guest)
    generated = records(args.guest_library, own)
    control = records(args.control_library, args.control_key)
    bem, meta, png = 0x1A99B06B, 0x030BDEE3, 0x2F7D0004
    if generated[bem] != control[bem] or generated[bem] == records(args.host_library, home)[bem]:
        raise RuntimeError("The guest's actual editor loaded different geometry, parts or paint.")
    if generated[png] != control[png] or generated[meta][24:36] != control[meta][24:36]:
        raise RuntimeError('The thumbnail or parent cell in the catalog differs from the native control.')
    if struct.unpack_from('>III', generated[meta], 12) != (0x2B978C46, 0x40626200, own):
        raise RuntimeError('The new catalog entry points to another asset.')
    for c in (host, guest):
        if not re.search(r'total faults: 0\b', command(c, 'exc')):
            raise RuntimeError('The game reported a first-chance fault.')
    print(f'First editors: host {home:08X}, guest {own:08X}.')
    print(f'PASS: {len(generated[bem])} model bytes, thumbnail and parent match the native control.')
    print(f'SHA256 {hashlib.sha256(generated[bem]).hexdigest()}.')


if __name__ == '__main__':
    main()
