#!/usr/bin/env python3
"""Generate Sporepedia category, feed item, and locale resources for the Shared Creations tab (BUG-0873).

Writes three files into <out_dir>:
  - library.locale  (0x02FABF01!0xA7C9F2B2.0x02FAC0B6, UTF-8 with BOM)
  - category.prop   (0x06B38241!0x11C77D4C.0x00B1B104, FeedCategory)
  - item.prop       (0x4E5892EB!0x0BA43291.0x00B1B104, FeedItem KeyList)
"""
import os
import struct
import sys

LOCALE_ID = 0xA7C9F2B2  # fnv("sporecoop_shared_library_locale")
CAT_ID = 0x11C77D4C     # fnv("sporecoop_shared_library_category")
ITEM_ID = 0x0BA43291    # fnv("sporecoop_shared_library_item")


def prop_key_scalar(pid, inst, typ, grp):
    return struct.pack('>IHH', pid, 0x20, 0x0000) + struct.pack('<IIII', inst, typ, grp, 0)


def prop_bool_scalar(pid, val):
    return struct.pack('>IHHB', pid, 0x01, 0x0000, 1 if val else 0)


def prop_key_array(pid, keys):
    out = bytearray(struct.pack('>IHHII', pid, 0x20, 0x009C, len(keys), 12))
    for inst, typ, grp in keys:
        out += struct.pack('<III', inst, typ, grp)
    return bytes(out)


def prop_text_array(pid, table_id, text_id, fallback):
    enc = fallback.encode('utf-16le')[:510]
    padded = enc + b'\x00' * (512 - len(enc))
    return struct.pack('>IHHII', pid, 0x22, 0x001C, 1, 520) + struct.pack('<II', table_id, text_id) + padded


def pack_props(items):
    # Property lists in Spore are sorted by property id ascending
    items = sorted(items, key=lambda kv: kv[0])
    out = bytearray(struct.pack('>I', len(items)))
    for _, raw in items:
        out += raw
    return bytes(out)


def build(out_dir):
    os.makedirs(out_dir, exist_ok=True)

    # 1. Locale table (UTF-8 with BOM, CRLF)
    locale_text = (
        '\ufeff'
        '# SporeCoop Shared Creations library (BUG-0873)\r\n'
        '0x00000001 Shared Creations\r\n'
        '0x00000002 Shared Creations\r\n'
    ).encode('utf-8')
    with open(os.path.join(out_dir, 'library.locale'), 'wb') as f:
        f.write(locale_text)

    # 2. FeedCategory (group 0x06B38241, parent 0x4529F96F)
    cat_prop = pack_props([
        (0x00B2CCCB, prop_key_scalar(0x00B2CCCB, 0x4529F96F, 0x00B1B104, 0x06B38241)),
        (0x744717C1, prop_key_array(0x744717C1, [(ITEM_ID, 0, 0)])),
        (0x744717C2, prop_text_array(0x744717C2, LOCALE_ID, 0x00000001, 'Shared Creations')),
    ])
    with open(os.path.join(out_dir, 'category.prop'), 'wb') as f:
        f.write(cat_prop)

    # 3. FeedItem (group 0x4E5892EB, parent 0xFCAA7702, type 0x48A6F111 KeyList)
    item_prop = pack_props([
        (0x00B2CCCB, prop_key_scalar(0x00B2CCCB, 0xFCAA7702, 0x00B1B104, 0x4E5892EB)),
        (0x06397993, prop_bool_scalar(0x06397993, True)),
        (0x744717C3, prop_key_scalar(0x744717C3, 0x48A6F111, 0, 0)),
        (0x744717C5, prop_text_array(0x744717C5, LOCALE_ID, 0x00000002, 'Shared Creations')),
        (0x74B839B7, prop_bool_scalar(0x74B839B7, True)),
        (0xF4906970, prop_key_scalar(0xF4906970, 0x46B9F009, 0x2F7D0004, 0xCA14DE92)),
    ])
    with open(os.path.join(out_dir, 'item.prop'), 'wb') as f:
        f.write(item_prop)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print('usage: gen_library_res.py <out_dir>', file=sys.stderr)
        sys.exit(1)
    build(sys.argv[1])
