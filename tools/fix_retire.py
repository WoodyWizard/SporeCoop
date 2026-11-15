#!/usr/bin/env python3
"""Fix-switch retirement (0.7.15 B1, owner decision 4; docs/10-dev-pipeline.md#pipeline-fix-retire).

  fix_retire.py list [--upto 0.7.9] [--all] [--json]
                    the switches that may retire, each with its evidence: the fix was released 3 or more releases
                    before the current one (and, with --upto, in that release or earlier), a FAIL run with the switch
                    off is on record (the ticket names a FAIL run id, or a recorded lab result with
                    `SPORE_FIX_OFF: <switch> off` failed: `lab.py fixcheck`), and the switch is on no keep list.
                    --all also prints the kept switches with the reason
  fix_retire.py check
                    (make test) no retired switch (tools/fix_retired.txt) is registered again or named by a test,
                    a lab tool or a current doc (tickets, the CHANGELOG and docs/archive keep their history)

`lab.py fixcheck --retire [--upto X]` is the same as `list`.

The keep list (a switch stays even when it qualifies):
  untested   no test or lab tool names the switch (the 152 of the audit: no FAIL run can be made for them in the
             lab, owner decision 4)
  space      a switch in a Space file (space*.cpp, squadron.cpp, spacecoop.cpp): refactored by its own package
  option     the off path is a documented user or debug option, or a test seam other tests need (KEEP below)
  manual     the off path and the on path are interleaved so that deleting one is not obviously correct (KEEP)
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
COOP = os.path.join(ROOT, 'mods', 'coop')
TICKETS = os.path.join(ROOT, 'docs', 'tickets')
RETIRED = os.path.join(HERE, 'fix_retired.txt')
AGE = 3  # releases between the fix's release and the current one

REG = re.compile(r'^\s*COOP_FIX(_UNSAFE)?\(\s*"([^"]+)",\s*"([^"]*)",\s*([^,]+?)\s*,', re.M)
SPACE_FILE = re.compile(r'^(space\w*|squadron|spacecoop)\.(cpp|h)$')
SPACE_SHARED = {'shared.cpp'}  # the Space package (0.7.15) rewrites it too
SPACE_AREAS = {'space', 'fleet', 'squadron'}
RUN_ID = re.compile(r'\b20\d{6}-\d{6}\b')
FAIL_WORD = re.compile(r'\bFAIL(S|ED)?\b|\bfails\b|\bfailed\b')

# switches kept although they qualify, with the reason (owner decision 4: options, seams, interleaved paths)
SEAM = 'seam (another test switches it off for its own setup)'
KEEP = {
    # test seams: switched off by tests that are not its own twin
    'creature.partner_hp': 'seam (coop_monitor_plant plants its off side as a known fault)',
    'npc.party_peace': 'seam (coop_monitor_plant plants its off side as a known fault)',
    'creature.puppet_ground': 'seam (coop_monitor_plant plants its off side as a known fault)',
    'parts.shared': 'seam (coop_monitor_plant plants its off side as a known fault)',
    'effects.other_stages': 'seam (coop_tribe_guest_baby --no-effects)',
    'npc.strays': SEAM + ': coop_old_species, coop_fix_registry',
    'npc.stray_ids': SEAM + ': coop_creature_desync, coop_creature_e84',
    'realign.villages': SEAM + ': coop_tribe_guest_tribes',
    'tribe.guest_tribes': 'seam (coop_tribe_guest_tribes checks the off side in its PASS run)',
    'tribe.form_pace': SEAM + ': coop_tribe_copy_marker, coop_wsync_tribe',
    'core.editor_own_pause': SEAM + ': coop_exit_editor_held --held (its default)',
    'world.intro_at_once': SEAM + ': coop_tribe_camera_null',
}


def vtuple(v):
    parts = re.findall(r'\d+', v or '')
    return tuple(int(x) for x in parts[:3]) if len(parts) >= 3 else None


def releases():
    """the released X.Y.Z versions (hotfixes X.Y.Z.W count with their base), oldest first"""
    try:
        tags = subprocess.run(['git', '-C', ROOT, 'tag', '--list', 'v*'], capture_output=True, text=True).stdout.split()
    except OSError:
        tags = []
    if not tags:  # no history (a source copy): the CHANGELOG's sections
        tags = re.findall(r'^## \[(\d[\d.]*)\]', open(os.path.join(ROOT, 'CHANGELOG.md')).read(), re.M)
    return sorted({vtuple(t) for t in tags if vtuple(t)})


def current_release(rel):
    """the release being made: the next X.Y.Z after the newest tag"""
    last = rel[-1]
    return (last[0], last[1], last[2] + 1)


def registrations():
    """[{name, ticket, var, file, line, unsafe}] of every COOP_FIX in mods/coop"""
    out = []
    for f in sorted(os.listdir(COOP)):
        if not f.endswith('.cpp'):
            continue
        text = open(os.path.join(COOP, f), encoding='utf-8', errors='replace').read()
        for m in REG.finditer(text):
            out.append({'name': m.group(2), 'tickets': re.findall(r'BUG-\d{4}', m.group(3)), 'var': m.group(4),
                        'file': f, 'line': text.count('\n', 0, m.start()) + 1, 'unsafe': bool(m.group(1))})
    return out


def tickets():
    out = {}
    for n in sorted(os.listdir(TICKETS)):
        m = re.match(r'^(BUG-\d{4})-.*\.md$', n)
        if not m:
            continue
        text = open(os.path.join(TICKETS, n), encoding='utf-8').read()
        meta = {}
        fm = re.match(r'---\n(.*?)\n---\n', text, re.S)
        for line in (fm.group(1).splitlines() if fm else []):
            if ':' in line:
                k, v = line.split(':', 1)
                meta[k.strip()] = v.strip()
        meta['_text'] = text
        meta['_path'] = os.path.join(TICKETS, n)
        out[m.group(1)] = meta
    return out


def test_files():
    """{path: text} of the tests and the lab tools that can name a switch"""
    out = {}
    for d in ('tools/tests', 'tools'):
        for f in sorted(os.listdir(os.path.join(ROOT, d))):
            p = os.path.join(ROOT, d, f)
            if os.path.isfile(p) and f.endswith(('.py', '.sh')) and f != 'fix_retire.py':
                out[os.path.join(d, f)] = open(p, encoding='utf-8', errors='replace').read()
    return out


def code_files():
    """{path: lines} of the code that can read a switch: mods/coop (with net/), updater/, the unit tests"""
    out = {}
    for d in ('mods/coop', 'mods/coop/net', 'updater', 'tests'):
        for f in sorted(os.listdir(os.path.join(ROOT, d))):
            if f.endswith(('.cpp', '.h')):
                p = os.path.join(d, f)
                out[p] = open(os.path.join(ROOT, p), encoding='utf-8', errors='replace').read().split('\n')
    return out


def code_index(code):
    """{identifier: [(file, line)]} of the code lines (comments left out)"""
    idx = {}
    for p, lines in code.items():
        for i, l in enumerate(lines, 1):
            c = l.split('//')[0]
            if 'COOP_FIX' in c:
                continue
            for w in set(re.findall(r'\w+', c)):
                idx.setdefault(w, []).append((p, i))
    return idx


def uses(s, code, idx, shared_names):
    """[(file, line)] of the code lines that read or write the switch's bool (comments left out). A bool name that
    several files define (g_fix, g_path_fix) counts only in its own file and where it is written with its scope"""
    var = s['var'].split('::')[-1].split('.')[-1]
    if var not in shared_names:
        return list(idx.get(var, []))
    scope = s['var'].replace(' ', '')
    return [(p, i) for p, i in idx.get(var, [])
            if os.path.basename(p) == s['file'] or (('::' in scope or '.' in scope) and scope in code[p][i - 1])]


def aliases(s, code, use):
    """the names of the old per-module commands for the switch (`coop.pool fix <alias>`, `coop.worldfix <alias>`,
    `coop.corefix <alias>`): string literals on the line that writes the bool or lists it in a {"name", &bool}
    table, or on the line before"""
    var = s['var'].split('::')[-1].split('.')[-1]
    write = re.compile(r'&(?:\w+(?:::|\.))*' + re.escape(var) + r'\b|\b' + re.escape(var) + r'\s*=[^=]')
    out = set()
    for p, i in use:
        l = code[p][i - 1]
        if not write.search(l) or re.match(r'\s*(static\s+)?(bool|FIX_SWITCH)\b', l):
            continue
        m = re.search(r'"(\w+)",\s*&(?:\w+(?:::|\.))*' + re.escape(var) + r'\b', l)
        if m:
            out.add(m.group(1))
            continue
        prev = code[p][i - 2] if i >= 2 else ''
        out.update(re.findall(r'== "(\w+)"|strcmp\([^,]+, "(\w+)"\)', prev) and
                   [x for t in re.findall(r'== "(\w+)"|strcmp\([^,]+, "(\w+)"\)', prev) for x in t if x] or [])
    return sorted(out)


TOKEN = re.compile(r'(?<![\w.])[A-Za-z_][\w.]*\w')


def ticket_fail_runs(t, name, only_switch):
    """the run ids a ticket names in a sentence that says FAIL (with the switch named there, or the ticket's
    only switch)"""
    text = t['_text'].split('\n---\n', 1)[-1]
    runs = []
    for sent in re.split(r'(?<=[.;])\s+|\n\s*[-*]\s|\n\n', text):
        if not FAIL_WORD.search(sent) or not RUN_ID.search(sent):
            continue
        if name in sent or (only_switch and re.search(r'SPORE_FIX_OFF|\boff\b|coop\.fix|--old|without the fix|'
                                                      r'fix off|switch', sent)):
            runs += RUN_ID.findall(sent)
    return sorted(set(runs))


def lab_fail_runs():
    """{switch: ([FAIL run dirs], [PASS run dirs])} of every recorded lab result with switches off"""
    sys.path[:0] = [HERE, os.path.join(HERE, 'tests')]
    import labcov
    import labverdict
    import loggates
    out = {}
    dirs = labcov.default_dirs()
    # the logs that switched anything off (grep first: reading every log of every run takes minutes)
    found = subprocess.run(['grep', '-rlE', '--include=*.log', 'SPORE_FIX_OFF[^:]*: *[a-z_]+\\.[a-z_0-9]+ off', *dirs],
                           capture_output=True, text=True).stdout.split('\n')
    with_off = set(filter(None, found))
    for rundir, _data, r in loggates.recorded(dirs):
        if labverdict.was_inconclusive(r):
            continue
        off = r.get('fix_off') or ''
        if not off:
            _, tlog = loggates.result_logs(rundir, r)
            off = labverdict.fix_off_in_log(tlog) if tlog in with_off else ''
        for sw in filter(None, (x.strip() for x in off.split(','))):
            ok = bool(r.get('ok')) and not r.get('skipped')
            out.setdefault(sw, ([], []))[1 if ok else 0].append(os.path.basename(rundir) + ':' + r['name'])
    return out


def inventory(upto=None, lab=True):
    rel = releases()
    cur = current_release(rel)
    rank = {v: i for i, v in enumerate(rel)}
    tk = tickets()
    tokens = {}
    for p, text in test_files().items():
        for tok in set(TOKEN.findall(text)):
            tokens.setdefault(tok, set()).add(p)
    labruns = lab_fail_runs() if lab else {}
    regs = registrations()
    code = code_files()
    idx = code_index(code)
    import collections
    defs = collections.Counter(r['var'].split('::')[-1].split('.')[-1] for r in regs)
    shared_names = {v for v, n in defs.items() if n > 1} | {'g_fix'}
    alltext = {p: t for p, t in test_files().items()}
    by_ticket = {}
    for s in regs:
        for t in s['tickets']:
            by_ticket.setdefault(t, []).append(s['name'])
    rows = []
    for s in regs:
        ts = [tk[t] for t in s['tickets'] if t in tk]
        fixed = [vtuple(t.get('fixed_in')) for t in ts]
        s['fixed_in'] = '.'.join(map(str, max(fixed))) if fixed and all(fixed) else None
        age = len(rel) - rank[max(fixed)] if fixed and all(fixed) and max(fixed) in rank else None
        s['age'] = age
        named = sorted(tokens.get(s['name'], ()))
        use = uses(s, code, idx, shared_names)
        s['uses'] = [f'{p}:{i}' for p, i in use]
        s['aliases'] = aliases(s, code, use)
        legacy = []
        for al in s['aliases']:
            pat = re.compile(r'coop\.(?:\w*fix|\w+ \w*fix\w*)\s+' + re.escape(al) + r'\b')
            legacy += [p for p, t in alltext.items() if pat.search(t)]
        s['alias_tests'] = sorted(set(legacy))
        named = sorted(set(named) | set(legacy))
        s['tests'] = named
        ufiles = {os.path.basename(p) for p, _ in use} | {s['file']}
        ev = []
        for tid, t in zip([x for x in s['tickets'] if x in tk], ts):
            for run in ticket_fail_runs(t, s['name'], len(by_ticket.get(tid, [])) == 1):
                ev.append(f'{tid} {run}')
        fails, passes = labruns.get(s['name'], ([], []))
        ev += [f'lab {x}' for x in fails[:3]]
        s['evidence'] = ev
        s['lab_fail'], s['lab_pass'] = len(fails), len(passes)
        why = []
        if s['fixed_in'] is None:
            why.append('no fixed ticket')
        elif age < AGE:
            why.append(f'released {age} release(s) ago')
        elif upto and vtuple(s['fixed_in']) > vtuple(upto):
            why.append(f'after {upto}')
        if any(SPACE_FILE.match(f) or f in SPACE_SHARED for f in ufiles) or s['name'].split('.')[0] in SPACE_AREAS:
            why.append('space')
        if s['file'] == 'fix_table.cpp' or any(p.startswith(('tests/', 'mods/coop/net/', 'updater/')) for p, _ in use):
            why.append('unit (a portable switch, or the unit tests run its off side)')
        if not named:
            why.append('untested')
        if s['name'] in KEEP:
            why.append(KEEP[s['name']])
        if not ev:
            why.append('no FAIL run on record')
        s['keep'] = why
        rows.append(s)
    return rows, cur


def cmd_list(a):
    rows, cur = inventory(a.upto)
    cand = [r for r in rows if not r['keep']]
    if a.json:
        json.dump(rows if a.all else cand, sys.stdout, indent=1)
        print()
        return 0
    print(f'# Fix switches that may retire in {".".join(map(str, cur))} (docs/10-dev-pipeline.md#pipeline-fix-retire)\n')
    print(f'{len(rows)} switches, {len(cand)} candidates' + (f' (fixed in {a.upto} or earlier)' if a.upto else '') + '\n')
    print('| switch | file | ticket | fixed in | tests | evidence |' + (' kept |' if a.all else ''))
    print('|---|---|---|---|---|---|' + ('---|' if a.all else ''))
    for r in rows if a.all else cand:
        print(f'| {r["name"]} | {r["file"]} | {" ".join(r["tickets"])} | {r["fixed_in"]} | {len(r["tests"])} | '
              f'{"; ".join(r["evidence"][:3])} |' + (f' {", ".join(r["keep"])} |' if a.all else ''))
    if a.all:
        from collections import Counter
        c = Counter(k.split(' ')[0] if not k.startswith('released') else 'recent' for r in rows for k in r['keep'])
        print('\nkept by reason (a switch can have several): ' + ', '.join(f'{k} {n}' for k, n in c.most_common()))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    li = sub.add_parser('list')
    li.add_argument('--upto', help='only fixes released in this version or earlier (the batch)')
    li.add_argument('--all', action='store_true', help='also the kept switches, with the reason')
    li.add_argument('--json', action='store_true')
    sub.add_parser('check')
    a = ap.parse_args(argv)
    if a.cmd == 'list':
        return cmd_list(a)
    return cmd_check(a)


def retired():
    """({name: line} of the retired switches, [old commands removed with them]) from tools/fix_retired.txt"""
    names, cmds = {}, []
    if not os.path.exists(RETIRED):
        return names, cmds
    for line in open(RETIRED, encoding='utf-8'):
        line = line.split('#', 1)[0].strip()
        if line.startswith('command '):
            cmds.append(line[len('command '):].strip())
        elif line:
            names[line.split()[0]] = line
    return names, cmds


# where a retired switch must not be named any more: the tests and the lab's tools (any mention), the current docs (a
# use: `coop.fix <name>`, SPORE_FIX_OFF=..., SPORE_LAB_FIXOFF=...). History keeps its names: the tickets, the
# CHANGELOG, docs/archive, the audits and plans, and docs/re's labels of a fix ("(pool.ko_grace, BUG-0238)")
HISTORY = re.compile(r'^(docs/(tickets|archive|audit-[\d.]+)/|docs/plan-|CHANGELOG\.md$)')
DOC_USE = r'coop\.fix\s+{n}\b|SPORE_(?:LAB_)?FIX_?OFF=[^\s`)]*(?<![\w.]){n}(?![\w])'


def doc_files():
    out = []
    for d, _, fs in os.walk(ROOT):
        rel = os.path.relpath(d, ROOT)
        if rel.split(os.sep)[0] in ('.git', 'build', 'out', 'third_party', 'sdk', 'node_modules'):
            continue
        for f in fs:
            if f.endswith('.md'):
                p = os.path.normpath(os.path.join(rel, f))
                if not HISTORY.match(p):
                    out.append(p)
    return sorted(out)


def check():
    """[problem] (empty: clean)"""
    names, cmds = retired()
    if not names and not cmds:
        return []
    out = []
    regs = {r['name']: r for r in registrations()}
    for n in names:
        if n in regs:
            out.append(f'mods/coop/{regs[n]["file"]}:{regs[n]["line"]}: retired switch {n} is registered again '
                       '(a new fix takes a new name)')
    tok = {n: re.compile(r'(?<![\w.])' + re.escape(n) + r'(?![\w])') for n in names}
    cmd = [(c, re.compile(r'(?<![\w.])' + re.escape(c) + r'(?![\w])')) for c in cmds]
    for p, text in test_files().items():
        for i, line in enumerate(text.split('\n'), 1):
            for n, rx in tok.items():
                if rx.search(line):
                    out.append(f'{p}:{i}: names the retired switch {n}')
            for c, rx in cmd:
                if rx.search(line):
                    out.append(f'{p}:{i}: uses the removed command `{c}`')
    uses = {n: re.compile(DOC_USE.format(n=re.escape(n))) for n in names}
    for p in doc_files():
        for i, line in enumerate(open(os.path.join(ROOT, p), encoding='utf-8', errors='replace'), 1):
            for n, rx in uses.items():
                if rx.search(line):
                    out.append(f'{p}:{i}: switches off the retired switch {n}')
            for c, rx in cmd:
                if rx.search(line):
                    out.append(f'{p}:{i}: uses the removed command `{c}`')
    for f in sorted(os.listdir(COOP)):
        if f.endswith(('.cpp', '.h')):
            for i, line in enumerate(open(os.path.join(COOP, f), encoding='utf-8', errors='replace'), 1):
                for c, rx in cmd:
                    if rx.search(line):
                        out.append(f'mods/coop/{f}:{i}: names the removed command `{c}`')
    return out


def cmd_check(a):
    names, cmds = retired()
    probs = check()
    for x in probs:
        print(x)
    print(f'fix_retire check: {len(names)} retired switches, {len(cmds)} removed commands, {len(probs)} problem(s)')
    return 1 if probs else 0


if __name__ == '__main__':
    sys.exit(main())
