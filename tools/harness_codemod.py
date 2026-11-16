#!/usr/bin/env python3
"""The codemod of D4 (0.7.15, BUG-1185, TST-5): the helpers the game tests copied (wait, mode, sp, num, faults, ...)
become imports from tools/tests/harness.py. Run once over tools/tests/coop_*.py; test_lint.py runs its plan() on every
test so a new copy is flagged (docs/06-testing.md#testing-harness).

  harness_codemod.py plan  [files...]        what it would change (default: every tools/tests/coop_*.py)
  harness_codemod.py apply [files...]        change the files
  harness_codemod.py verify <rev> [files...] prove the change of each file against its text at <rev>: the module's
                                             syntax tree is the old one minus the replaced helpers (and the stdlib
                                             imports only they used) plus the harness import / waiter lines, and its
                                             FAIL / RESULT / INCONCLUSIVE / SKIP texts are the same
  harness_codemod.py --self-test

What it replaces, and only that (an odd variant stays as it is):
  * a top-level `def` whose text is byte for byte one of COPIES below: deleted, its name imported from harness
    (`from harness import mode_id as mode`);
  * a top-level `def wait(...)` that is exactly the wait template (WAIT_BODY, BOOL_BODY: poll cond() until truthy,
    the same loop, sleep and end) with some names and defaults: replaced in place by `wait = waiter(...)` with the same
    defaults (harness.waiter: the same loop).
A def is left alone when its name is bound twice at module level or `global` anywhere in the file, when a call of it
(in the file or in a module that imports it from the file) passes a keyword the harness function does not have, or
when the file has a star import. Stdlib imports (`import re`, time, math, os, struct) that only the deleted helpers
used are deleted with them.
"""
import ast
import glob
import inspect
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TESTS = os.path.join(HERE, 'tests')
sys.path.insert(0, TESTS)
import harness  # noqa: E402

