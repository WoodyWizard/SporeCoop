#!/usr/bin/env python3
"""The save-slot key scan of the own-species tests (keys_in_slot, BUG-1326, in make test) on synthetic DBPF slots: it
finds a guest's line key and its raw creation key wherever a record holds them, also in a compressed record, and does
not count an id of the host's own resources that only has the same instance (the game's per-process id counter gives
both games the same ids when they start in the same second)."""
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_tribe_own_species as ts  # noqa: E402

CRT, GROUP, SCRIPT, SCRIPT_GROUP = 0x2B978C46, 0x40626200, 0x00B1B104, 0x4084AF00


def literal_refpack(raw):
    """RefPack with literal blocks only (what the scan must unpack; no key bytes in the file as they are)"""
    out = bytearray([0x10, 0xFB]) + len(raw).to_bytes(3, 'big')
    p = 0
    while len(raw) - p >= 4:
        n = min(112, (len(raw) - p) & ~3)
        out.append(0xE0 + (n - 4) // 4)
        out += raw[p:p + n]
        p += n
    out.append(0xFC + (len(raw) - p))
    out += raw[p:]
    return bytes(out)


def package(recs):
    """a DBPF 3.0 file: recs = [(type, group, instance, data, compress)]"""
    body = bytearray()
    index = bytearray(struct.pack('<I', 0))
    for t, g, i, d, comp in recs:
        stored = literal_refpack(d) if comp else d
        off = 96 + len(body)
        body += stored
        index += struct.pack('<IIIIIIIHH', t, g, 0, i, off, len(stored) | 0x80000000, len(d), 0xFFFF if comp else 0, 1)
    head = bytearray(96)
    head[:4] = b'DBPF'
    struct.pack_into('<II', head, 4, 3, 0)
    struct.pack_into('<I', head, 36, len(recs))
    struct.pack_into('<I', head, 44, len(index))
    struct.pack_into('<I', head, 64, 96 + len(body))
    return bytes(head + body + index)


def slot(files):
    d = tempfile.mkdtemp(prefix='savekeys_')
    for name, recs in files.items():
        with open(os.path.join(d, name), 'wb') as f:
            f.write(package(recs))
    return d


def main():
    ok = True
    line, raw, host_id = 0x7B519F39, 0x29293F61, 0x29293F61
    filler = bytes(range(256)) * 2

    def check(what, cond):
        nonlocal ok
        print(('ok   ' if cond else 'FAIL ') + what)
        ok &= bool(cond)

    # 1. the host's own planet script under the same instance as the guest's creation: not a key of the guest's
    d = slot({'lastSave.pld': [(SCRIPT, SCRIPT_GROUP, host_id, filler, True),
                               (SCRIPT, SCRIPT_GROUP, 0x2900C777, struct.pack('<I', host_id) + filler, True)]})
    own = set()
    hits = ts.keys_in_slot(d, [line, raw], own)
    check(f'the host\'s own resource id is not the guest\'s key ({hits}, own ids {sorted(own)})', not hits and own == {raw})
    # 2. the line key in a compressed record (big-endian, the stream's group, type, instance): found
    d = slot({'Zorispe.spo': [(0x018EB4B7, 0, 1, filler + struct.pack('>III', GROUP, CRT, line) + filler, True)]})
    hits = ts.keys_in_slot(d, [line, raw])
    check(f'the line key in a compressed record is found ({hits})', hits == ['Zorispe.spo:7B519F39 (records 018EB4B7)'])
    # 3. the raw creation key as a key ({instance, type, group}) in a record of a slot that also has a host id of that
    # instance: found
    d = slot({'lastSave.pld': [(SCRIPT, SCRIPT_GROUP, host_id, filler, True)],
              'Zorispe.spo': [(0x01897C18, 0, 2, filler + struct.pack('<III', raw, CRT, GROUP), False)]})
    hits = ts.keys_in_slot(d, [raw])
    check(f'the raw creation key beside its type is found ({hits})', hits == ['Zorispe.spo:29293F61 (records 01897C18)'])
    # 4. the raw creation instance alone in a slot where no host resource has that id: found (nothing explains it)
    d = slot({'Zorispe.spo': [(0x01897C18, 0, 2, filler + struct.pack('<I', raw) + filler, True)]})
    hits = ts.keys_in_slot(d, [raw])
    check(f'the raw instance without a host id of it is found ({hits})', hits == ['Zorispe.spo:29293F61 (records 01897C18)'])
    # 5. the guest's creation itself as a resource of the host's slot (its index): found
    d = slot({'planetRecords.pkp': [(CRT, GROUP, raw, filler, False)]})
    hits = ts.keys_in_slot(d, [raw])
    check(f'the guest\'s creation as a resource of the slot is found ({hits})', hits == ['planetRecords.pkp:29293F61 (records 2B978C46)'])
    print('savekeys selftest:', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
