"""List base paint IDs in shipped Cell BEM records for native landfall compatibility.

Read-only: pass the stock Data/Spore_Content.package. These paints can survive
the game's own Cell-to-Creature conversion even without a skin-effect record.
"""
import struct
import sys
from part_substitutes import entries, read


def header(data):
    out = bytearray()
    cursor = 0
    while len(out) < 38:
        if cursor >= len(data): raise ValueError('Truncated model header')
        value = data[cursor]
        cursor += 1
        if value != 0xFE:
            out.append(value)
            continue
        if cursor >= len(data): raise ValueError('Truncated model run')
        run = data[cursor]
        cursor += 1
        if run:
            if not out: raise ValueError('Run without preceding byte')
            out.extend([out[-1]] * min(run, 38-len(out)))
        else: out.append(0xFE)
    if out[:4] != bytes.fromhex('d37e6548'): raise ValueError('Wrong model magic')
    return struct.unpack_from('<III', out, 26)


if __name__ == '__main__':
    paints = set()
    models = 0
    for entry in entries(sys.argv[1]):
        if entry['type'] != 0x1A99B06B or entry['group'] != 0x40616200: continue
        base, coat, detail = header(read(sys.argv[1], entry))
        if coat or detail: raise ValueError('Unexpected Cell paint layout')
        if base: paints.add(base)
        models += 1
    print(f'// {models} stock Cell models; {len(paints)} distinct nonzero base paints')
    print(', '.join(f'0x{value:08X}' for value in sorted(paints)))
