#!/usr/bin/env python3
"""B5 (BUG-1123): tools/crash_triage.py names the functions of coop.dll / sporekit.dll frames with the debug
symbols of the report's version (make test, part unit-symbols, on the local build):

  triage_symbols_check.py <build dir> <llvm-mingw dir>

* tools/symbols_pack.sh (what the release job publishes) packs the local build; a made-up crash report of
  that version with a fault in coop.dll (sporekit_mod_main+0x40) and a sporekit.dll frame in the chain;
* --symbols <the .tar.xz> and --symbols <build dir>: the fault is named sporekit_mod_main+0x40 at
  mods/coop/coop.cpp:<line> (or the phase function inlined there, "inlined into sporekit_mod_main+0x40"), the
  sporekit frame by its function;
* a report of another version with --symbols <build dir>: refused (the offsets would name wrong code);
* the download path with a fake `gh` first on PATH (no network): the asset is fetched, checked against the
  release's SHA256SUMS.txt and cached; a second run with a failing gh symbolises from the cache; a wrong
  sha256 is refused and nothing is cached; gh failing without a cache prints `symbols    none` and exits 0;
* a report older than 0.7.15 (no asset) never calls gh.
SELFTEST_TRIAGE=<old copy of crash_triage.py>: the same checks against an older tool (must FAIL)."""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TRIAGE = os.environ.get('SELFTEST_TRIAGE', os.path.join(ROOT, 'tools', 'crash_triage.py'))
fails = []

FAKE_GH = r'''#!/usr/bin/env python3
import os, shutil, sys
open(os.environ['FAKE_GH_LOG'], 'a').write(' '.join(sys.argv[1:]) + '\n')
if os.environ.get('FAKE_GH_FAIL'):
    sys.stderr.write('release not found\n')
    sys.exit(1)
a = sys.argv[1:]
assert a[:2] == ['release', 'download'], a
tag, d = a[2], a[a.index('--dir') + 1]
pats = [a[i + 1] for i, x in enumerate(a) if x == '--pattern']
ver = tag[1:]
asset = 'SporeCoop-%s-debug-symbols.tar.xz' % ver
os.makedirs(d, exist_ok=True)
if asset in pats:
    shutil.copy(os.environ['FAKE_GH_TAR'], os.path.join(d, asset))
if 'SHA256SUMS.txt' in pats:
    open(os.path.join(d, 'SHA256SUMS.txt'), 'w').write(
        '%s  SporeCoop-%s.zip\n%s  %s\n' % ('0' * 64, ver, os.environ['FAKE_GH_SHA'], asset))
'''


def check(cond, what):
    print(('  ok   ' if cond else '  FAIL ') + what)
    if not cond:
        fails.append(what)


