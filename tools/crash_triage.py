#!/usr/bin/env python3
"""Turn a crash report into a build, a signature and an action (the triage-crash skill, BUG-0158).

  python3 -I tools/crash_triage.py <crash-*.txt | sporekit*.log | a folder | a .zip> [...]

Reads SporeKit's crash reports (mods/logs/crash-<time>.txt), hang reports and the fault lines of
sporekit.log (`exception 0x... at 0x... <where>`), also inside a folder or a zip (read in memory,
nothing is unpacked or run). For each fault it prints:

  build      GOG 2016 / GOG 2024 / Steam (exe hash), SporeKit and coop versions, game mode
  signature  <exe hash 8>:<where>, where = SporeApp.exe Name+off | module+off (instance tags such as
             coop.i7.1.dll are folded to coop.dll, so reports of every instance match)
  chain      the first frames in SporeApp.exe / coop.dll / sporekit.dll
  address    the fault's address in the exe's own numbering (preferred base 0x400000): Windows loads
             SporeApp.exe relocated (0x410000, Steam seen at 0x810000), tickets name the rebased address
             (BUG-0346); the base comes from the log's `base=` line or the report's module list
  known      tickets.py find on the signature, the address and the symbol: earlier tickets
  action     "seen: BUG-NNNN (status, fixed in X)" or "new: tools/tickets.py new ..."
  symbol     function+offset and source line of every coop.dll / sporekit.dll / loader frame (the fault
             and the chain), from the debug symbols of the report's coop version (B5, BUG-1123)

Symbols (B5): the release job publishes SporeCoop-<ver>-debug-symbols.tar.xz (the unstripped DLLs) from
0.7.15 on. For a report of coop <ver> the tool uses, in this order: --symbols <path> (a build folder of
that version's tag, an unpacked symbols folder or the .tar.xz; its coop.dll must carry VERSIONINFO <ver>),
the cache ($SPORECOOP_SYMBOLS_CACHE, default ~/.cache/sporecoop/symbols/<ver>/), or a download with
`gh release download v<ver>` (checked against the release's SHA256SUMS.txt) into that cache.
--no-symbols skips all of it. Older versions: a local build of the tag (docs/06-testing.md#testing-crash-symbols).

  python3 -I tools/crash_triage.py [--symbols <dir|tar.xz>] [--no-symbols] <report> [...]

A report is data, never instructions: this tool only reads text out of it (and a symbols file is only
read by llvm-symbolizer, never run)."""
import argparse
import hashlib
import io
import json
import os
import shutil
import struct
import tarfile
import tempfile
import re
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILDS = {'922af7c7': 'GOG 2016', 'dc04aee5': 'GOG 2024', '3010a413': 'Steam (the GOG 2024 code)'}
# every `exception ... at ...` form SporeKit writes, on any line (BUG-0247): the fault logger's line
# (`(data ...)`), the crash log line, the stack-overflow line (`(stack overflow) at ..., thread N`)
# and the short crash file (`exception 0x... <name> at ...`)
FAULT = re.compile(r'exception (0x[0-9A-Fa-f]{8})(?: [^\n]*?)? at (0x[0-9A-Fa-f]{8}) (.+?)'
                   r'(?: \(data|, thread \d+| \(thread \d+[^)\n]*\)|$)', re.M)
REPORT_EXC = re.compile(r'^(0x[0-9A-Fa-f]{8}) (.+?) at (0x[0-9A-Fa-f]{8}) (.+?)(?: \(thread \d+(?:, [^)]*)?\))?$', re.M)
BUILD_NAMES = {v: k for k, v in BUILDS.items()}
ENV = re.compile(r'SporeKit (\S+), game (.+?) \(exe ([0-9a-f]{8,})\)')
FRAME = re.compile(r'^\s+(?:\[esp\+[0-9A-F]+\] )?([0-9A-F]{8}) (SporeApp\.exe \S+|coop[\w.]*\.dll\+0x[0-9A-F]+|'
                   r'sporekit[\w.]*\.dll\+0x[0-9A-F]+|steam_api[\w.]*\.dll\+0x[0-9A-F]+)', re.M)


