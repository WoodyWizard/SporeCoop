#!/usr/bin/env python3
"""Knowledge check (docs/10-dev-pipeline.md): every game symbol the code asks for by name must be
in data/symbols/*.sym of EVERY supported build (GOG 2016 is frozen after 0.7: only noted) (<name>_<exe hash>.sym; a missing name returns 0 and
silently turns a feature off on that build), and every entry of the hand-written overrides files
must say how sure it is (verified / likely / unknown)."""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
known = {}
by_build = {}  # exe hash (file name suffix) -> names
checked = {}  # exe hash -> names in the hand-written overrides file (a signature checked by a person)
for f in sorted(glob.glob(os.path.join(ROOT, 'data', 'symbols', '*.sym'))):
    build = os.path.basename(f)[:-4].rsplit('_', 1)[-1]
    for i, line in enumerate(open(f, errors='replace'), 1):
        m = re.match(r'\s*([^=;#\s]+)\s*=\s*(0x[0-9A-Fa-f]+)\s*(;.*)?$', line)
        if m:
            by_build.setdefault(build, set()).add(m.group(1))
            if os.path.basename(f).startswith('overrides'):
                checked.setdefault(build, set()).add(m.group(1))
            known.setdefault(m.group(1), (os.path.basename(f), i, (m.group(3) or '').strip()))
used = {}
SRC = sorted(glob.glob(os.path.join(ROOT, 'mods', '**', '*.cpp'), recursive=True)) + \
    sorted(glob.glob(os.path.join(ROOT, 'mods', '**', '*.h'), recursive=True)) + \
    sorted(glob.glob(os.path.join(ROOT, 'sporekit', '*.cpp'))) + sorted(glob.glob(os.path.join(ROOT, 'sporekit', '*.h')))
for f in SRC:
    for i, line in enumerate(open(f, errors='replace'), 1):
        where = f'{os.path.relpath(f, ROOT)}:{i}'
        for m in re.finditer(r'\b(?:sym|S|sym_addr)\(\s*"([A-Za-z_][\w:]*)"\s*\)', line):
            used.setdefault(m.group(1), where)
        if 're_lint: not-a-sym' in line:
            continue
        # BUG-0216: names looked up through a table (`rets[i]`, `try_hook("App::...")`, `req[k]`): a
        # string literal that is exactly a qualified name of a .sym file, or any literal on a line
        # marked `// re_lint: sym`
        for lit in re.findall(r'"([A-Za-z_][\w:]*)"', line):
            if ('::' in lit and lit in known) or 're_lint: sym' in line:
                used.setdefault(lit, where)
# BUG-0430: the test tools ask the game for symbols by name too (`call Simulator::cCivExpansion::Get thiscall`,
# `sym`, `mem.read`, `probe`); a name missing from a .sym makes the console answer an error and the test check
# something else. tools_selftest.py holds made-up names on purpose. Tests run on the supported build only:
# a name only the tools use needs no frozen_off line.
code_used = set(used)
for f in sorted(glob.glob(os.path.join(ROOT, 'tools', '*.py')) + glob.glob(os.path.join(ROOT, 'tools', 'tests', '*.py'))):
    if os.path.basename(f) in ('tools_selftest.py', 're_lint.py'):
        continue
    for i, line in enumerate(open(f, errors='replace'), 1):
        if 're_lint: not-a-sym' in line:
            continue
        where = f'{os.path.relpath(f, ROOT)}:{i}'
        for m in re.finditer(r'\b(?:sym|call|probe|mem\.read|mem\.write|mem\.ptr)\s+([A-Za-z_]\w*(?:::\w+)+)', line):
            used.setdefault(m.group(1), where)
        for lit in re.findall(r'''['"]([A-Za-z_]\w*(?:::\w+)+)['"]''', line):
            used.setdefault(lit, where)