# the text of a copy -> the harness function that does the same (harness_selftest.py runs both on the same answers)
COPIES = {
    "def mode(con):\n    return con.call('game.mode')['out']": 'mode',
    "def mode(con):\n    return con.call('game.mode')['out'].strip()": 'mode_strip',
    "def mode(con):\n    out = con.call('game.mode')['out'].split()\n    return out[0] if out else ''": 'mode_word',
    "def mode(con):\n    try:\n        return int(con.call('game.mode')['out'].split()[0], 16)\n"
    "    except (OSError, ValueError, IndexError):\n        return -1": 'mode_id',
    "def sp(con, *a):\n    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()": 'sp',
    "def pool(con):\n    return con.call('coop.pool')['out']": 'pool',
    "def num(out, key):\n    m = re.search(r'\\b' + key + r'=(-?\\d+)', out)\n    return int(m.group(1)) if m else None":
        'num',
    "def num(text, key):\n    m = re.search(r'\\b' + key + r'=(-?\\d+)', text)\n    return int(m.group(1)) if m else None":
        'num',
    "def num(con, key):\n    m = re.search(r'\\b' + key + r'=(-?\\d+)', con.call('coop.pool')['out'])\n"
    "    return int(m.group(1)) if m else None": 'pool_num',
    "def field(out, pat):\n    m = re.search(pat, out)\n    return m.group(1) if m else None": 'field',
    "def field(text, pat):\n    m = re.search(pat, text or '', re.S)\n    return m.group(1) if m else None": 'field_dotall',
    "def faults(con):\n    m = re.search(r'total faults: (\\d+)', con.call('exc')['out'])\n"
    "    return int(m.group(1)) if m else -1": 'faults',
    "def busy(con):\n    for line in con.call('coop.status')['out'].splitlines():\n        if line.startswith('busy:'):\n"
    "            return line.split(':', 1)[1].strip()\n    return '?'": 'busy_line',
    "def busy(con):\n    m = re.search(r'^busy: (\\S+)', con.call('coop.status')['out'], re.M)\n"
    "    return m.group(1) if m else '?'": 'busy_word',
    "def busy(con):\n    m = re.search(r'^busy: (\\S+)', con.call('coop.status')['out'], re.M)\n"
    "    return m.group(1).split(',') if m else ['?']": 'busy_list',
    "def cinematic(con):\n    m = re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])\n"
    "    return m.group(1) if m else None": 'cinematic',
    "def eax(con, what):\n    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call ' + what)['out'])\n"
    "    return int(m.group(1), 16) if m else None": 'eax',
    "def frame(con):\n    m = re.search(r'frame=(\\d+)', con.call('frame').get('out', ''))\n"
    "    return int(m.group(1)) if m else -1": 'frame',
    "def world_of(con):\n    m = re.search(r\"world '([^']*)'\", con.call('coop.avatar')['out'])\n"
    "    return m.group(1) if m else None": 'world_of',
    "def puppet(con):\n    m = re.search(r'puppet=([0-9A-F]{8})', con.call('coop.status')['out'])\n"
    "    return int(m.group(1), 16) if m else 0": 'puppet',
    "def own_pos(con):\n    m = re.search(r'pos=\\(([-\\d.]+) ([-\\d.]+) ([-\\d.]+)\\)', con.call('coop.avatar')['out'])\n"
    "    return tuple(map(float, m.groups())) if m else None": 'avatar_pos',
    "def own_pos(con):\n    m = re.search(r'pos=\\(([-\\d.]+) ([-\\d.]+) ([-\\d.]+)\\)', con.call('coop.avatar')['out'])\n"
    "    return tuple(float(x) for x in m.groups()) if m else None": 'avatar_pos',
    "def pos(con):\n    m = re.search(r'pos=\\(([-\\d.]+) ([-\\d.]+) ([-\\d.]+)\\)', con.call('coop.avatar')['out'])\n"
    "    return tuple(map(float, m.groups())) if m else None": 'avatar_pos',
    "def own_pos(con):\n    m = re.search(r'^avatar .*?pos=\\((-?[\\d.]+) (-?[\\d.]+)\\)', con.call('coop.pool')['out'], "
    "re.MULTILINE)\n    return (float(m.group(1)), float(m.group(2))) if m else None": 'cell_pos',
    "def dist(a, b):\n    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))": 'dist',
    "def dist(a, b):\n    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b))) if a and b else None": 'dist_or_none',
    "def money(con):\n    m = re.search(r'shared money=(-?[\\d.]+)', con.call('coop.res')['out'])\n"
    "    return float(m.group(1)) if m else None": 'shared_money',
    "def food(con):\n    m = re.search(r'shared food=(-?[\\d.]+)', con.call('coop.res')['out'])\n"
    "    return float(m.group(1)) if m else None": 'shared_food',
    "def dna(con):\n    m = re.search(r'dna=(-?[\\d.]+)', con.call('coop.dna')['out'])\n"
    "    return float(m.group(1)) if m else None": 'dna',
    "def log_size(inst):\n    try:\n        return os.path.getsize(lane.game_log(inst))\n    except OSError:\n"
    "        return 0": 'log_size',
    "def log_since(inst, off):\n    try:\n        with open(lane.game_log(inst), 'rb') as f:\n            f.seek(off)\n"
    "            return f.read().decode('utf-8', 'replace').splitlines()\n    except OSError:\n        return []": 'log_since',
    "def pct(xs, q):\n    xs = sorted(xs)\n    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else 0.0": 'pct',
    "def fnv(data):\n    h = 0x811C9DC5\n    for c in data:\n        h = ((h * 0x1000193) & 0xFFFFFFFF) ^ c\n    return h": 'fnv',
}

# the wait templates: {c} the condition's name, {t} the timeout's, {s} the sleep (the step's name or a number), {v} the
# variable, {last} the end (None, or {c}() for one more call)
WAIT_BODY = ('    t0 = time.time()\n    while time.time() - t0 < {t}:\n        {v} = {c}()\n        if {v}:\n'
             '            return {v}\n        time.sleep({s})\n    return {last}')
BOOL_BODY = ('    t0 = time.time()\n    while time.time() - t0 < {t}:\n        if {c}():\n            return True\n'
             '        time.sleep({s})\n    return False')
STDLIB = ('re', 'time', 'math', 'os', 'struct')
_NUM = re.compile(r'^\d+(\.\d+)?$')