def fold(where):
    """coop.i7.1.dll+0x439A6 -> coop.dll+0x439A6 (the instance copy of the same file)"""
    return re.sub(r'\b(coop|sporekit)\.i\d+(?:\.\d+)?\.dll', r'\1.dll', where.strip())


def clean(s):
    """report text is data: no control characters into our output"""
    return re.sub(r'[\x00-\x08\x0a-\x1f\x7f]', '', s)


def clean_where(where):
    """the place only: no thread text (`(thread 4242, the game's main thread)`, `, thread 12`), whatever
    thread wrote the report, so every report of the same place has one signature (BUG-0247)"""
    w = re.sub(r'(?: \(thread \d+[^)]*\)|, thread \d+.*)$', '', clean(where).strip())
    return fold(w)


def texts(path):
    """(name, text) of every report-like file in path (a file, a folder or a zip)"""
    if os.path.isdir(path):
        for d, _, files in os.walk(path):
            for n in sorted(files):
                yield from texts(os.path.join(d, n))
    elif zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as z:
            for i in z.infolist():
                n = os.path.basename(i.filename)
                if i.file_size < 32 << 20 and re.search(r'(crash|hang).*\.txt$|sporekit.*\.log$', n):
                    yield f'{path}:{i.filename}', z.read(i).decode('utf-8', 'replace')
    elif re.search(r'(crash|hang).*\.txt$|\.log$|\.txt$', path):
        with open(path, 'rb') as f:
            yield path, f.read(32 << 20).decode('utf-8', 'replace')


def build_of(text):
    m = ENV.search(text)
    if m:
        return dict(kit=m.group(1), build=clean(m.group(2)), exe=m.group(3)[:8])
    # the short crash file names the build without its hash (BUG-0247): map the name back, never guess
    m = re.search(r'SporeKit (\S+), game (.+?)\s*$', text, re.M)
    if m:
        name = m.group(2).strip()
        h = BUILD_NAMES.get(name) or (re.search(r'\b([0-9a-f]{8})\b', name).group(1)
                                      if re.search(r'GOG 2024 code \(|unknown build ', name)
                                      and re.search(r'\b([0-9a-f]{8})\b', name) else None)
        if h:
            return dict(kit=clean(m.group(1)), build=BUILDS.get(h, clean(name)), exe=h)
    for h, name in BUILDS.items():  # sporekit.log: "symbols: ... _<hash>.sym" or the exe hash line
        if h in text:
            return dict(kit='?', build=name, exe=h)
    return dict(kit='?', build='unknown', exe='????????')


EXE_BASE = re.compile(r'SporeApp\.exe sha256=\S+ base=0x([0-9A-Fa-f]{8})|'
                      r'^\s+([0-9A-F]{8})\s+[0-9A-F]+\s+\S.*\\SporeApp\.exe\s*$', re.M)
PREFERRED_BASE = 0x400000


def exe_base(text):
    """where SporeApp.exe was loaded (None when the text does not say): the log's `exe ... base=` line or
    the report's module list"""
    m = EXE_BASE.search(text)
    return int(m.group(1) or m.group(2), 16) if m else None


def rebased(eip, where, base):
    """the fault address in the exe's preferred numbering (0x400000), as tickets and .sym files name it
    (BUG-0346), or None when the fault is not in SporeApp.exe"""
    m = re.match(r'SporeApp\.exe\+(0x[0-9A-Fa-f]+)$', where)
    if m:
        return PREFERRED_BASE + int(m.group(1), 16)
    in_exe = where.startswith('SporeApp.exe') or ('.dll' not in where and '::' in where)  # log lines omit the module
    if in_exe and base is not None:
        return int(eip, 16) - base + PREFERRED_BASE
    return None


