"""Dump the decrypted SporeApp image from a running QA game (SporeKit mem.read), undo the ASLR relocation and write a
PE with image base 0x400000 whose file layout equals its memory layout (raw offset = RVA), for Ghidra on Windows.

  py tools/qa_win/dumpexe.py <instance> <out.exe>      (the game at the galaxy menu: its code is decrypted then)
"""
import os
import re
import struct
import subprocess
import sys

QA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "qa.py")
INST = sys.argv[1]
OUT = sys.argv[2]


def rd(a, n):
    out = subprocess.run(['py', QA, INST, f'mem.read 0x{a:08X} {n}'], capture_output=True, text=True).stdout
    b = bytearray()
    for line in out.splitlines():
        m = re.match(r'^([0-9A-Fa-f]{8})\s+(.*)$', line.strip())
        if m:
            for t in m.group(2).split():
                if re.fullmatch(r'[0-9A-Fa-f]{2}', t):
                    b.append(int(t, 16))
                else:
                    break
    if len(b) != n:
        raise SystemExit(f'short read at {a:08X}: {len(b)} of {n}: {out[:200]}')
    return bytes(b)


info = subprocess.run(['py', QA, INST, 'info'], capture_output=True, text=True).stdout
base = int(re.search(r'base=0x([0-9A-Fa-f]+)', info).group(1), 16)
hdr = rd(base, 0x1000)
pe = struct.unpack_from('<I', hdr, 0x3C)[0]
nsec = struct.unpack_from('<H', hdr, pe + 6)[0]
optsz = struct.unpack_from('<H', hdr, pe + 20)[0]
opt = pe + 24
size_image = struct.unpack_from('<I', hdr, opt + 56)[0]
pref_base = struct.unpack_from('<I', hdr, opt + 28)[0]
print(f'base {base:08X} header ImageBase {pref_base:08X} SizeOfImage {size_image:X} sections {nsec}')
img = bytearray()
for off in range(0, size_image, 0x10000):
    n = min(0x10000, size_image - off)
    try:
        img += rd(base + off, n)
    except SystemExit as e:  # an unreadable page: zeros, 4 KB at a time
        print(e)
        for p in range(0, n, 0x1000):
            try:
                img += rd(base + off + p, 0x1000)
            except SystemExit:
                img += bytes(0x1000)
    if off % 0x400000 == 0:
        print(f'  {off:X} / {size_image:X}', flush=True)
# relocations: data directory 5
reloc_rva, reloc_size = struct.unpack_from('<II', img, opt + 96 + 5 * 8)
delta = (base - 0x400000) & 0xFFFFFFFF
fix = 0
p = reloc_rva
while p < reloc_rva + reloc_size:
    page, blk = struct.unpack_from('<II', img, p)
    if blk < 8:
        break
    for i in range((blk - 8) // 2):
        e = struct.unpack_from('<H', img, p + 8 + 2 * i)[0]
        if e >> 12 == 3:  # HIGHLOW
            at = page + (e & 0xFFF)
            v = struct.unpack_from('<I', img, at)[0]
            struct.pack_into('<I', img, at, (v - delta) & 0xFFFFFFFF)
            fix += 1
    p += blk
print(f'relocations undone: {fix} (delta {delta:08X})')
# headers: image base 0x400000, file layout = memory layout
struct.pack_into('<I', img, opt + 28, 0x400000)
struct.pack_into('<I', img, opt + 36, struct.unpack_from('<I', img, opt + 32)[0])  # FileAlignment = SectionAlignment
sec = opt + optsz
for i in range(nsec):
    s = sec + 40 * i
    name = img[s:s + 8].rstrip(b'\0').decode('latin1')
    vsize, va = struct.unpack_from('<II', img, s + 8)
    struct.pack_into('<II', img, s + 16, (vsize + 0xFFF) & ~0xFFF, va)  # SizeOfRawData, PointerToRawData
    print(f'  section {name:8s} va {va:08X} size {vsize:X}')
open(OUT, 'wb').write(img)
print('wrote', OUT, len(img))