def text_symbol(nm, dll, pattern):
    """(address, name) of the first defined text symbol of dll matching pattern"""
    out = subprocess.run([nm, '--defined-only', dll], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in 'Tt' and re.search(pattern, p[2]):
            return int(p[0], 16), p[2]
    raise SystemExit(f'triage_symbols_check: no text symbol {pattern} in {dll}')


def image_base(readobj, dll):
    out = subprocess.run([readobj, '--file-headers', dll], capture_output=True, text=True, check=True).stdout
    return int(re.search(r'ImageBase:\s*(0x[0-9A-Fa-f]+)', out).group(1), 16)


def report(path, ver, coop_rva, kit_rva):
    with open(path, 'w') as f:
        f.write('SporeCoop crash 20261115-000000 (unhandled exception)\n\n== Environment\n'
                'SporeKit 0.3.18, game GOG 2024 (exe dc04aee5a3debc3f), frames from Present\n'
                f'game mode: Creature, frame 1, up 1 s\nmods: coop {ver}  \n\n== Exception\n'
                f'0xC0000005 access violation at 0x6C3EED1E coop.i1.1.dll+0x{coop_rva:X} (thread 1)\n'
                'read of address 0x00000160\n\n== Call chain (frame pointers, then return addresses found on the stack)\n'
                f'  6C3EED1E coop.i1.1.dll+0x{coop_rva:X}\n  6A019825 sporekit.dll+0x{kit_rva:X}\n')


def run(args, env):
    r = subprocess.run([sys.executable, '-I', TRIAGE, *args], capture_output=True, text=True, env=env, timeout=600)
    return r.returncode, r.stdout + r.stderr


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    build, llvm = sys.argv[1], sys.argv[2]
    nm, readobj = os.path.join(llvm, 'bin', 'llvm-nm'), os.path.join(llvm, 'bin', 'llvm-readobj')
    for f in ('coop.dll', 'sporekit.dll', 'steam_api.dll'):
        if not os.path.isfile(os.path.join(build, f)):
            print(f'triage_symbols_check: {build}/{f} missing: build first (make)')
            return 1
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import pe_version
    ver = pe_version.read(os.path.join(build, 'coop.dll'))[1].get('ProductVersion')
    print(f'triage_symbols: crash_triage.py with the debug symbols of the local build (coop {ver})')
    tmp = tempfile.mkdtemp(prefix='sk-triage-sym-')
    try:
        a, fn = text_symbol(nm, os.path.join(build, 'coop.dll'), r'^_sporekit_mod_main$')
        coop_rva = a - image_base(readobj, os.path.join(build, 'coop.dll')) + 0x40
        b, kfn = text_symbol(nm, os.path.join(build, 'sporekit.dll'), r'^__ZN2sk\w*\d\w+Ev$')
        kit_rva = b - image_base(readobj, os.path.join(build, 'sporekit.dll')) + 5  # a return address inside it
        kit_name = subprocess.run([os.path.join(llvm, 'bin', 'llvm-cxxfilt'), '-_', kfn], capture_output=True,
                                  text=True).stdout.strip() or kfn
        kit_name = kit_name.split('(')[0]
        tar = subprocess.run(['bash', os.path.join(ROOT, 'tools', 'symbols_pack.sh'), build, ver, tmp],
                             capture_output=True, text=True)
        tar_path = tar.stdout.strip().splitlines()[-1] if tar.returncode == 0 and tar.stdout.strip() else ''
        check(os.path.isfile(tar_path), f'symbols_pack.sh packs the build ({tar_path or tar.stderr.strip()[-200:]})')
        rep = os.path.join(tmp, 'crash-20261115-000000.txt')
        report(rep, ver, coop_rva, kit_rva)
        env = dict(os.environ, SPORECOOP_SYMBOLS_CACHE=os.path.join(tmp, 'cache'), LLVM_MINGW=llvm)
        # sporekit_mod_main+0x40 may lie in a phase function inlined into it (B2, BUG-1128): then the frame names that
        # function at its coop.cpp line, `(inlined into sporekit_mod_main+0x40)`
        want = re.compile(rf'^symbol\s+coop\.dll\+0x{coop_rva:X}\s+(?:sporekit_mod_main\+0x40\s+mods/coop/coop\.cpp:\d+'
                          rf'|\S+\s+mods/coop/coop\.cpp:\d+\s+\(inlined into sporekit_mod_main\+0x40\))', re.M)
        want_kit = re.compile(rf'^symbol\s+sporekit\.dll\+0x{kit_rva:X}\s+.*{re.escape(kit_name)}', re.M)
        for what, sym in (('the release asset (.tar.xz)', tar_path), ('a local build folder', build)):
            rc, out = run(['--symbols', sym, rep], env)
            check(rc == 0 and want.search(out) is not None,
                  f'--symbols {what}: the fault is sporekit_mod_main+0x40 at mods/coop/coop.cpp '
                  f'({[l for l in out.splitlines() if l.startswith("symbol")][:3]})')
            check(want_kit.search(out) is not None, f'--symbols {what}: the sporekit.dll frame is in {kit_name}')
        other = os.path.join(tmp, 'crash-20261115-000001.txt')
        report(other, '0.7.13', coop_rva, kit_rva)
        rc, out = run(['--symbols', build, other], env)
        check(rc == 0 and 'the report is coop 0.7.13' in out and not want.search(out),
              f'a coop 0.7.13 report with a {ver} build is refused ({[l for l in out.splitlines() if l.startswith("symbol")]})')

        # the download: a fake gh, no network
        bindir = os.path.join(tmp, 'bin')
        os.makedirs(bindir)
        with open(os.path.join(bindir, 'gh'), 'w') as f:
            f.write(FAKE_GH.replace('/usr/bin/env python3', sys.executable, 1))
        os.chmod(os.path.join(bindir, 'gh'), 0o755)
        log = os.path.join(tmp, 'gh.log')
        sha = hashlib.sha256(open(tar_path, 'rb').read()).hexdigest() if tar_path else '0' * 64
        new = '0.9.99'
        rep99 = os.path.join(tmp, 'crash-20261115-000099.txt')
        report(rep99, new, coop_rva, kit_rva)
        genv = dict(env, PATH=bindir + os.pathsep + env.get('PATH', ''), FAKE_GH_LOG=log, FAKE_GH_TAR=tar_path,
                    FAKE_GH_SHA=sha)
        rc, out = run([rep99], genv)
        calls = open(log).read() if os.path.exists(log) else ''
        check(f'release download v{new} --repo WoodyWizard/SporeCoop' in calls
              and f'SporeCoop-{new}-debug-symbols.tar.xz' in calls, f'coop {new}: gh downloads the asset ({calls.strip()})')
        check(rc == 0 and want.search(out) is not None and 'sha256 checked' in out,
              f'the downloaded symbols name the fault, sha256 checked '
              f'({[l for l in out.splitlines() if l.startswith("symbol")][:2]})')
        check(os.path.isfile(os.path.join(tmp, 'cache', new, 'coop.dll')), 'the asset is cached by version')
        rc, out = run([rep99], dict(genv, FAKE_GH_FAIL='1'))
        check(rc == 0 and want.search(out) is not None and 'cached release asset' in out,
              'a second run uses the cache (gh failing now)')
        rc, out = run([rep99], dict(genv, SPORECOOP_SYMBOLS_CACHE=os.path.join(tmp, 'cache2'), FAKE_GH_SHA='1' * 64))
        check(rc == 0 and not want.search(out) and "is not the release's" in out
              and not os.path.exists(os.path.join(tmp, 'cache2', new)),
              f'a wrong sha256 is refused, nothing cached ({[l for l in out.splitlines() if l.startswith("symbols")]})')
        rc, out = run([rep99], dict(genv, SPORECOOP_SYMBOLS_CACHE=os.path.join(tmp, 'cache3'), FAKE_GH_FAIL='1'))
        check(rc == 0 and re.search(rf'^symbols\s+none for coop {re.escape(new)}: gh release download', out, re.M) is not None
              and 'signature' in out, 'gh failing without a cache: `symbols    none`, the rest of the triage stays')
        open(log, 'w').close()
        old = os.path.join(tmp, 'crash-20261031-000000.txt')
        report(old, '0.7.3.1', coop_rva, kit_rva)
        rc, out = run([old], dict(genv, SPORECOOP_SYMBOLS_CACHE=os.path.join(tmp, 'cache4')))
        check(rc == 0 and open(log).read() == '' and 'older than the first release with a symbols asset' in out,
              'a coop 0.7.3.1 report (no asset) never calls gh and says how to build the tag')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f'triage_symbols_check: {len(fails)} failure(s)')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