def faults(text):
    """[(code, eip, where)]: the report's exception first, then the log's `the game crashed:` lines,
    then the other fault lines in file order (BUG-0247: the crash is never cut off by fl[:3])"""
    out = []
    if '== Exception' in text:
        sec = text.split('== Exception', 1)[1]
        m = REPORT_EXC.search(sec)
        if m:
            out.append((m.group(1), m.group(3), clean_where(m.group(4))))
    found = [(('the game crashed:' not in text[text.rfind('\n', 0, m.start()) + 1:m.start()]),
              (m.group(1), m.group(2), clean_where(m.group(3)))) for m in FAULT.finditer(text)]
    for _, f in sorted(found, key=lambda x: x[0]):  # stable: crash lines first, then file order
        if not any(o[:2] == f[:2] for o in out):  # the same fault named twice (with and without the module)
            out.append(f)
    return out


def find(term):
    r = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'tickets.py'), 'find', term],
                       capture_output=True, text=True)
    return [l for l in r.stdout.splitlines() if re.match(r'^BUG-\d{4} ', l)]


# the running coop version, most reliable first (BUG-0372): the crash file's Environment (`mods: coop X`) and
# coop section (`SporeCoop X, name ...`), the log's load line; a crash file's Summary quotes the latest
# co-op summary, which can be of an earlier run with another version, so a plain `coop X` comes last
# and never from a summary line
COOP_VERSION = (re.compile(r'^mods: coop v?(\d+\.\d+[\w.]*\d)', re.M),
                re.compile(r'^SporeCoop v?(\d+\.\d+[\w.]*\d)[ ,]', re.M),
                re.compile(r'coop: (?:SporeCoop|loaded coop) v?(\d+\.\d+[\w.]*\d)'))


def coop_version(text):
    for rx in COOP_VERSION:
        m = rx.search(text)
        if m:
            return m
    for line in text.splitlines():
        if 'summary' in line or 'an earlier run' in line:
            continue
        m = re.search(r'\bcoop v?(\d+\.\d+[\w.]*\d)', line)
        if m:
            return m
    return None


# ---------------------------------------------------------------- debug symbols (B5, BUG-1123)
REPO = os.environ.get('SPORECOOP_REPO', 'WoodyWizard/SporeCoop')
SYMBOLS_SINCE = (0, 7, 15)  # the first release with a debug-symbols asset
SYM_FILES = ('coop.dll', 'sporekit.dll', 'steam_api.sporekit.dll')
OWN_FRAME = re.compile(r'\b(coop|sporekit|steam_api(?:\.sporekit)?)\.dll\+0x([0-9A-Fa-f]+)')


def vtuple(v):
    return tuple(int(x) for x in re.findall(r'\d+', v))


def sym_file(mod):
    return {'coop': 'coop.dll', 'sporekit': 'sporekit.dll'}.get(mod, 'steam_api.sporekit.dll')


def dll_version(path):
    """ProductVersion of a DLL's VERSIONINFO (tools/pe_version.py), or None"""
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    try:
        import pe_version
        _, strings = pe_version.read(path)
    except (OSError, ValueError, struct.error):
        return None
    finally:
        sys.path.pop(0)
    if strings:
        return strings.get('ProductVersion')
    # builds before 0.7.12 have no VERSIONINFO (BUG-0784): the COOP_VERSION string compiled into coop.dll
    try:
        with open(path, 'rb') as f:
            found = set(re.findall(rb'SporeCoop (\d+\.\d+(?:\.\d+)*)[ ,\x00]', f.read()))
    except OSError:
        return None
    return found.pop().decode() if len(found) == 1 else None


def cache_root():
    return os.environ.get('SPORECOOP_SYMBOLS_CACHE') or os.path.join(
        os.environ.get('XDG_CACHE_HOME') or os.path.join(os.path.expanduser('~'), '.cache'), 'sporecoop', 'symbols')


def unpack_symbols(tar_path, dest):
    """the known files of a symbols .tar.xz into dest (nothing else: no links, no paths of its own)"""
    os.makedirs(dest, exist_ok=True)
    got = []
    with tarfile.open(tar_path, 'r:xz') as t:
        for m in t.getmembers():
            base = os.path.basename(m.name)
            if m.isfile() and base in SYM_FILES + ('SYMBOLS.txt',) and m.size < 1 << 30:
                with t.extractfile(m) as src, open(os.path.join(dest, base + '.part'), 'wb') as dst:
                    shutil.copyfileobj(src, dst)
                os.replace(os.path.join(dest, base + '.part'), os.path.join(dest, base))
                got.append(base)
    return got


