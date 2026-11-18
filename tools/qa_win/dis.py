"""Game code without Ghidra on the Windows PC (BUG-9153, 2026-11-12): read it from a running QA game's memory.

  py tools/qa_win/dis.py <instance|msi> dis <sym address> [bytes]     disassemble (llvm-objdump of llvm-mingw)
  py tools/qa_win/dis.py <instance|msi> calls <crash base> <addr>...   which crash stack-scan entries are return addresses

Addresses are .sym addresses (image base 0x400000). The Steam / GOG 2024 exe is relocated (ASLR): `info` gives the
running game's base; `calls` takes the base of the game that crashed (the crash report's "== Modules" line for
SporeApp.exe) and prints each stack-scan entry as a .sym address with the call before it (`-`: not a return address).
The Steam exe on disk is encrypted, so the code is read from memory (`mem.read`). Needs LLVM_MINGW (as msi.ps1 deploy).
"""
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import qa  # noqa: E402

con = qa.console(sys.argv[1])
off = int(re.search(r'base=0x([0-9A-Fa-f]+)', con.call('info')['out']).group(1), 16) - 0x400000


def read(addr, n):
    data = []
    for chunk in range(0, n, 256):
        out = con.call(f'mem.read 0x{addr + off + chunk:X} {min(256, n - chunk)}')['out']
        for line in out.splitlines():
            data += [int(x, 16) for x in line.split()[1:]]
    return bytes(data[:n])


def dis(addr, n):
    tools = os.path.join(os.environ['LLVM_MINGW'], 'bin')
    d = tempfile.mkdtemp()
    s, o = os.path.join(d, 'x.s'), os.path.join(d, 'x.o')
    open(s, 'w').write('.text\n' + ''.join(f'.byte {b}\n' for b in read(addr, n)))
    subprocess.run([os.path.join(tools, 'i686-w64-mingw32-clang.exe'), '-c', s, '-o', o], check=True)
    r = subprocess.run([os.path.join(tools, 'llvm-objdump.exe'), '-d', '--no-show-raw-insn', '-M', 'intel', o],
                       capture_output=True, text=True)
    for line in r.stdout.splitlines():
        m = re.match(r'\s+([0-9a-f]+):\s+(.*)', line)
        if not m:
            continue
        text = m.group(2)
        t = re.match(r'(call|jmp|j\w+)\s+0x([0-9a-f]+)', text)
        if t:  # objdump counts from 0: make the target absolute
            text = f'{t.group(1)} 0x{(int(t.group(2), 16) + addr) & 0xFFFFFFFF:08X}'
        print(f'{int(m.group(1), 16) + addr:08X}: {text}')


def calls(crash_base, addrs):
    for a in addrs:
        sym = a - crash_base + 0x400000
        b = read(sym - 7, 7)
        kind = '-'
        if b[2] == 0xE8:
            kind = f'call 0x{(sym + int.from_bytes(b[3:7], "little", signed=True)) & 0xFFFFFFFF:08X}'
        else:
            for i in (1, 3, 4, 5):  # FF /2: call indirect, 6, 4, 3 or 2 bytes
                if b[i] == 0xFF and (b[i + 1] & 0x38) == 0x10:
                    kind = 'call indirect ' + b[i:].hex()
                    break
        print(f'{a:08X} -> {sym:08X} {kind}')


if sys.argv[2] == 'dis':
    dis(int(sys.argv[3], 16), int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x40)
elif sys.argv[2] == 'calls':
    calls(int(sys.argv[3], 16), [int(x, 16) for x in sys.argv[4:]])
else:
    sys.exit(__doc__)
