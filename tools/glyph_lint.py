#!/usr/bin/env python3
"""Every non-ASCII character in a string literal of mods/coop is one the overlay font can draw
(BUG-0161). The overlay is a D3DX font created as Arial (sporekit/d3d9hook.cpp): it draws glyphs
from that one font, and a character outside it (U+2694 crossed swords, U+271D Latin cross) came
out as a missing-glyph box. Arial covers WGL4; the set below is the part of WGL4 the mod may use
(checked against arial.ttf and Liberation Sans with a cmap reader, 2026-10-28, fix/w3-civ).
Comments are not checked. Run by `make test`.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ALLOWED = set(range(0xA0, 0x100))                      # Latin-1 (Spanish, Western European)
ALLOWED |= set(range(0x100, 0x180))                    # Latin Extended-A (Hungarian ő/ű, Central European, WGL4)
ALLOWED |= (set(range(0x401, 0x460)) - {0x40D, 0x450, 0x45D}) | {0x490, 0x491}  # Cyrillic (the Russian UI)
ALLOWED |= {0x2013, 0x2018, 0x2019, 0x201C, 0x201D, 0x2020, 0x2021, 0x2022, 0x2026, 0x2030,
            0x2039, 0x203A, 0x20AC, 0x2116, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2194,
            0x2195, 0x221E, 0x2248, 0x2260, 0x2264, 0x2265, 0x25A0, 0x25A1, 0x25AA, 0x25AB,
            0x25AC, 0x25B2, 0x25BA, 0x25BC, 0x25C4, 0x25CA, 0x25CB, 0x25CF, 0x25D8, 0x25D9,
            0x25E6, 0x263A, 0x263B, 0x263C, 0x2640, 0x2642, 0x2660, 0x2663, 0x2665, 0x2666,
            0x266A, 0x266B}

# Simplified Chinese (zh) localization lives in mods/coop/i18n.h; SporeKit overlay uses a CJK fallback font
# (Microsoft YaHei / SimSun) whenever a string contains CJK codepoints (BUG-1575).
ALLOWED_CJK = (set(range(0x3000, 0x3040)) | set(range(0x4E00, 0xA000)) | set(range(0xFF00, 0xFFF0)))


def literals(line):
    code = re.split(r'//', line, maxsplit=1)[0]
    return re.findall(r'"((?:[^"\\]|\\.)*)"', code)


def main():
    bad = 0
    for f in sorted(glob.glob(os.path.join(ROOT, 'mods/coop/*.cpp')) + glob.glob(os.path.join(ROOT, 'mods/coop/*.h'))):
        rel = os.path.relpath(f, ROOT)
        ok_set = (ALLOWED | ALLOWED_CJK) if rel == os.path.join('mods', 'coop', 'i18n.h') else ALLOWED
        with open(f, encoding='utf-8') as fh:
            for n, line in enumerate(fh, 1):
                for lit in literals(line):
                    for ch in lit:
                        if ord(ch) > 0x7F and ord(ch) not in ok_set:
                            print(f'{rel}:{n}: U+{ord(ch):04X} {ch!r} is not in the overlay font (Arial/WGL4)')
                            bad += 1
    print(f'glyph_lint: {bad} problem(s)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