def download_symbols(ver, dest):
    """(ok, note): the release's symbols asset, its sha256 checked against SHA256SUMS.txt, unpacked to dest"""
    asset = f'SporeCoop-{ver}-debug-symbols.tar.xz'
    if not shutil.which('gh'):
        return False, 'gh (GitHub CLI) is not installed'
    with tempfile.TemporaryDirectory() as tmp:
        try:
            r = subprocess.run(['gh', 'release', 'download', f'v{ver}', '--repo', REPO, '--pattern', asset,
                                '--pattern', 'SHA256SUMS.txt', '--dir', tmp], capture_output=True, text=True,
                               timeout=600)
        except subprocess.TimeoutExpired:
            return False, f'gh release download v{ver} timed out'
        tar_path = os.path.join(tmp, asset)
        if r.returncode != 0 or not os.path.isfile(tar_path):
            return False, f'gh release download v{ver} --pattern {asset}: ' + clean((r.stderr or r.stdout).strip())[:200]
        sums = os.path.join(tmp, 'SHA256SUMS.txt')
        want = None
        if os.path.isfile(sums):
            for line in open(sums, encoding='utf-8', errors='replace'):
                parts = line.split()
                if len(parts) == 2 and parts[1].lstrip('*') == asset:
                    want = parts[0].lower()
        h = hashlib.sha256()
        with open(tar_path, 'rb') as f:
            for chunk in iter(lambda: f.read(1 << 20), b''):
                h.update(chunk)
        if want is None:
            return False, f'SHA256SUMS.txt of v{ver} does not list {asset}: not used'
        if h.hexdigest() != want:
            return False, f'{asset}: sha256 {h.hexdigest()} is not the release\'s {want}: not used'
        try:
            got = unpack_symbols(tar_path, dest + '.part')
        except (tarfile.TarError, OSError) as e:
            shutil.rmtree(dest + '.part', ignore_errors=True)
            return False, f'{asset}: {e}'
        if 'coop.dll' not in got:
            shutil.rmtree(dest + '.part', ignore_errors=True)
            return False, f'{asset} has no coop.dll'
        shutil.rmtree(dest, ignore_errors=True)
        os.replace(dest + '.part', dest)
    return True, f'release asset {asset}, sha256 checked, cached in {dest}'


def find_symbols(ver, opt):
    """(folder with coop.dll / sporekit.dll / steam_api.sporekit.dll or None, a note on where it came from)"""
    if opt is not None and opt.symbols:
        p = os.path.abspath(opt.symbols)
        if os.path.isfile(p):
            d = os.path.join(cache_root(), 'local', hashlib.sha256(p.encode()).hexdigest()[:12])
            try:
                unpack_symbols(p, d)
            except (tarfile.TarError, OSError) as e:
                return None, f'--symbols {p}: {e}'
            p = d
        dll = os.path.join(p, 'coop.dll')
        if not os.path.isfile(dll):
            return None, f'--symbols {opt.symbols}: no coop.dll there'
        have = dll_version(dll)
        if have != ver:
            return None, (f'--symbols {opt.symbols}: its coop.dll is {have or "of no version"}, the report is coop {ver}: '
                          f'the offsets would name wrong functions (build tag v{ver})')
        note = f'--symbols {opt.symbols} (coop.dll {have})'
        repo = os.path.dirname(p)
        tag = subprocess.run(['git', '-C', repo, 'describe', '--exact-match', '--tags', 'HEAD'], capture_output=True,
                             text=True)
        if os.path.isdir(os.path.join(repo, '.git')) or os.path.isfile(os.path.join(repo, '.git')):
            if tag.stdout.strip() != f'v{ver}':
                note += f'; WARNING: that tree is not at tag v{ver}: right only if the code is the same'
        return p, note
    d = os.path.join(cache_root(), ver)
    if os.path.isfile(os.path.join(d, 'coop.dll')):
        return d, f'cached release asset {d}'
    if vtuple(ver) < SYMBOLS_SINCE:
        return None, (f'coop {ver} is older than the first release with a symbols asset (0.7.15): build its tag '
                      f'(d=$(mktemp -d) && git archive v{ver} | tar -x -C $d && make -C $d -j8 coop sporekit loader) and '
                      f'pass --symbols $d/build')
    ok, note = download_symbols(ver, d)
    return (d, note) if ok else (None, note + f' (or --symbols <a build of tag v{ver}>)')


