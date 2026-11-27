#!/usr/bin/env python3
"""Static guard linter for SporeCoop native memory/vcall footguns.

Scans mods/**/*.cpp and mods/**/*.h for:
  1. Off-by-N / under-sized `readable(...)` guards:
     - `readable(base + X, N)` immediately followed (same statement or guarded next 2 lines)
       by `F<T>(base, Y)`, `field<T>(base, Y)`, or `*(T*)(base + Y)` where `Y < X` or
       `Y + sizeof(T) > X + N`.
     - `readable(base, N)` immediately followed (same statement or guarded next 2 lines)
       by `F<T>(base, Y)`, `field<T>(base, Y)`, or `*(T*)(base + Y)` where `Y + sizeof(T) > N`.
  2. Suspicious `vcall` cast calls:
     - Raw hex byte offset passed as vtable index without `/ 4` (e.g. `vcall<void*>(p, 0x0C, ...)`).
     - `vcall<void*>(spatial_ptr, 0x0C / 4, <typeId>)` on a `cSpatialObject*` pointer
       (`spatial`, `game::spatial(...)`, or `+ 0x70` / `+ 0xC0` subobject), where `Cast` lives
       at vtable `+0xB8` rather than `+0x0C` (unless annotated with `// lint:ok-vcall-0c` or
       an explicit `COOP_FIX` off-branch paired with `+0xB8` / `kSpatialCastVtableSlot`).

Run by `make test` (`unit-lint`). Supports `--self-test` to verify detection and zero false positives.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Match readable((cast*)var + OFF, SIZE)
RX_READ_OFF = re.compile(
    r'\breadable\s*\(\s*(?:\([^)]+\)\s*)?([A-Za-z_]\w*)\s*\+\s*'
    r'(0x[0-9a-fA-F]+|\d+)[uUlL]*\s*,\s*(0x[0-9a-fA-F]+|\d+)[uUlL]*\s*\)'
)

# Match readable((cast*)var, SIZE)
RX_READ_BASE = re.compile(
    r'\breadable\s*\(\s*(?:\([^)]+\)\s*)?([A-Za-z_]\w*)\s*,\s*'
    r'(0x[0-9a-fA-F]+|\d+)[uUlL]*\s*\)'
)

# Match F<T>(var, OFF) / field<T>(var, OFF) / skg::field<T>(var, OFF)
RX_FIELD = re.compile(
    r'(?:(?:skg::)?(?:field|F))\s*<\s*([^>]+?)\s*>\s*\(\s*([A-Za-z_]\w*)\s*,\s*'
    r'(0x[0-9a-fA-F]+|\d+)[uUlL]*\s*\)'
)

# Match *(T*)((cast*)var + OFF)
RX_DEREF = re.compile(
    r'\*\s*\(\s*(?:const\s+)?([^)*]+?)\s*\*\s*\)\s*\(\s*(?:\([^)]+\)\s*)?'
    r'([A-Za-z_]\w*)\s*\+\s*(0x[0-9a-fA-F]+|\d+)[uUlL]*\s*\)'
)

# Match vcall<...>(expr, 0xHEX, ...) where the index is a raw hex literal without `/ 4`
RX_VCALL_RAW_HEX = re.compile(
    r'\bvcall\s*<[^>]+>\s*\([^,]+,\s*(0x[0-9a-fA-F]+)[uUlL]*\s*(?:,|\))'
)

# Match vcall<void*>(receiver, 0x0C / 4, <typeId>)
RX_VCALL_0C_CAST = re.compile(
    r'\bvcall\s*<\s*void\s*\*\s*>\s*\(\s*([^,]+?)\s*,\s*'
    r'(?:0x0*[cC]\s*/\s*4|3)\s*,\s*((?:\([^)]*\)\s*)?[^,)]+)\)'
)

# Receivers that indicate a cSpatialObject* subobject (Cast is at +0xB8, not +0x0C)
RX_SPATIAL_RECV = re.compile(
    r'(?:\bspatial\w*\b|\bgame::spatial\s*\(|\+\s*0x(?:70|c0|C0)\b)'
)

# 4-byte type ID argument (hex literal or k*TypeId constant)
RX_TYPE_ID_ARG = re.compile(
    r'(?:0x[0-9a-fA-F]{4,}[uUlL]*|\bk[A-Z]\w*Type(?:Id)?\b)'
)

TYPE_SIZES = {
    'uint8_t': 1, 'int8_t': 1, 'char': 1, 'unsigned char': 1, 'bool': 1, 'BYTE': 1,
    'uint16_t': 2, 'int16_t': 2, 'short': 2, 'unsigned short': 2, 'WORD': 2,
    'uint32_t': 4, 'int32_t': 4, 'int': 4, 'unsigned': 4, 'unsigned int': 4,
    'float': 4, 'DWORD': 4, 'uintptr_t': 4, 'intptr_t': 4, 'long': 4, 'unsigned long': 4,
    'uint64_t': 8, 'int64_t': 8, 'double': 8,
}


def strip_comment(line):
    return line.split('//', 1)[0]


def type_size(t):
    t = t.strip()
    if '*' in t:
        return 4
    t = re.sub(r'\b(?:const|volatile)\b', '', t).strip()
    return TYPE_SIZES.get(t, 0)


def is_guard_line(code):
    s = code.lstrip()
    return s.startswith('if') or s.startswith('else if') or s.startswith('while') or s.startswith('} else if')


def iter_accesses(text):
    for m in RX_FIELD.finditer(text):
        yield m.group(1), m.group(2), int(m.group(3), 0)
    for m in RX_DEREF.finditer(text):
        yield m.group(1), m.group(2), int(m.group(3), 0)


def has_readable_for(text, var):
    return bool(re.search(rf'\breadable\s*\((?:[^()]|\([^)]*\))*\b{re.escape(var)}\b', text))


def check_lines(rel, lines):
    findings = []
    n_lines = len(lines)

    for idx, raw_line in enumerate(lines):
        lineno = idx + 1
        code = strip_comment(raw_line)
        if not code.strip():
            continue

        # --- Check 1a: readable(var + OFF, SIZE) ---
        if 'readable' in code:
            for m in RX_READ_OFF.finditer(code):
                var = m.group(1)
                off = int(m.group(2), 0)
                sz = int(m.group(3), 0)
                if 'lint:ok-readable' in raw_line:
                    continue

                # Check remainder of same statement up to ';' or next readable on var
                rest = code[m.end():]
                stmt_part = rest.split(';', 1)[0]
                if not has_readable_for(stmt_part, var):
                    for t, fvar, foff in iter_accesses(stmt_part):
                        if fvar != var:
                            continue
                        tsz = type_size(t)
                        if foff < off or (tsz and foff + tsz > off + sz):
                            findings.append(
                                f'{rel}:{lineno}: readable({var} + 0x{off:X}, {sz}) does not cover '
                                f'<{t.strip()}>({var}, 0x{foff:X})'
                            )

                # If this line is a guard (if / else if / while), also check next 2 lines
                if is_guard_line(code):
                    for j in range(1, 3):
                        if idx + j >= n_lines:
                            break
                        nxt_raw = lines[idx + j]
                        nxt = strip_comment(nxt_raw)
                        if 'lint:ok-readable' in nxt_raw:
                            continue
                        if '}' in nxt:
                            break
                        if has_readable_for(nxt, var):
                            break
                        if re.search(rf'\b{re.escape(var)}\s*=(?!=)', nxt):
                            break
                        for t, fvar, foff in iter_accesses(nxt):
                            if fvar != var:
                                continue
                            tsz = type_size(t)
                            if foff < off or (tsz and foff + tsz > off + sz):
                                findings.append(
                                    f'{rel}:{lineno}->{lineno + j}: readable({var} + 0x{off:X}, {sz}) '
                                    f'does not cover <{t.strip()}>({var}, 0x{foff:X})'
                                )

            # --- Check 1b: readable(var, SIZE) ---
            for m in RX_READ_BASE.finditer(code):
                var = m.group(1)
                sz = int(m.group(2), 0)
                if 'lint:ok-readable' in raw_line:
                    continue

                rest = code[m.end():]
                stmt_part = rest.split(';', 1)[0]
                if not has_readable_for(stmt_part, var):
                    for t, fvar, foff in iter_accesses(stmt_part):
                        if fvar != var:
                            continue
                        tsz = type_size(t) or 1
                        if foff + tsz > sz:
                            findings.append(
                                f'{rel}:{lineno}: readable({var}, 0x{sz:X}) does not cover '
                                f'<{t.strip()}>({var}, 0x{foff:X}) (needs 0x{foff + tsz:X})'
                            )

                if is_guard_line(code):
                    for j in range(1, 3):
                        if idx + j >= n_lines:
                            break
                        nxt_raw = lines[idx + j]
                        nxt = strip_comment(nxt_raw)
                        if 'lint:ok-readable' in nxt_raw:
                            continue
                        if '}' in nxt:
                            break
                        if has_readable_for(nxt, var):
                            break
                        if re.search(rf'\b{re.escape(var)}\s*=(?!=)', nxt):
                            break
                        for t, fvar, foff in iter_accesses(nxt):
                            if fvar != var:
                                continue
                            tsz = type_size(t) or 1
                            if foff + tsz > sz:
                                findings.append(
                                    f'{rel}:{lineno}->{lineno + j}: readable({var}, 0x{sz:X}) '
                                    f'does not cover <{t.strip()}>({var}, 0x{foff:X}) (needs 0x{foff + tsz:X})'
                                )

        # --- Check 2: Suspicious vcall cast / raw hex byte-offset slot ---
        if 'vcall' in code and 'lint:ok-vcall-0c' not in raw_line:
            for m in RX_VCALL_RAW_HEX.finditer(code):
                raw_hex = m.group(1)
                findings.append(
                    f'{rel}:{lineno}: vcall passes raw hex byte offset {raw_hex} instead of vtable index ({raw_hex} / 4)'
                )

            for m in RX_VCALL_0C_CAST.finditer(code):
                recv = m.group(1).strip()
                arg = m.group(2).strip()
                if not RX_TYPE_ID_ARG.search(arg):
                    continue
                if not RX_SPATIAL_RECV.search(recv):
                    continue
                # Allow an explicit COOP_FIX off-branch directly paired with the +0xB8 / kSpatialCastVtableSlot branch
                prev_code = strip_comment(lines[idx - 1]) if idx > 0 else ''
                if code.lstrip().startswith(':') and (
                    'kSpatialCastVtableSlot' in prev_code or '0xB8' in prev_code or '0xb8' in prev_code
                ):
                    continue
                findings.append(
                    f'{rel}:{lineno}: suspicious vcall<void*>({recv}, 0x0C / 4, {arg}) on spatial object '
                    f'(cSpatialObject::Cast is at +0xB8, not +0x0C; annotate // lint:ok-vcall-0c if real Object::Cast)'
                )

    return findings


def self_test():
    bad_cases = [
        (
            'off_by_4_sp',
            [
                '    if (!sp || !readable((uint8_t*)sp + 0x570, 4))\n',
                '        return false;\n',
                '    float bhp = field<float>(sp, 0x56C);\n',
            ],
        ),
        (
            'off_by_4_comb',
            [
                '    if (!cancel_hostile && skg::readable((uint8_t*)c + 0xE80, 4)) {\n',
                '        void* comb = F<void*>(c, 0xE7C);\n',
                '    }\n',
            ],
        ),
        (
            'undersized_base',
            [
                '    if (obj && readable(obj, 0x10)) {\n',
                '        uint32_t v = F<uint32_t>(obj, 0x10);\n',
                '    }\n',
            ],
        ),
        (
            'raw_hex_vcall',
            [
                '    void* gd = vcall<void*>(obj, 0x0C, 0x17F243Bu);\n',
            ],
        ),
        (
            'spatial_vcall_0c',
            [
                '    void* gd = vcall<void*>(spatial, 0x0C / 4, (uint32_t)0x17F243Bu);\n',
            ],
        ),
        (
            'spatial_subobj_vcall_0c',
            [
                '    void* gd = vcall<void*>((uint8_t*)c + 0xC0, 0xC / 4, 0x17F243Bu);\n',
            ],
        ),
    ]

    good_cases = [
        (
            'exact_readable_off',
            [
                '    if (!sp || !readable((uint8_t*)sp + 0x56C, 4))\n',
                '        return false;\n',
                '    float bhp = field<float>(sp, 0x56C);\n',
            ],
        ),
        (
            'per_line_readable_or',
            [
                '    bool inv = (skg::readable((uint8_t*)obj + 0x5DC, 4) && *(uint32_t*)((uint8_t*)obj + 0x5DC) == 2u) ||\n',
                '               (skg::readable((uint8_t*)obj + 0x628, 1) && *(uint8_t*)((uint8_t*)obj + 0x628) != 0);\n',
            ],
        ),
        (
            'ternary_non_guard',
            [
                '    void* hut = best && skg::readable(best, 0x36C) ? F<void*>(best, 0x368) : nullptr;\n',
                '    int r = best && !F<uint8_t>(best, 0x556) ? 1 : 0;\n',
            ],
        ),
        (
            'object_cast_0c',
            [
                '    void* gd = vcall<void*>(obj, 0xC / 4, (uint32_t)0x17F243Bu);\n',
            ],
        ),
        (
            'annotated_spatial_0c',
            [
                '    void* gd = vcall<void*>(spatial, 0x0C / 4, 0x17F243Bu); // lint:ok-vcall-0c\n',
            ],
        ),
        (
            'coop_fix_off_branch',
            [
                '    void* gd = g_fix\n',
                '                   ? vcall<void*>(spatial, kSpatialCastVtableSlot, kTribeToolTypeId)\n',
                '                   : vcall<void*>(spatial, 0xC / 4, (uint32_t)0x17F243Bu);\n',
            ],
        ),
    ]

    failed = 0
    for name, lines in bad_cases:
        res = check_lines(f'<bad:{name}>', lines)
        if not res:
            print(f'static_guard_lint --self-test FAIL: expected finding for {name}')
            failed += 1
    for name, lines in good_cases:
        res = check_lines(f'<good:{name}>', lines)
        if res:
            print(f'static_guard_lint --self-test FAIL: unexpected finding for {name}: {res}')
            failed += 1
    if failed:
        return 1
    print(f'static_guard_lint --self-test: {len(bad_cases)} bad + {len(good_cases)} good cases passed')
    return 0


def main():
    if '--self-test' in sys.argv[1:]:
        return self_test()

    files = sorted(
        glob.glob(os.path.join(ROOT, 'mods', '**', '*.cpp'), recursive=True)
        + glob.glob(os.path.join(ROOT, 'mods', '**', '*.h'), recursive=True)
    )
    all_findings = []
    for path in files:
        rel = os.path.relpath(path, ROOT)
        with open(path, encoding='utf-8') as fh:
            lines = fh.readlines()
        all_findings.extend(check_lines(rel, lines))

    for f in all_findings:
        print(f)
    print(f'static_guard_lint: {len(files)} files, {len(all_findings)} problem(s)')
    return 1 if all_findings else 0


if __name__ == '__main__':
    sys.exit(main())
