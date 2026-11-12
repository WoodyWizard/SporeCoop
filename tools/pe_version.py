#!/usr/bin/env python3
"""The VERSIONINFO resource of Windows binaries (BUG-0784), without Windows or llvm-readobj.

  tools/pe_version.py <file.exe|dll>...                  print each file's version strings
  tools/pe_version.py --expect X.Y.Z <file>...           exit 1 unless every file has a VERSIONINFO
                                                         whose FileVersion / ProductVersion strings are
                                                         X.Y.Z and whose fixed FILEVERSION is X,Y,Z(,0)
  tools/pe_version.py --repro-sha256 <file>...          sha256 with the link time stamps and the debug
                                                         record's id zeroed: two builds of the same commit
                                                         (stripped like tools/package.sh) give the same value
tools/package.sh runs --expect on every binary of the release (the zip, the pack and the setup).
"""
import hashlib
import struct
import sys

RT_VERSION = 16


def _u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def _u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def version_resource(data):
    """the raw VS_VERSIONINFO bytes of a PE image, or None"""
    if len(data) < 0x40 or data[:2] != b'MZ':
        return None
    pe = _u32(data, 0x3C)
    if data[pe:pe + 4] != b'PE\0\0':
        return None
    nsec, optsz = _u16(data, pe + 6), _u16(data, pe + 20)
    opt = pe + 24
    magic = _u16(data, opt)
    dd = opt + (96 if magic == 0x10B else 112)
    rsrc_rva = _u32(data, dd + 2 * 8)
    if not rsrc_rva:
        return None
    secs = []
    for i in range(nsec):
        o = opt + optsz + 40 * i
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, o + 8)
        secs.append((va, max(vsize, rsize), raw))

    def off(rva):
        for va, size, raw in secs:
            if va <= rva < va + size:
                return raw + rva - va
        return None
    base = off(rsrc_rva)
    if base is None:
        return None

    def entries(d):
        n = _u16(data, d + 12) + _u16(data, d + 14)
        return [struct.unpack_from('<II', data, d + 16 + 8 * i) for i in range(n)]
    for name, sub in entries(base):
        if name != RT_VERSION or not sub & 0x80000000:
            continue
        d = base + (sub & 0x7FFFFFFF)
        while True:  # the first name, the first language
            e = entries(d)
            if not e:
                return None
            nxt = e[0][1]
            if nxt & 0x80000000:
                d = base + (nxt & 0x7FFFFFFF)
                continue
            rva, size = struct.unpack_from('<II', data, base + nxt)
            o = off(rva)
            return data[o:o + size] if o is not None else None
    return None


def parse(vi):
    """(fixed (a, b, c, d) or None, {string name: value}) of a VS_VERSIONINFO"""
    def node(o):
        """length, value length, type, key, value offset, children offset of the node at o"""
        length, vlen, typ = struct.unpack_from('<HHH', vi, o)
        k = e = o + 6
        while vi[e:e + 2] != b'\0\0':
            e += 2
        val = (e + 2 + 3) & ~3
        kids = (val + vlen * (2 if typ == 1 else 1) + 3) & ~3
        return length, vlen, typ, vi[k:e].decode('utf-16-le'), val, kids

    def children(o):
        length, _, _, _, _, c = node(o)
        while c + 6 <= o + length:
            n = node(c)
            if n[0] == 0:
                break
            yield c, n
            c = (c + n[0] + 3) & ~3
    length, vlen, _, key, v, _ = node(0)
    if key != 'VS_VERSION_INFO':
        return None, {}
    fixed = None
    if vlen >= 52 and _u32(vi, v) == 0xFEEF04BD:
        ms, ls = _u32(vi, v + 8), _u32(vi, v + 12)
        fixed = (ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF)
    strings = {}
    for o1, n1 in children(0):
        if n1[3] != 'StringFileInfo':
            continue
        for o2, _ in children(o1):  # a StringTable per language
            for _, (ln, vl, typ, k, val, _) in children(o2):
                strings[k] = vi[val:val + 2 * vl].decode('utf-16-le').rstrip('\0')
    return fixed, strings


def repro_bytes(data):
    """the image with the fields that change on every link zeroed: the COFF header's TimeDateStamp, each
    debug directory entry's TimeDateStamp and the CodeView (RSDS) record's GUID and age"""
    b = bytearray(data)
    if len(b) < 0x40 or b[:2] != b'MZ' or b[_u32(b, 0x3C):_u32(b, 0x3C) + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    pe = _u32(b, 0x3C)
    struct.pack_into('<I', b, pe + 8, 0)
    opt = pe + 24
    dd = opt + (96 if _u16(b, opt) == 0x10B else 112)
    dbg_rva, dbg_size = struct.unpack_from('<II', b, dd + 6 * 8)
    nsec, optsz = _u16(b, pe + 6), _u16(b, pe + 20)
    for i in range(nsec):
        o = opt + optsz + 40 * i
        vsize, va, rsize, raw = struct.unpack_from('<IIII', b, o + 8)
        if dbg_rva and va <= dbg_rva < va + max(vsize, rsize):
            d = raw + dbg_rva - va
            for e in range(d, d + dbg_size, 28):
                struct.pack_into('<I', b, e + 4, 0)
                ptr = _u32(b, e + 24)
                if _u32(b, e + 12) == 2 and b[ptr:ptr + 4] == b'RSDS':
                    b[ptr + 4:ptr + 24] = bytes(20)
    return bytes(b)


def read(path):
    with open(path, 'rb') as f:
        vi = version_resource(f.read())
    return parse(vi) if vi else (None, None)


def main(argv):
    expect = None
    if argv[:1] == ['--repro-sha256']:
        for p in argv[1:]:
            with open(p, 'rb') as f:
                print(hashlib.sha256(repro_bytes(f.read())).hexdigest(), p)
        return 0
    if argv[:1] == ['--expect']:
        expect, argv = argv[1], argv[2:]
    if not argv:
        print(__doc__)
        return 2
    bad = 0
    for p in argv:
        fixed, strings = read(p)
        if strings is None:
            print(f'{p}: no VERSIONINFO resource')
            bad += 1
            continue
        print(f'{p}: {".".join(map(str, fixed)) if fixed else "?"} {strings.get("FileDescription", "")!r} '
              f'({strings.get("OriginalFilename", "")}, {strings.get("ProductName", "")} {strings.get("ProductVersion", "")})')
        if expect:
            want = tuple(int(x) for x in expect.split('.')) + (0,) * (4 - len(expect.split('.')))
            problems = []
            if fixed != want:
                problems.append(f'FILEVERSION {fixed} is not {want}')
            for k in ('FileVersion', 'ProductVersion'):
                if strings.get(k) != expect:
                    problems.append(f'{k} {strings.get(k)!r} is not {expect!r}')
            for k in ('CompanyName', 'FileDescription', 'OriginalFilename', 'ProductName', 'LegalCopyright'):
                if not strings.get(k):
                    problems.append(f'no {k}')
            for m in problems:
                print(f'  FAIL {m}')
            bad += bool(problems)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