def symbolizer():
    exe = os.environ.get('LLVM_SYMBOLIZER')
    if exe:
        return exe
    llvm = os.environ.get('LLVM_MINGW')
    if not llvm:
        r = subprocess.run(['make', '-s', '--no-print-directory', '-C', ROOT, 'toolchain-path'], capture_output=True,
                           text=True)
        llvm = r.stdout.strip()
    exe = os.path.join(llvm, 'bin', 'llvm-symbolizer') if llvm else ''
    return exe if os.path.isfile(exe) else shutil.which('llvm-symbolizer')


def short_path(f):
    """source paths relative to the repository; the toolchain's headers as <llvm-mingw>/..."""
    f = f.replace('\\', '/')
    f = re.sub(r'^.*?/llvm-mingw[^/]*/', '<llvm-mingw>/', f)
    m = re.search(r'(?:^|/)((?:mods|sporekit|sdk|updater|installer|loader|third_party|tests)/.*)$', f)
    return m.group(1) if m and not f.startswith('<') else f


def symbolize(folder, frames):
    """{(module file, rva, is_return): 'function+0xN  file:line'} for the frames, by llvm-symbolizer"""
    exe = symbolizer()
    if not exe:
        return None, 'llvm-symbolizer not found (it ships in llvm-mingw: make toolchain-path)'
    out = {}
    by_mod = {}
    for f in frames:
        by_mod.setdefault(f[0], []).append(f)
    for mod, fs in by_mod.items():
        obj = os.path.join(folder, mod)
        if not os.path.isfile(obj):
            continue
        addrs = [hex(rva - 1 if ret else rva) for _, rva, ret in fs]
        r = subprocess.run([exe, f'--obj={obj}', '--relative-address', '--relativenames', '--inlines',
                            '--output-style=JSON', *addrs], capture_output=True, text=True, timeout=300)
        try:
            rows = json.loads(r.stdout or '[]')
        except ValueError:
            rows = []
        for f, row in zip(fs, rows):
            syms = [x for x in row.get('Symbol', []) if x.get('FunctionName')]
            if not syms:
                continue
            top, outer = syms[0], syms[-1]  # the innermost inlined frame, the real function
            off = ''
            start, base = int(outer.get('StartAddress') or '0', 16), image_base(obj)
            if start and base is not None:
                off = f'+0x{f[1] - (start - base):X}'
            text = top['FunctionName'] + (off if top is outer else '')
            if top.get('FileName'):
                text += f'  {short_path(top["FileName"])}' + (f':{top["Line"]}' if top.get('Line') else '')
            if top is not outer:
                text += f'  (inlined into {outer["FunctionName"]}{off})'
            out[f] = text
    return out, None


def image_base(path):
    try:
        with open(path, 'rb') as f:
            head = f.read(4096)
        pe = struct.unpack_from('<I', head, 0x3C)[0]
        if head[pe:pe + 4] != b'PE\0\0':
            return None
        opt = pe + 24
        return struct.unpack_from('<I', head, opt + 28)[0] if struct.unpack_from('<H', head, opt)[0] == 0x10B \
            else struct.unpack_from('<Q', head, opt + 24)[0]
    except (OSError, struct.error):
        return None