def wait_form(fn, text):
    """the waiter(...) arguments when the def is the wait template, else None"""
    if fn.name != 'wait' or fn.decorator_list or fn.args.vararg or fn.args.kwarg or fn.args.kwonlyargs \
            or fn.args.posonlyargs:
        return None
    names = [a.arg for a in fn.args.args]
    defaults = [ast.get_source_segment(text, d) for d in fn.args.defaults]
    if len(names) not in (2, 3) or any(not _NUM.match(d or '') for d in defaults):
        return None
    dflt = dict(zip(names[len(names) - len(defaults):], defaults))
    head = 'def wait(' + ', '.join(n + ('=' + dflt[n] if n in dflt else '') for n in names) + '):\n'
    c, t = names[0], names[1]
    if len(names) == 3:
        if names[2] != 'step' or 'step' not in dflt:
            return None
        sleeps = [('step', dflt['step'])]
    else:
        sleeps = [(n, n) for n in ('0.1', '0.25', '0.3', '0.5', '1', '1.0', '2')]
    src = ast.get_source_segment(text, fn)
    for s, step in sleeps:
        for v in ('r', 'v'):
            for last, final in (('None', False), (c + '()', True)):
                if src == head + WAIT_BODY.format(c=c, t=t, s=s, v=v, last=last):
                    return dict(timeout=dflt.get(t), step=step, final=final, as_bool=False)
        if src == head + BOOL_BODY.format(c=c, t=t, s=s):
            return dict(timeout=dflt.get(t), step=step, final=False, as_bool=True)
    return None


def waiter_call(form):
    a = ([f'timeout={form["timeout"]}'] if form['timeout'] is not None else []) + [f'step={form["step"]}']
    a += ['final=True'] if form['final'] else []
    a += ['as_bool=True'] if form['as_bool'] else []
    return f'waiter({", ".join(a)})'


def local_modules():
    return {os.path.basename(f)[:-3] for d in (HERE, TESTS) for f in glob.glob(os.path.join(d, '*.py'))}


def _parse(path):
    text = open(path, encoding='utf-8').read()
    return text, ast.parse(text)


_IMPORTERS = None


def importers():
    """{module: [(importer path, {local name: name in module}, {alias of the module})]} of tools/ and tools/tests/"""
    global _IMPORTERS
    if _IMPORTERS is None:
        _IMPORTERS = {}
        for f in glob.glob(os.path.join(HERE, '*.py')) + glob.glob(os.path.join(TESTS, '*.py')):
            try:
                _t, tree = _parse(f)
            except (SyntaxError, UnicodeDecodeError):
                continue
            for n in ast.walk(tree):
                if isinstance(n, ast.ImportFrom) and n.module and not n.level:
                    _IMPORTERS.setdefault(n.module, []).append((f, {a.asname or a.name: a.name for a in n.names}, set()))
                elif isinstance(n, ast.Import):
                    for a in n.names:
                        _IMPORTERS.setdefault(a.name, []).append((f, {}, {a.asname or a.name}))
    return _IMPORTERS


def call_keywords(tree, name, modaliases=()):
    """the keyword names of every call of `name` (or `<alias>.name`) in the tree"""
    kws = set()
    for n in ast.walk(tree):
        if not isinstance(n, ast.Call):
            continue
        f = n.func
        if (isinstance(f, ast.Name) and f.id == name) or (isinstance(f, ast.Attribute) and f.attr == name
                                                          and isinstance(f.value, ast.Name) and f.value.id in modaliases):
            kws |= {k.arg for k in n.keywords}  # None: **kwargs
    return kws


def module_bindings(tree):
    """{name: number of module-level bindings}, names declared global anywhere, star import present"""
    count, glob_names, star = {}, set(), False
    for n in tree.body:
        targets = []
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            targets = [n.name]
        elif isinstance(n, (ast.Import, ast.ImportFrom)):
            for a in n.names:
                if a.name == '*':
                    star = True
                targets.append(a.asname or a.name.split('.')[0])
        else:
            for sub in ast.walk(n):
                if isinstance(sub, ast.Name) and isinstance(sub.ctx, ast.Store):
                    targets.append(sub.id)
        for t in targets:
            count[t] = count.get(t, 0) + 1
    for n in ast.walk(tree):
        if isinstance(n, (ast.Global, ast.Nonlocal)):
            glob_names |= set(n.names)
    return count, glob_names, star


def names_used(nodes):
    used = set()
    for node in nodes:
        for n in ast.walk(node):
            if isinstance(n, ast.Name):
                used.add(n.id)
    return used


