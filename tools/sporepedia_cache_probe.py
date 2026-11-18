#!/usr/bin/env python3
"""Read native Sporepedia feed definitions without modifying the game.

Usage: python tools/sporepedia_cache_probe.py GAME --output out/cache-probe
The output contains only category/config property resources and their decoded
fields, for the dedicated Lineages Cache category prototype.
"""
import argparse
import json
import struct
from pathlib import Path

from part_substitutes import entries, read, parse

FIELDS = {
    0x0574D7F3: 'configFeedList', 0x744717C0: 'categories',
    0x744717C1: 'items', 0x744717C2: 'categoryName',
    0x744717C3: 'itemType', 0x744717C5: 'itemName',
    0x744717C6: 'queryType', 0x744717C7: 'subscriptionType',
    0x0662081E: 'keyList', 0x06678DF3: 'uniqueID',
    0x05E90865: 'rootFilter', 0x05E9086C: 'selectedFilter',
    0x06397993: 'visibleBuddiesOnly', 0x071D9ED4: 'groupID',
}


def encode(value):
    if isinstance(value, bytes):
        return value.hex()
    if isinstance(value, (tuple, list)):
        return [encode(x) for x in value]
    return value


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('game', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    # Keep source packs separate: precedence must be established before overrides.
    for folder in ('Data', 'DataEP1'):
        for pack in sorted((args.game / folder).glob('*.package')):
            if pack.name.startswith('Spore_Audio'):
                continue
            count = 0
            for entry in entries(str(pack)):
                if entry['type'] != 0x00B1B104:
                    continue
                data = read(str(pack), entry)
                try:
                    props = parse(data)
                except (KeyError, IndexError, ValueError, struct.error):
                    continue
                if not FIELDS.keys() & props.keys():
                    continue
                key = f"{entry['group']:08X}!{entry['inst']:08X}.prop"
                target = args.output / pack.stem / key
                target.parent.mkdir(exist_ok=True)
                target.write_bytes(data)
                rows.append({'pack': str(pack), 'key': key,
                             'properties': {FIELDS.get(k, f'{k:08X}'): encode(v)
                                            for k, v in props.items()}})
                count += 1
            print(f'{pack.name}: {count} feed/config resources')
    (args.output / 'feeds.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    print(f'{len(rows)} resources recorded in {args.output}')


if __name__ == '__main__':
    main()
