#!/usr/bin/env python3
"""Build the experimental native Lineages Cache sidebar package.

Consumes the read-only cache probe output. Does not install anything. An optional
JSON list of [instance, type, group] integer keys supplies the explicit feed.
Runtime support, import and synchronization are separate from this UI prototype.
"""
import argparse
import json
import struct
from pathlib import Path
from tempfile import TemporaryDirectory

from mkpackage import build, fnv
from part_substitutes import parse

CATEGORY = fnv('LineagesCacheCategory')
ITEM = fnv('LineagesCacheItems')
LOCALE = fnv('LineagesCacheLabels')
ROOTS = ('2EA8FB98', '1B327962', 'FDECEF02')


def key(instance, typ=0, group=0):
    return struct.pack('<III', instance, typ, group)


def array(pid, typ, values, size):
    if any(len(v) != size for v in values):
        raise ValueError('Invalid property element size')
    return struct.pack('>IHHII', pid, typ, 0x30, len(values), size) + b''.join(values)


def text(pid, value):
    encoded = value.encode('utf-16le')
    if len(encoded) > 510:
        raise ValueError('Localized text is too long')
    # This browser resolves locale references; it ignores the embedded fallback.
    return array(pid, 0x22, [struct.pack('<II', LOCALE, fnv(value)) + encoded + bytes(512 - len(encoded))], 520)


def props(*fields):
    return struct.pack('>I', len(fields)) + b''.join(sorted(fields, key=lambda x: x[:4]))


def extend_root(data):
    """Only accept the observed root shape; fail rather than dropping unknown fields."""
    parsed = parse(data)
    if set(parsed) - {0x744717C0, 0xEA7E4C1C}:
        raise ValueError('Unrecognized feed root fields')
    typ, shape, raw, count, size = parsed[0x744717C0]
    if (typ, shape, size) != (0x20, 'arr', 12) or len(raw) != count * size:
        raise ValueError('Unrecognized categories array')
    values = [raw[i:i+12] for i in range(0, len(raw), 12)]
    if key(CATEGORY) not in values:
        values.append(key(CATEGORY))
    fields = [array(0x744717C0, 0x20, values, 12)]
    if 0xEA7E4C1C in parsed:
        typ, value = parsed[0xEA7E4C1C]
        if typ != 1 or value not in (0, 1):
            raise ValueError('Unexpected hide-header property')
        fields.append(struct.pack('>IHHB', 0xEA7E4C1C, 1, 0, value))
    return props(*fields)


def cache_records(keys=()):
    """Standalone resources; the DLL appends to live roots, preserving other mods."""
    records = {}
    records[f'0x06B38241!0x{CATEGORY:08X}.prop'] = props(
        struct.pack('>IHH', 0x00B2CCCB, 0x20, 0) + key(0x4529F96F, 0x00B1B104, 0x06B38241) + bytes(4),
        text(0x744717C2, 'Lineages Cache'),
        array(0x744717C1, 0x20, [key(ITEM)], 12),
        struct.pack('>IHHB', 0x06397993, 1, 0, 1))
    # Use the native keylist_Template item type. Rare Statues also has a key list,
    # but is FeedItemMessage and dispatches a game-specific message instead.
    records[f'0x4E5892EB!0x{ITEM:08X}.prop'] = props(
        struct.pack('>IHH', 0x00B2CCCB, 0x20, 0) + key(0xFE003A17, 0x00B1B104, 0x4E5892EB) + bytes(4),
        text(0x744717C5, 'Shared Creations'),
        struct.pack('>IHH', 0x744717C3, 0x20, 0) + key(fnv('FeedItemKeyList')) + bytes(4),
        array(0x0662081E, 0x20, [key(*k) for k in keys], 12))
    records[f'0x02FABF01!0x{LOCALE:08X}.0x02FAC0B6'] = (
        '\ufeff' + ''.join(f'0x{fnv(label):08X} {label}\r\n' for label in ('Lineages Cache', 'Shared Creations'))
    ).encode('utf-8')
    return records


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('probe', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--keys', type=Path)
    args = ap.parse_args()
    keys = json.loads(args.keys.read_text(encoding='utf-8')) if args.keys else []
    if not isinstance(keys, list) or len(keys) > 10000:
        raise ValueError('Expected at most 10000 asset keys')
    for k in keys:
        if not isinstance(k, list) or len(k) != 3 or any(type(n) is not int or not 0 <= n <= 0xFFFFFFFF for n in k):
            raise ValueError('Expected [instance, type, group] uint32 keys')
    keys = sorted(set(tuple(k) for k in keys))
    records = {}
    for root in ROOTS:
        name = f'6EDC12D4!{root}.prop'
        # These exact roots were observed in base/PatchData and GA respectively.
        pack = 'PatchData' if root == '2EA8FB98' else 'Spore_EP1_Data'
        records[f'0x6EDC12D4!0x{root}.prop'] = extend_root((args.probe / pack / name).read_bytes())
    records.update(cache_records(keys))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory() as tmp:
        specs = []
        for i, (resource, data) in enumerate(records.items()):
            if resource.endswith('.prop'):
                parse(data)  # refuse malformed property output before creating a package
            path = Path(tmp) / f'{i}.prop'
            path.write_bytes(data)
            specs.append(f'{resource}={path}')
        build(str(args.output), specs)
    print(f'Experimental category built with {len(keys)} explicit assets; not installed.')


if __name__ == '__main__':
    main()
