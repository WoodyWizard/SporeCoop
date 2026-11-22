#!/usr/bin/env python3
"""Build the update pack of a release (format: updater/pack.h) from the staged zip contents:
the files an update installs, paths relative to the game folder, each with its SHA-256.

  mkpack.py <stage dir of tools/package.sh> <version> <out.pack>
"""
import glob
import hashlib
import os
import struct
import sys

ONLY_IF_MISSING = 1


def main():
    stage, version, out = sys.argv[1], sys.argv[2], sys.argv[3]
    src = os.path.join(stage, 'SporeCoop')
    files = [('SporebinEP1/steam_api.dll', os.path.join(src, 'steam_api.sporekit.dll'), 0),
             ('SporebinEP1/sporekit.dll', os.path.join(src, 'sporekit.dll'), 0),
             ('SporebinEP1/mods/coop.dll', os.path.join(src, 'mods', 'coop.dll'), 0),
             ('Data/SporeCoop.package', os.path.join(src, 'SporeCoop.package'), 0)]
    for f in sorted(glob.glob(os.path.join(src, 'mods', 'sporekit', 'symbols', '*.sym'))):
        files.append(('SporebinEP1/mods/sporekit/symbols/' + os.path.basename(f), f, 0))
    for f in sorted(glob.glob(os.path.join(src, 'mods', 'lineages', '*.txt'))):  # BUG-9329: the ban list (always the release's)
        files.append(('SporebinEP1/mods/lineages/' + os.path.basename(f), f, 0))
    for f in sorted(glob.glob(os.path.join(src, 'mods', '*.ini'))):  # the player's settings stay
        files.append(('SporebinEP1/mods/' + os.path.basename(f), f, ONLY_IF_MISSING))
    entries = [(path, open(f, 'rb').read(), flags) for path, f, flags in files]
    entries.append(('SporebinEP1/mods/sporecoop.version', (version + '\n').encode(), 0))
    s16 = lambda b: struct.pack('<H', len(b)) + b  # noqa: E731
    head = b'SCUP' + struct.pack('<I', 1) + s16(version.encode()) + struct.pack('<I', len(entries))
    for path, data, flags in entries:
        head += s16(path.encode()) + bytes([flags]) + struct.pack('<I', len(data)) + hashlib.sha256(data).digest()
    with open(out, 'wb') as o:
        o.write(head)
        for _, data, _ in entries:
            o.write(data)
    print(out, len(entries), 'files')


if __name__ == '__main__':
    main()