def plan(path):
    """the change of one file: dict(imports={local: harness name}, waits={def node: waiter call}, drop=[import nodes],
    skipped=[(name, why)]) or None when nothing applies"""
    text, tree = _parse(path)
    mod = os.path.basename(path)[:-3]
    count, globs, star = module_bindings(tree)
    imports, waits, skipped = {}, {}, []
    if star:
        return None
    for fn in tree.body:
        if not isinstance(fn, ast.FunctionDef):
            continue
        src = ast.get_source_segment(text, fn)
        target = COPIES.get(src)
        form = None if target else wait_form(fn, text)
        if not target and not form:
            continue
        if count.get(fn.name, 0) != 1 or fn.name in globs:
            skipped.append((fn.name, 'bound twice or global'))
            continue
        params = set(inspect.signature(harness.waiter() if form else getattr(harness, target)).parameters)
        kws = call_keywords(tree, fn.name)
        for f, names, aliases in importers().get(mod, ()):
            local = [ln for ln, real in names.items() if real == fn.name]
            if local or aliases:
                _t, other = _parse(f)
                for ln in local:
                    kws |= call_keywords(other, ln)
                if aliases:
                    kws |= call_keywords(other, fn.name, aliases)
        if kws - params:
            skipped.append((fn.name, f'called with keyword(s) {sorted(k or "**" for k in kws - params)}'))
            continue
        if form:
            waits[fn] = waiter_call(form)
        else:
            imports[fn.name] = target
    if not imports and not waits:
        return dict(imports={}, waits={}, drop=[], skipped=skipped, text=text, tree=tree) if skipped else None
    if 'waiter' in count and waits:
        skipped.append(('waiter', 'the name is taken'))
        waits = {}
    removed = [fn for fn in tree.body if isinstance(fn, ast.FunctionDef) and (fn.name in imports or fn in waits)]
    kept = [n for n in tree.body if n not in removed]
    used_after, used_before = names_used(kept), names_used(removed)
    drop = [n for n in tree.body if isinstance(n, ast.Import) and len(n.names) == 1 and n.names[0].asname is None
            and n.names[0].name in STDLIB and n.names[0].name in used_before and n.names[0].name not in used_after]
    return dict(imports=imports, waits=waits, drop=drop, skipped=skipped, text=text, tree=tree)


def import_line(p):
    names = sorted(((h, loc) for loc, h in p['imports'].items()), key=lambda x: (x[0], x[1]))
    parts = [h if h == loc else f'{h} as {loc}' for h, loc in names]
    if p['waits']:
        parts.append('waiter')
    line = 'from harness import ' + ', '.join(parts)
    if len(line) <= 120:
        return [line]
    out, cur = [], 'from harness import ('
    for i, part in enumerate(parts):
        piece = part + (', ' if i < len(parts) - 1 else ')')
        if len(cur) + len(piece.rstrip()) > 120:
            out.append(cur.rstrip())
            cur = '                     '
        cur += piece
    out.append(cur)
    return out


def rewrite(p):
    """the new text of the file"""
    text, tree = p['text'], p['tree']
    lines = text.split('\n')
    local = local_modules()
    removed = {fn.lineno: fn for fn in tree.body if isinstance(fn, ast.FunctionDef) and fn.name in p['imports']}
    edits = []  # (first line, last line, replacement lines), 1-based inclusive
    for fn in removed.values():
        end = fn.end_lineno
        while end < len(lines) and lines[end].strip() == '':  # its blank lines after
            end += 1
        if end >= len(lines) or (end == len(lines) - 1 and lines[end] == ''):
            # the last def of the file: the blank lines before it go instead
            start = fn.lineno
            while start > 1 and lines[start - 2].strip() == '':
                start -= 1
            edits.append((start, len(lines), ['']))
        else:
            edits.append((fn.lineno, end, []))
    for fn, call in p['waits'].items():
        edits.append((fn.lineno, fn.end_lineno, [f'wait = {call}']))
    for n in p['drop']:
        edits.append((n.lineno, n.end_lineno, []))
    # the harness import: before the first import of a local module of the header, else after its last import
    header = []
    for n in tree.body:
        if isinstance(n, (ast.FunctionDef, ast.ClassDef, ast.AsyncFunctionDef)):
            break
        if isinstance(n, (ast.Import, ast.ImportFrom)):
            header.append(n)
    first_local = next((n for n in header if n not in p['drop'] and (
        (isinstance(n, ast.ImportFrom) and n.module in local)
        or (isinstance(n, ast.Import) and n.names[0].name in local))), None)
    new_imp = import_line(p)
    if first_local is not None:
        edits.append((first_local.lineno, first_local.lineno - 1, new_imp))  # insert before
    else:
        last = max((n for n in header if n not in p['drop']), key=lambda n: n.end_lineno, default=None)
        at = last.end_lineno if last else 0
        edits.append((at + 1, at, new_imp))
    for start, end, rep in sorted(edits, key=lambda e: (e[0], e[1]), reverse=True):
        lines[start - 1:end] = rep
    out = '\n'.join(lines)
    out = re.sub(r'\n{3,}(?=\Z)', '\n', out)
    if not out.endswith('\n'):
        out += '\n'
    return out


