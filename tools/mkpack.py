#!/usr/bin/env python3
"""Build the update pack of a release (format: updater/pack.h) from the staged zip contents:
the files an update installs, paths relative to the game folder, each with its SHA-256.

  mkpack.py <stage dir of tools/package.sh> <version> <out.pack>
  mkpack.py --server <dir with the galaxy server's files> <version> <out.pack>

--server (decision 8Y C): the pack of the optional galaxy server, which the setup carries beside the game's pack and
installs only when asked. Every file of the folder goes to SporebinEP1/mods/galaxy-server/; galaxy-settings.cmd is
the host's own file (installed only when missing). File names: letters, digits, '_', '-' and '.' (updater/pack.cpp).
"""
import glob
import hashlib
import os
import struct
import sys

ONLY_IF_MISSING = 1
SERVER_PREFIX = 'SporebinEP1/mods/galaxy-server/'  # installer/install.h kServerPrefix
SERVER_KEEP = ('galaxy-settings.cmd',)


def write(entries, version, out):
    s16 = lambda b: struct.pack('<H', len(b)) + b  # noqa: E731
    head = b'SCUP' + struct.pack('<I', 1) + s16(version.encode()) + struct.pack('<I', len(entries))
    for path, data, flags in entries:
        head += s16(path.encode()) + bytes([flags]) + struct.pack('<I', len(data)) + hashlib.sha256(data).digest()
    with open(out, 'wb') as o:
        o.write(head)
        for _, data, _ in entries:
            o.write(data)
    print(out, len(entries), 'files')


def server(src, version, out):
    names = sorted(n for n in os.listdir(src) if os.path.isfile(os.path.join(src, n)))
    if 'galaxy_server.exe' not in names:
        sys.exit('mkpack --server: no galaxy_server.exe in ' + src)
    write([(SERVER_PREFIX + n, open(os.path.join(src, n), 'rb').read(), ONLY_IF_MISSING if n in SERVER_KEEP else 0)
           for n in names], version, out)


def main():
    if sys.argv[1] == '--server':
        return server(sys.argv[2], sys.argv[3], sys.argv[4])
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
    write(entries, version, out)


if __name__ == '__main__':
    main()