# builds that are no longer supported (CLAUDE.md: GOG 2016 after 0.7): their symbol files stay
# frozen, code added later may lack their symbols (the feature stays off on such a build)
FROZEN = {'922af7c7'}
frozen_missing = []
bad = 0
for name, where in sorted(used.items()):
    if name not in known:
        print(f'missing symbol {name} (used at {where}): add it to data/symbols/overrides_*.sym')
        bad += 1
    else:
        for build, names in sorted(by_build.items()):
            if name not in names:
                if build in FROZEN:  # no longer supported: a note, not a failure
                    if name in code_used:
                        frozen_missing.append(name)
                    continue
                print(f'missing symbol {name} (used at {where}) for build {build}: port it to '
                      f'data/symbols/overrides_{build}.sym (docs/05-re-notes.md 5.1.1)')
                bad += 1
# BUG-0143: a name only in a generated file (ModAPI's address, "high" / "medium") has no checked
# convention, argument count or return: every name the code uses needs an overrides entry of every
# supported build, with its confidence and signature
for name, where in sorted(used.items()):
    for build in sorted(by_build):
        if build in FROZEN or name not in by_build[build] or name in checked.get(build, ()):
            continue
        print(f'unchecked symbol {name} (used at {where}) for build {build}: only in a generated .sym; check its '
              f'signature in the disassembly and add it to data/symbols/overrides_{build}.sym')
        bad += 1
# every line of every hand-written overrides file, also when a generated file has the same name
# (the usual reason to write an override) or the name appears twice (BUG-0086)
for f in sorted(glob.glob(os.path.join(ROOT, 'data', 'symbols', 'overrides_*.sym'))):
    for i, line in enumerate(open(f, errors='replace'), 1):
        m = re.match(r'\s*([^=;#\s]+)\s*=\s*(0x[0-9A-Fa-f]+)\s*(;.*)?$', line)
        if m and not re.search(r'\b(verified|likely|unknown)\b', m.group(3) or '', re.I):
            print(f'{os.path.basename(f)}:{i} {m.group(1)}: say how sure it is (verified / likely / unknown) '
                  f'and what it does')
            bad += 1
# BUG-0430: the return type. A function that returns a float leaves it in ST0 (x87), not in EAX: called as
# `thiscall<int>` the caller reads garbage, and a call that ignores a float result leaves the x87 stack as the
# game's own callers do only when it pops it. Every name the code calls with a float / double result
# (`thiscall<float>(a_X, ...)`, `cdecl_call<double>(...)`, a cast to `float(__thiscall*)(...)`) says so in its
# overrides entry of every supported build (`returns: float (ST0)`, or the older `-> float`, `float in ST0`);
# and a name whose entry says it returns a float is not called with an integer or pointer result.
FLOAT_NOTE = re.compile(r'returns:\s*(?:float|double)|->\s*(?:float|double)\b|\bST\(?0\)?|\b(?:float|double)\s+in\s+st'
                        r'|\b(?:cdecl|thiscall|stdcall|fastcall)\s+(?:float|double)\b|\bfld\w*\s+(?:result|\[)', re.I)
CALL_T = re.compile(r'\b(?!vcall\b)\w*call<\s*([\w:]+\s*\**)\s*>\(\s*([A-Za-z_]\w*)\b')
CAST_T = re.compile(r'\(\s*(float|double)\s*\(\s*__\w+\s*\*\s*\)\s*\([^)]*\)\s*\)\s*([A-Za-z_]\w*)')
ASSIGN = re.compile(r'\b([A-Za-z_]\w*)\s*=\s*(?:H->)?(?:sym|S|sym_addr)\(\s*"([A-Za-z_][\w:]*)"\s*\)'
                    r'|\{\s*&\s*([A-Za-z_]\w*)\s*,\s*"([A-Za-z_][\w:]*)"\s*\}')
notes = {}  # build -> name -> the overrides entry's comment
for f in sorted(glob.glob(os.path.join(ROOT, 'data', 'symbols', 'overrides_*.sym'))):
    build = os.path.basename(f)[:-4].rsplit('_', 1)[-1]
    for line in open(f, errors='replace'):
        m = re.match(r'\s*([^=;#\s]+)\s*=\s*(0x[0-9A-Fa-f]+)\s*(;.*)?$', line)
        if m:
            notes.setdefault(build, {})[m.group(1)] = m.group(3) or ''