def default_files():
    return sorted(glob.glob(os.path.join(TESTS, 'coop_*.py')))


def cmd_plan(files, apply=False):
    total_defs = total_lines = changed = 0
    for f in files:
        p = plan(f)
        if not p:
            continue
        for name, why in p['skipped']:
            print(f'{os.path.relpath(f, ROOT)}: left {name} ({why})')
        if not p['imports'] and not p['waits']:
            continue
        new = rewrite(p)
        nd = len(p['imports']) + len(p['waits'])
        nl = p['text'].count('\n') - new.count('\n')
        total_defs += nd
        total_lines += nl
        changed += 1
        what = sorted(p['imports']) + [f'wait={c}' for c in p['waits'].values()] + \
            [f'-import {n.names[0].name}' for n in p['drop']]
        print(f"{os.path.relpath(f, ROOT)}: {', '.join(what)} ({nl} lines fewer)")
        if apply:
            ast.parse(new)
            with open(f, 'w', encoding='utf-8') as fh:
                fh.write(new)
    print(f'{"changed" if apply else "would change"} {changed} files: {total_defs} copied helpers, '
          f'{total_lines} lines fewer')
    return 0


def _strip(node):
    return ast.dump(node, include_attributes=False)


def _strings(tree):
    out = []
    for n in ast.walk(tree):
        if isinstance(n, ast.Constant) and isinstance(n.value, str) and re.search(r'FAIL|RESULT|INCONCLUSIVE|SKIP', n.value):
            out.append(n.value)
    return sorted(out)


def verify_pair(old_text, new_text, name='?'):
    """problems of one file's change (empty: only the codemod's edits)"""
    old, new = ast.parse(old_text), ast.parse(new_text)
    probs = []
    if _strings(old) != _strings(new):
        probs.append(f'{name}: the FAIL / RESULT texts differ')
    new_body, waits = [], {}
    for n in new.body:
        if isinstance(n, ast.ImportFrom) and n.module == 'harness':
            for a in n.names:
                if a.name != 'waiter' and COPIES and a.name not in set(COPIES.values()):
                    probs.append(f'{name}: harness import of {a.name}')
            continue
        if isinstance(n, ast.Assign) and len(n.targets) == 1 and isinstance(n.targets[0], ast.Name) \
                and n.targets[0].id == 'wait' and isinstance(n.value, ast.Call) \
                and isinstance(n.value.func, ast.Name) and n.value.func.id == 'waiter':
            waits[len(new_body)] = n
            new_body.append(None)
            continue
        new_body.append(n)
    imported = {a.asname or a.name: a.name for n in new.body if isinstance(n, ast.ImportFrom) and n.module == 'harness'
                for a in n.names}
    old_body = []
    for n in old.body:
        if isinstance(n, ast.FunctionDef):
            src = ast.get_source_segment(old_text, n)
            if n.name in imported and COPIES.get(src) == imported[n.name]:
                continue
            form = wait_form(n, old_text)
            if form:
                old_body.append(('wait', waiter_call(form)))
                continue
        old_body.append(n)
    # the stdlib imports the codemod dropped
    new_imports = {_strip(n) for n in new.body if isinstance(n, ast.Import)}
    old_body = [n for n in old_body if not (isinstance(n, ast.Import) and len(n.names) == 1
                                            and n.names[0].name in STDLIB and _strip(n) not in new_imports)]
    if len(old_body) != len(new_body):
        probs.append(f'{name}: {len(old_body)} module statements before, {len(new_body)} after')
        return probs
    for i, (a, b) in enumerate(zip(old_body, new_body)):
        if b is None:
            if not (isinstance(a, tuple) and ast.unparse(waits[i].value) == a[1]):
                probs.append(f'{name}: statement {i}: a waiter line for something else')
        elif isinstance(a, tuple) or _strip(a) != _strip(b):
            probs.append(f'{name}: statement {i} changed (line {b.lineno})')
    return probs


