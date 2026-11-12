#!/usr/bin/env python3
"""The Large Address Aware flag of SporeApp.exe (BUG-0862): inspect it, set it with a verified backup,
restore the backup. Optional and never done by the setup or the mod: only when a player runs it.

  tools/laa.py status  <SporeApp.exe>            the flag, the PE checksum, the sha256, the backup
  tools/laa.py set     <SporeApp.exe> [--force]  backup to <exe>.pre-laa (+ .sha256), then set the flag
  tools/laa.py restore <SporeApp.exe>            put the verified backup back (and remove it)

Spore is a 32-bit program: without the flag it gets 2 GB of address space, with it 4 GB on a 64-bit
Windows (and under Wine). Big worlds and adventures can run out of 2 GB (BUG-0631, BUG-0621).

`set` changes exactly two fields of the PE header: FileHeader.Characteristics gets
IMAGE_FILE_LARGE_ADDRESS_AWARE (0x0020), and OptionalHeader.CheckSum is recomputed (left 0 when it
was 0). Every other byte stays; the tool checks that before it replaces the file, and writes the new
file next to the old one first (no half written exe). SporeKit finds its symbols for such an exe by
its code_sha256 (the header is not part of it), so the mod works the same. An exe with a .bind
section (Steam's DRM wrapper) is refused without --force: the wrapper may check its own header.
Close Spore first. Exit code 0 = done (or nothing to do for status), 1 = refused or failed.
"""
import hashlib
import os
import shutil
import struct
import sys

LAA = 0x0020
BACKUP = '.pre-laa'


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def header(data):
    """(offset of FileHeader.Characteristics, offset of OptionalHeader.CheckSum, section names) or an error"""
    if len(data) < 0x40 or data[:2] != b'MZ':
        raise ValueError('not an exe (no MZ header)')
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    if struct.unpack_from('<H', data, pe + 4)[0] != 0x14C:
        raise ValueError('not a 32-bit x86 exe (the flag only matters for 32-bit programs)')
    opt = pe + 24
    if struct.unpack_from('<H', data, opt)[0] != 0x10B:
        raise ValueError('not a PE32 optional header')
    nsec, optsz = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    names = [data[opt + optsz + 40 * i:opt + optsz + 40 * i + 8].rstrip(b'\0').decode('latin-1') for i in range(nsec)]
    return pe + 22, opt + 64, names


def pe_checksum(data, ck_off):
    """the Windows PE checksum (as ImageHlp's CheckSumMappedFile): 16-bit sums with the field as 0, plus the size"""
    b = bytearray(data)
    b[ck_off:ck_off + 4] = bytes(4)
    if len(b) % 2:
        b.append(0)
    s = 0
    for (w,) in struct.iter_unpack('<H', bytes(b)):
        s += w
        s = (s & 0xFFFF) + (s >> 16)
    s = (s & 0xFFFF) + (s >> 16)
    return (s + len(data)) & 0xFFFFFFFF


def flags(data):
    ch_off, ck_off, names = header(data)
    ch = struct.unpack_from('<H', data, ch_off)[0]
    ck = struct.unpack_from('<I', data, ck_off)[0]
    return ch, ck, names, ch_off, ck_off


def read(p):
    with open(p, 'rb') as f:
        return f.read()


def write_verified(path, data):
    """data to path through a temporary file next to it, read back and compared before the rename"""
    tmp = path + '.laa-tmp'
    with open(tmp, 'wb') as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    if read(tmp) != data:
        os.remove(tmp)
        raise OSError(f'{tmp}: the written file differs')
    try:
        shutil.copymode(path, tmp)
    except OSError:
        pass
    os.replace(tmp, path)


def status(exe):
    data = read(exe)
    ch, ck, names, _, ck_off = flags(data)
    print(f'{exe}: Large Address Aware: {"yes" if ch & LAA else "no"} (Characteristics 0x{ch:04x})')
    print(f'  PE checksum 0x{ck:08x} ({"valid" if ck == pe_checksum(data, ck_off) else "0, not used" if ck == 0 else "does not match"})')
    print(f'  sha256 {sha256(data)}')
    if '.bind' in names:
        print('  has a .bind section (Steam DRM wrapper): set needs --force')
    b = exe + BACKUP
    if os.path.exists(b):
        want = open(b + '.sha256').read().split()[0] if os.path.exists(b + '.sha256') else ''
        ok = want and sha256(read(b)) == want
        print(f'  backup {b}: {"verified" if ok else "DOES NOT MATCH its .sha256" if want else "no .sha256 next to it"}')
    return 0


def set_flag(exe, force=False):
    data = read(exe)
    ch, ck, names, ch_off, ck_off = flags(data)
    if ch & LAA:
        print(f'{exe}: already Large Address Aware: nothing changed')
        return 0
    if '.bind' in names and not force:
        print(f'{exe}: has a .bind section (Steam\'s DRM wrapper), which may check its own header: refused '
              '(--force to set it anyway; restore puts the original back)')
        return 1
    b = exe + BACKUP
    if os.path.exists(b):
        print(f'{b} exists already (an earlier set?): refused. Run restore first, or move it away.')
        return 1
    # 1. the verified backup
    h = sha256(data)
    write_new = b + '.laa-tmp'
    with open(write_new, 'wb') as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())
    if sha256(read(write_new)) != h:
        os.remove(write_new)
        print(f'{b}: the backup reads back different: refused, nothing changed')
        return 1
    os.replace(write_new, b)
    with open(b + '.sha256', 'w') as f:
        f.write(f'{h}  {os.path.basename(exe)}\n')
    # 2. the flag and the checksum; nothing else may differ
    new = bytearray(data)
    struct.pack_into('<H', new, ch_off, ch | LAA)
    if ck:
        struct.pack_into('<I', new, ck_off, pe_checksum(bytes(new), ck_off))
    diff = [i for i in range(len(data)) if data[i] != new[i]]
    allowed = set(range(ch_off, ch_off + 2)) | set(range(ck_off, ck_off + 4))
    if len(new) != len(data) or not set(diff) <= allowed:
        print('internal error: more than the two header fields would change: refused, nothing changed')
        return 1
    write_verified(exe, bytes(new))
    print(f'{exe}: Large Address Aware set (Characteristics 0x{ch:04x} -> 0x{ch | LAA:04x}'
          + (f', checksum 0x{ck:08x} -> 0x{struct.unpack_from("<I", new, ck_off)[0]:08x}' if ck else '') + ')')
    print(f'  backup: {b} (sha256 {h})')
    return 0


def restore(exe):
    b = exe + BACKUP
    if not os.path.exists(b) or not os.path.exists(b + '.sha256'):
        print(f'no backup {b} (with its .sha256): nothing to restore')
        return 1
    want = open(b + '.sha256').read().split()[0]
    data = read(b)
    if sha256(data) != want:
        print(f'{b} does not match its .sha256: refused (the backup is damaged; reinstall or verify the game files)')
        return 1
    write_verified(exe, data)
    if sha256(read(exe)) != want:
        print(f'{exe}: the restored file differs from the backup: the backup is kept')
        return 1
    os.remove(b)
    os.remove(b + '.sha256')
    print(f'{exe}: the original is back (sha256 {want})')
    return 0


def main(argv):
    if len(argv) < 2 or argv[0] not in ('status', 'set', 'restore'):
        print(__doc__)
        return 1
    exe = argv[1]
    try:
        if argv[0] == 'status':
            return status(exe)
        if argv[0] == 'set':
            return set_flag(exe, '--force' in argv[2:])
        return restore(exe)
    except (OSError, ValueError) as e:
        print(f'{exe}: {e}')
        return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