def own_frames(fl, text):
    """[(module file, rva, is_return)] of the faults' places and the call chain, each once, fault first"""
    frames = []
    for _, _, where in fl[:3]:
        m = OWN_FRAME.search(fold(where))
        if m:
            frames.append((sym_file(m.group(1)), int(m.group(2), 16), False))
    for m in FRAME.finditer(text):
        m2 = OWN_FRAME.search(fold(m.group(2)))
        if m2:
            f = (sym_file(m2.group(1)), int(m2.group(2), 16), True)
            if (f[0], f[1], False) not in frames and f not in frames:
                frames.append(f)
    return frames


def print_symbols(fl, text, coop, opt):
    frames = own_frames(fl, text)
    if not frames or (opt is not None and opt.no_symbols):
        return
    if not coop:
        print('symbols    none: the report names no coop version')
        return
    ver = clean(coop.group(1))
    folder, note = find_symbols(ver, opt)
    if folder is None:
        print(f'symbols    none for coop {ver}: {note}')
        return
    names, err = symbolize(folder, frames)
    if err:
        print(f'symbols    {err}')
        return
    print(f'symbols    coop {ver}: {note}')
    for f in frames:
        mod = f[0] if f[0] != 'steam_api.sporekit.dll' else 'steam_api.dll'
        print(f'symbol     {mod}+0x{f[1]:X}  {names.get(f, "?")}' + ('' if f[2] else '   <- the fault'))


def triage(name, text, opt=None):
    b = build_of(text)
    coop = coop_version(text)
    mode = re.search(r'game mode: (\w+)', text)
    fl = faults(text)
    print(f'== {name}')
    print(f'build      {b["build"]} (exe {b["exe"]}), SporeKit {b["kit"]}, coop {clean(coop.group(1)) if coop else "?"}, '
          f'mode {clean(mode.group(1)) if mode else "?"}')
    if not fl:
        print('signature  none (no exception in this file)')
        return
    chain = [fold(m.group(2)) for m in FRAME.finditer(text)][:6]
    base = exe_base(text)
    for code, eip, where in fl[:3]:
        sig = f'{b["exe"]}:{where}'
        print(f'signature  {sig}   ({code} at {eip})')
        if chain:
            print('chain      ' + ' <- '.join(chain))
        terms = [where, eip.lower(), '0x' + eip[2:].upper().lstrip('0')]
        va = rebased(eip, where, base)
        if va is not None and va != int(eip, 16):
            print(f'address    0x{va:X} (the exe loaded at 0x{base:08X}, numbered from 0x{PREFERRED_BASE:X} here)'
                  if base is not None else f'address    0x{va:X} (numbered from 0x{PREFERRED_BASE:X})')
            terms.append(f'0x{va:X}')
        sym = re.match(r'SporeApp\.exe (\S+?)\+', where)
        if sym:
            terms.append(sym.group(1))
        terms += [f for f in chain[:2] if f.startswith(('coop.dll', 'SporeApp.exe'))]  # the first own frames
        hits = []
        for t in terms:
            hits += [h for h in find(t) if h not in hits]
        for h in hits[:5]:
            print(f'known      {h}')
        if hits:
            print(f'action     seen before: read {hits[0].split()[0]} (tools/tickets.py show); fixed in a newer version '
                  f'than the report\'s coop -> answer "fixed in X"; else add this report to it')
        else:
            print(f'action     new: tools/tickets.py new "<symptom> ({sig})" --kind crash --stage '
                  f'{mode.group(1) if mode else "<stage>"}')
    print_symbols(fl, text, coop, opt)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    ap = argparse.ArgumentParser(add_help=True, usage='crash_triage.py [--symbols <dir|tar.xz>] [--no-symbols] <report> ...')
    ap.add_argument('--symbols', help='a build folder of the report version\'s tag, a symbols folder or .tar.xz')
    ap.add_argument('--no-symbols', action='store_true', help='do not look for debug symbols')
    ap.add_argument('paths', nargs='+')
    opt = ap.parse_args()
    for p in opt.paths:
        for name, text in texts(p):
            triage(name, text, opt)
    return 0


if __name__ == '__main__':
    sys.exit(main())