def cmd_verify(rev, files):
    probs, n = [], 0
    for f in files:
        rel = os.path.relpath(f, ROOT)
        try:
            old = subprocess.run(['git', 'show', f'{rev}:{rel}'], cwd=ROOT, capture_output=True, text=True,
                                 check=True).stdout
        except subprocess.CalledProcessError:
            continue
        new = open(f, encoding='utf-8').read()
        if old == new:
            continue
        n += 1
        probs += verify_pair(old, new, rel)
    for p in probs:
        print('FAIL', p)
    print(f'verify: {n} changed files, {len(probs)} problems')
    return 1 if probs else 0


def self_test():
    import tempfile
    ok = True

    def ck(c, msg):
        nonlocal ok
        print(('  ok   ' if c else '  FAIL ') + msg)
        ok &= bool(c)

    src = ('#!/usr/bin/env python3\n"""doc"""\nimport re\nimport time\n\nfrom twoinst import Checks\n\n\n'
           "def mode(con):\n    return con.call('game.mode')['out']\n\n\n"
           "def num(out, key):\n    m = re.search(r'\\b' + key + r'=(-?\\d+)', out)\n    return int(m.group(1)) if m else None\n\n\n"
           'def wait(cond, secs, step=1.0):\n' + WAIT_BODY.format(c='cond', t='secs', s='step', v='r', last='None') +
           '\n\n\ndef other(con):\n    return mode(con) + "RESULT: FAIL"\n\n\n'
           'def main():\n    print(wait(lambda: 1, 3), other(None))\n    time.sleep(0)\n')
    with tempfile.TemporaryDirectory() as d:
        f = os.path.join(d, 'coop_x.py')
        open(f, 'w').write(src)
        p = plan(f)
        ck(p and p['imports'] == {'mode': 'mode', 'num': 'num'} and list(p['waits'].values()) == ['waiter(step=1.0)'],
           'the copy and the wait template are found')
        ck(p and [n.names[0].name for n in p['drop']] == ['re'], 'only the import the copies alone used is dropped')
        new = rewrite(p)
        ck('from harness import mode, num, waiter\nfrom twoinst import Checks' in new and 'wait = waiter(step=1.0)' in new
           and 'import re\n' not in new and 'import time\n' in new, 'the rewrite')
        ck(verify_pair(src, new) == [], 'verify accepts the codemod\'s own change')
        bad = new.replace('"RESULT: FAIL"', '"RESULT: OK"')
        ck(verify_pair(src, bad) != [], 'verify sees a changed RESULT text')
        bad = new.replace('print(wait(lambda: 1, 3)', 'print(wait(lambda: 1, 4)')
        ck(verify_pair(src, bad) != [], 'verify sees a changed timing')
        odd = src.replace("return con.call('game.mode')['out']", "return con.call('game.mode')['out'] or ''")
        open(f, 'w').write(odd)
        ck('mode' not in plan(f)['imports'], 'an odd variant stays')
        kw = src + '\n\nwait(None, secs=3)\n'
        open(f, 'w').write(kw)
        ck(not plan(f)['waits'], 'a wait called with a keyword the harness lacks stays')
        twice = src + '\n\nmode = None\n'
        open(f, 'w').write(twice)
        ck('mode' not in plan(f)['imports'], 'a name bound twice stays')
    print('RESULT:', 'OK' if ok else 'FAIL')
    return 0 if ok else 1


def main(argv):
    if argv[:1] == ['--self-test']:
        return self_test()
    if not argv or argv[0] not in ('plan', 'apply', 'verify'):
        print(__doc__)
        return 2
    if argv[0] == 'verify':
        files = [os.path.abspath(f) for f in argv[2:]] or default_files()
        return cmd_verify(argv[1], files)
    files = [os.path.abspath(f) for f in argv[1:]] or default_files()
    return cmd_plan(files, apply=argv[0] == 'apply')


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