ret_seen = set()
for f in SRC:
    if not f.endswith('.cpp'):
        continue
    lines = open(f, errors='replace').read().split('\n')
    named = {}  # variable -> symbol name: the nearest assignment above, else any in the file (module globals)
    for ln in lines:
        for m in ASSIGN.finditer(ln):
            named.setdefault(m.group(1) or m.group(3), m.group(2) or m.group(4))
    near = {}
    for i, ln in enumerate(lines, 1):
        for m in ASSIGN.finditer(ln):
            near[m.group(1) or m.group(3)] = m.group(2) or m.group(4)
        calls = [(m.group(1).replace(' ', ''), m.group(2)) for m in CALL_T.finditer(ln)] + \
            [(m.group(1), m.group(2)) for m in CAST_T.finditer(ln)]
        for rtype, var in calls:
            name = near.get(var) or named.get(var)
            if not name:
                continue
            where = f'{os.path.relpath(f, ROOT)}:{i}'
            is_float = rtype in ('float', 'double')
            for build in sorted(notes):
                if build in FROZEN or name not in notes[build] or (name, build, is_float) in ret_seen:
                    continue
                says_float = bool(FLOAT_NOTE.search(notes[build][name]))
                if is_float and not says_float:
                    print(f'overrides_{build}.sym {name}: called with a {rtype} result at {where}: say so in its entry '
                          f'(`returns: float (ST0)`) after checking the disassembly (fld / fstp before the ret)')
                    bad += 1
                elif not is_float and rtype not in ('void', 'void*') and says_float and rtype != 'float*':
                    print(f'{where}: {name} is called with a {rtype} result, its overrides_{build}.sym entry says it '
                          f'returns a float in ST0')
                    bad += 1
                ret_seen.add((name, build, is_float))
if frozen_missing:
    # the features behind these names are off on the frozen build (BUG-0108: say which)
    print(f're_lint: {len(frozen_missing)} symbol(s) missing for the frozen build(s) {", ".join(sorted(FROZEN))} '
          f'(not supported, these features are off there): {", ".join(sorted(set(frozen_missing)))}')
# BUG-0216: the one list of what is off on a frozen build, checked against the code: every missing
# name has a line `<name> ; <what is off> (BUG-NNNN)`, every line a name that is used and missing
for build in sorted(FROZEN):
    path = os.path.join(ROOT, 'data', 'symbols', f'frozen_off_{build}.txt')
    listed = {}
    if os.path.exists(path):
        for i, line in enumerate(open(path, encoding='utf-8'), 1):
            if line.strip() and not line.lstrip().startswith('#'):
                m = re.match(r'\s*([A-Za-z_][\w:]*)\s*;\s*(.*\S)\s*$', line)
                if not m or not re.search(r'BUG-\d{4}', m.group(2)):
                    print(f'frozen_off_{build}.txt:{i}: `<symbol> ; <what is off on this build> (BUG-NNNN)` expected')
                    bad += 1
                    continue
                listed[m.group(1)] = i
    missing_here = {n for n in code_used if n in known and n not in by_build.get(build, set())}
    for n in sorted(missing_here - set(listed)):
        print(f'frozen build {build}: {n} (used at {used[n]}) is missing there: add a line to '
              f'data/symbols/frozen_off_{build}.txt saying what is off (BUG-NNNN)')
        bad += 1
    for n in sorted(set(listed) - missing_here):
        print(f'frozen_off_{build}.txt:{listed[n]}: {n} is no longer used or no longer missing: remove the line')
        bad += 1
print(f're_lint: {len(used)} symbols used by the code, {len(known)} known, builds {", ".join(sorted(by_build))}, {bad} problem(s)')
sys.exit(1 if bad else 0)
