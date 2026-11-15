#!/usr/bin/env python3
"""Ratchet lints over mods/coop/*.cpp and *.h (B4 of docs/plan-0.7.15.md, BUG-1131; run by `make test`).
Each check compares the code with tools/ratchet_baseline.json: what is there today is allowed, more is not.
Every file of mods/coop is read (a glob, no file list), so a function or a variable that moves to another file
(the coop.cpp split) keeps its baseline entry: the entries are keyed by namespace, not by file.

  alive      the number of `object_alive(` calls may only go down (CORE-4, STG-9: object_alive on a kept
             pointer reads freed memory; game::LiveRef is the replacement)
  guard      a flag named like a re-entrancy guard (g_in_*, *_applying, *_depth, g_inside* of the audit and
             *_relaying, *_guard, *_bypass of the registered ones; thread_local ones are not checked) that a function sets before it calls an original-function trampoline (o_Xxx(...))
             must be registered with CALL_GUARD / CALL_GUARD_DEPTH (call_guard.h, BUG-1047: a fault the game
             catches inside the call leaves it set), or say why not on its definition or set line:
             `// not a call guard: <reason>` (CORE-5)
  lifecycle  a session / world function (on_session_*, on_world_change / _end / _reset / _start, on_player_join, on_player_leave,
             on_disconnect, on_disconnected) must be registered with the lifecycle registry (lifecycle.h: named in a
             LIFECYCLE_STATE / SESSION_STATE / WORLD_STATE / LIFECYCLE_HOOK of its file), or say why not on its
             definition line or the line above: `// lifecycle-exempt: <reason>` (CORE-3)

A count below the baseline passes and prints how to lower it: `python3 tools/ratchet_lint.py --lower` writes the
lower numbers (it refuses while anything is above). `--self-test` plants a violation of each check in a temporary
tree and fails unless every one is caught. docs/06-testing.md#testing-ratchet-lints.
"""
import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join('tools', 'ratchet_baseline.json')

GUARD_NAME = re.compile(r'^(g_in_\w+|\w*_applying|\w*_depth|g_inside\w*|\w*_relaying|\w*_guard|\w*_bypass)$')
LIFE_NAME = re.compile(r'^on_(session_\w+|world_(?:change|end|reset|start)|player_join|player_leave|disconnect|disconnected)$')
LIFE_MACROS = re.compile(r'\b(LIFECYCLE_STATE|SESSION_STATE|WORLD_STATE|LIFECYCLE_HOOK)\s*\(')
O_CALL = re.compile(r'(?<![\w.>:])o_[A-Za-z]\w*\s*\(')
GUARD_MARK = 'not a call guard:'
LIFE_MARK = 'lifecycle-exempt:'


# ---------------- source model ----------------

def strip(text):
    """code with comments, string and char literals and preprocessor lines blanked (newlines kept, quotes kept), and
    the comment text of every line (1-based)"""
    out = []
    comments = {}
    i, n, line = 0, len(text), 1
    bol = True  # at the start of a line (only blanks so far)

    def note(c):
        comments[line] = comments.get(line, '') + c

    while i < n:
        c = text[i]
        if c == '\n':
            out.append(c)
            line += 1
            i += 1
            bol = True
            continue
        if bol and c == '#':  # a preprocessor line with its continuations
            while i < n and text[i] != '\n':
                if text[i] == '\\' and i + 1 < n and text[i + 1] == '\n':
                    out.append(' \n')
                    line += 1
                    i += 2
                    continue
                out.append(' ')
                i += 1
            continue
        if not c.isspace():
            bol = False
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            note(text[i:j])
            out.append(' ' * (j - i))
            i = j
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            for ch in text[i:j]:
                if ch == '\n':
                    out.append('\n')
                    line += 1
                else:
                    note(ch)
                    out.append(' ')
            i = j
            continue
        m = re.match(r'R"([^(\s]*)\(', text[i:i + 20]) if c == 'R' and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')) else None
        if m:
            end = ')' + m.group(1) + '"'
            j = text.find(end, i + m.end())
            j = n if j < 0 else j + len(end)
            for ch in text[i:j]:
                out.append('\n' if ch == '\n' else ' ')
                line += ch == '\n'
            out[len(out) - (j - i)] = '"'
            out[-1] = '"'
            i = j
            continue
        if c in '"\'':
            if c == "'" and i > 0 and (text[i - 1].isalnum()) and text[i - 1] not in 'uUL8':  # a digit separator 1'000
                out.append(' ')
                i += 1
                continue
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            j = min(j, n - 1)
            out.append(c + ' ' * (j - i - 1) + c)
            i = j + 1
            continue
        out.append(c)
        i += 1
    return ''.join(out), comments


class Source:
    """one file: blanked code, comments, the top-level bodies with their namespace and the namespace-scope text"""

    def __init__(self, path, rel):
        self.path, self.rel = path, rel
        with open(path, encoding='utf-8', errors='replace') as fh:
            self.text = fh.read()
        self.code, self.comments = strip(self.text)
        self.line_at = self._line_index()
        self.bodies = []  # (start, end, namespace, head): `head` is the text before the body's `{`
        self.ns_spans = []  # (start, end, namespace): namespace-scope text outside every body
        self._scan()

    def _line_index(self):
        starts = [0]
        for k, ch in enumerate(self.code):
            if ch == '\n':
                starts.append(k + 1)
        import bisect

        def at(pos):
            return bisect.bisect_right(starts, pos)
        return at

    def comment(self, line):
        return self.comments.get(line, '')

    def _scan(self):
        code = self.code
        stack = []  # ('ns', name) or ('body', start)
        stmt = 0  # start of the current namespace-scope statement
        seg = 0
        for k, ch in enumerate(code):
            if ch == '{':
                head = code[stmt:k]
                in_body = any(t[0] == 'body' for t in stack)
                if not in_body:
                    m = re.search(r'\bnamespace\s*([\w:]*)\s*$', head)
                    if m or re.search(r'\bextern\s*"[^"]*"\s*$', head):
                        self.ns_spans.append((seg, k, self.ns(stack)))
                        stack.append(('ns', m.group(1) if m else ''))
                        stmt = seg = k + 1
                        continue
                    self.ns_spans.append((seg, k, self.ns(stack)))
                    stack.append(('body', k, head))
                    continue
                stack.append(('inner', k))
            elif ch == '}':
                if not stack:
                    continue
                top = stack.pop()
                if top[0] == 'body':
                    self.bodies.append((top[1], k, self.ns(stack), top[2]))
                    stmt = seg = k + 1
                elif top[0] == 'ns':
                    self.ns_spans.append((seg, k, self.ns(stack + [top])))
                    stmt = seg = k + 1
            elif ch == ';' and not any(t[0] in ('body', 'inner') for t in stack):
                stmt = k + 1
        self.ns_spans.append((seg, len(code), self.ns(stack)))

    @staticmethod
    def ns(stack):
        return '::'.join(t[1] for t in stack if t[0] == 'ns' and t[1])


def sources(root):
    files = sorted(glob.glob(os.path.join(root, 'mods', 'coop', '*.cpp')) + glob.glob(os.path.join(root, 'mods', 'coop', '*.h')))
    return [Source(f, os.path.relpath(f, root)) for f in files]


def key(ns, src, name):
    return f'{ns}::{name}' if ns else f'{os.path.splitext(os.path.basename(src.rel))[0]}:{name}'


# ---------------- alive ----------------

def check_alive(srcs):
    per = {}
    where = []
    for s in srcs:
        for m in re.finditer(r'\bobject_alive\s*\(', s.code):
            before = s.code[max(0, m.start() - 40):m.start()]
            if re.search(r'\bbool\s+(game::)?$', before):  # the declaration / definition
                continue
            per[s.rel] = per.get(s.rel, 0) + 1
            where.append(f'{s.rel}:{s.line_at(m.start())}')
    return per, where


# ---------------- guard ----------------

def guard_defs(srcs):
    """namespace-scope variables named like guards: name -> [(src, ns, line)] (thread_local and extern left out)"""
    defs = {}
    for s in srcs:
        for a, b, ns in s.ns_spans:
            text = s.code[a:b]
            for stm in re.finditer(r'[^;]*;', text):
                st = stm.group(0)
                if re.search(r'\b(thread_local|extern|typedef|using|return)\b', st) or '(' in st.split('=')[0]:
                    continue
                for m in re.finditer(r'\b(?:bool|int|unsigned|long|short|u?int\d+_t|size_t|std::atomic<[^>]+>)\s+(\w+)\s*(?=[;=,\[{])', st):
                    name = m.group(1)
                    if GUARD_NAME.match(name):
                        pos = a + stm.start() + m.start(1)
                        defs.setdefault(name, []).append((s, ns, s.line_at(pos)))
    return defs


def registered_guards(srcs):
    """(file, name) of every CALL_GUARD / CALL_GUARD_DEPTH: a guard is registered in the file that defines it"""
    reg = set()
    for s in srcs:
        for m in re.finditer(r'\bCALL_GUARD(?:_DEPTH)?\s*\(\s*"\s*"\s*,\s*&?\s*\(?\s*([\w:]+)', s.code):
            reg.add((s.rel, m.group(1).split('::')[-1]))
    return reg


def held_until(body, m, name):
    """where the guard set by match `m` is released: a Hold / Depth at the end of its block, a bare set at the next
    assignment, `--` or restore of the same name (the end of the body when there is none)"""
    if m.group(3):
        depth = 0
        for k in range(m.end(), len(body)):
            if body[k] == '{':
                depth += 1
            elif body[k] == '}':
                if depth == 0:
                    return k
                depth -= 1
        return len(body)
    clear = re.compile(r'(?<![\w.>])(?:\w+::)*' + re.escape(name) + r'\s*(?:=(?!=)|--)|--\s*(?:\w+::)*' + re.escape(name) + r'\b')
    c = clear.search(body, m.end())
    return c.start() if c else len(body)


def check_guard(srcs):
    defs = guard_defs(srcs)
    reg = registered_guards(srcs)
    names = sorted(defs)
    if not names:
        return {}, []
    set_re = re.compile(r'(?<![\w.>])(?:[\w]+::)*(' + '|'.join(map(re.escape, names)) + r')\s*(?:=\s*(?:true|1)\b|\+\+|\+=)'
                        r'|\+\+\s*(?:[\w]+::)*(' + '|'.join(map(re.escape, names)) + r')\b'
                        r'|callguard::(?:Hold|Depth)\s+\w+\s*[({]\s*(?:[\w]+::)*(' + '|'.join(map(re.escape, names)) + r')\b')
    found = {}
    detail = []
    for s in srcs:
        for a, b, ns, _ in s.bodies:
            body = s.code[a:b]
            calls = [m.start() for m in O_CALL.finditer(body)]
            if not calls:
                continue
            seen = set()
            for m in set_re.finditer(body):
                name = m.group(1) or m.group(2) or m.group(3)
                if name in seen:
                    continue
                end = held_until(body, m, name)
                after = [c for c in calls if m.start() < c < end]
                if not after:
                    continue
                line = s.line_at(a + m.start())
                mine = [d for d in defs[name] if d[0] is s] or defs[name]
                dsrc, dns, dline = mine[0]
                if (dsrc.rel, name) in reg:
                    continue
                if GUARD_MARK in s.comment(line) or GUARD_MARK in dsrc.comment(dline) or GUARD_MARK in dsrc.comment(dline - 1):
                    continue
                seen.add(name)
                k = key(dns, dsrc, name)
                found[k] = found.get(k, 0) + 1
                cline = s.line_at(a + after[0])
                detail.append((k, f'{s.rel}:{line}: {name} is set before the trampoline call at line {cline} but is no '
                                  f'CALL_GUARD (defined {dsrc.rel}:{dline})'))
    return found, detail


# ---------------- lifecycle ----------------

def check_lifecycle(srcs):
    found = {}
    detail = []
    for s in srcs:
        regs = []
        for m in LIFE_MACROS.finditer(s.code):
            depth, k = 0, m.end() - 1
            while k < len(s.code):
                if s.code[k] == '(':
                    depth += 1
                elif s.code[k] == ')':
                    depth -= 1
                    if not depth:
                        break
                k += 1
            regs.append(s.code[m.end():k])
        for a, b, ns, head in s.bodies:
            m = re.search(r'\b((?:\w+::)*)(on_\w+)\s*\([^()]*\)\s*(?:const\s*)?(?:noexcept\s*)?$', head)
            if not m or not LIFE_NAME.match(m.group(2)):
                continue
            if re.search(r'[=,(\[]\s*\[', head):  # a lambda, not a definition
                continue
            name = m.group(2)
            fns = '::'.join(x for x in (ns, m.group(1).rstrip(':')) if x)
            line = s.line_at(a - len(head) + m.start(2))
            if LIFE_MARK in s.comment(line) or LIFE_MARK in s.comment(line - 1):
                continue
            if any(re.search(r'\b' + name + r'\b', r) for r in regs):
                continue
            k = key(fns, s, name)
            found[k] = found.get(k, 0) + 1
            detail.append((k, f'{s.rel}:{line}: {fns + "::" if fns else ""}{name} is not registered with the lifecycle '
                              f'registry (lifecycle.h)'))
    return found, detail


# ---------------- compare ----------------

def compare_map(title, now, base, detail, lower_hint):
    bad = []
    lower = []
    for k in sorted(now):
        b = base.get(k, 0)
        if now[k] > b:
            for dk, text in detail:
                if dk == k:
                    bad.append(text)
            if not any(dk == k for dk, _ in detail):
                bad.append(f'{k}: {now[k]} (baseline {b})')
    for k in sorted(base):
        if now.get(k, 0) < base[k]:
            lower.append(f'{k} {base[k]} -> {now.get(k, 0)}')
    for line in bad:
        print(f'ratchet_lint {title}: FAIL {line}')
    if lower:
        print(f'ratchet_lint {title}: below the baseline ({", ".join(lower)}): lower it with {lower_hint}')
    return bad, lower


def run(root, baseline_path, lower=False, verbose=False):
    srcs = sources(root)
    bpath = os.path.join(root, baseline_path)
    try:
        with open(bpath) as fh:
            base = json.load(fh)
    except FileNotFoundError:
        base = {}
    hint = '`python3 tools/ratchet_lint.py --lower`'
    fails = 0

    per, where = check_alive(srcs)
    total = sum(per.values())
    btotal = base.get('object_alive', {}).get('total', 0)
    if total > btotal:
        fails += 1
        print(f'ratchet_lint alive: FAIL {total} object_alive( calls, the baseline is {btotal}: a new call on a kept pointer '
              f'reads freed memory; use game::live_ref / game::alive (LiveRef, game.h) or game::listed')
        old = base.get('object_alive', {}).get('per_file', {})
        for f in sorted(per):
            if per[f] > old.get(f, 0):
                print(f'  {f}: {per[f]} (was {old.get(f, 0)})')
    elif total < btotal:
        print(f'ratchet_lint alive: {total} object_alive( calls, below the baseline {btotal}: lower it with {hint}')
    if verbose:
        for w in where:
            print('  ' + w)

    guards, gdetail = check_guard(srcs)
    gbad, glow = compare_map('guard', guards, base.get('call_guard', {}), gdetail, hint)
    fails += len(gbad)

    life, ldetail = check_lifecycle(srcs)
    lbad, llow = compare_map('lifecycle', life, base.get('lifecycle', {}), ldetail, hint)
    fails += len(lbad)

    print(f'ratchet_lint: object_alive {total} (baseline {btotal}), unregistered guards {sum(guards.values())} '
          f'(baseline {sum(base.get("call_guard", {}).values())}), unregistered lifecycle functions {sum(life.values())} '
          f'(baseline {sum(base.get("lifecycle", {}).values())}): {fails} problem(s)')
    if lower:
        if fails:
            print('ratchet_lint --lower: refused, something is above its baseline')
            return 1
        new = {
            'object_alive': {'total': total, 'per_file': dict(sorted(per.items()))},
            'call_guard': dict(sorted(guards.items())),
            'lifecycle': dict(sorted(life.items())),
        }
        with open(bpath, 'w') as fh:
            json.dump(new, fh, indent=1, sort_keys=False)
            fh.write('\n')
        print(f'ratchet_lint --lower: {baseline_path} written')
    return 1 if fails else 0


# ---------------- self-test ----------------

CLEAN = {
    'mods/coop/game.h': 'namespace game {\nbool object_alive(void* p);\n}\n',
    'mods/coop/a.cpp': '''#include "call_guard.h"
namespace a {
static bool g_in_take;
CALL_GUARD("a.take", g_in_take);
static bool g_inside_x; // not a call guard: stays set across frames on purpose
static thread_local bool t_in_y;
static int g_depth;
CALL_GUARD_DEPTH("a.depth", g_depth);
static void (*o_Take)(int);
static void h_Take(int v)
{
    g_in_take = true;
    o_Take(v);
    g_in_take = false;
    g_inside_x = true;
    o_Take(v);
    callguard::Depth d(g_depth);
    o_Take(v);
    t_in_y = true;
    o_Take(v);
    const char* s = "g_in_bad = true; o_Take(1); object_alive(p)";
    (void)s;
}
// object_alive(p) in a comment does not count
bool kept(void* p) { return game::object_alive(p); }
void on_session_end() { }
SESSION_STATE("a.x", nullptr, [] { on_session_end(); }, []() -> size_t { return 0; });
// lifecycle-exempt: calls the game (despawns), cannot be a reset
void on_world_change() { }
void on_message(int) { }
}
''',
}
BASE = {'object_alive': {'total': 1, 'per_file': {'mods/coop/a.cpp': 1}}, 'call_guard': {}, 'lifecycle': {}}

PLANTS = {
    'alive': ('mods/coop/b.cpp', 'namespace b {\nbool again(void* p) { return game::object_alive(p); }\n}\n',
              'ratchet_lint alive: FAIL 2 object_alive'),
    'guard': ('mods/coop/c.cpp', 'namespace c {\nstatic bool g_applying;\nstatic void (*o_Apply)();\n'
                                 'void apply()\n{\n    g_applying = true;\n    o_Apply();\n    g_applying = false;\n}\n}\n',
              'ratchet_lint guard: FAIL mods/coop/c.cpp:6: g_applying'),
    'guard_depth': ('mods/coop/c2.cpp', 'namespace c2 {\nint g_hook_depth = 0;\nstatic int (*o_Ev)(int);\n'
                                        'int h_Ev(int x)\n{\n    ++g_hook_depth;\n    int r = o_Ev(x);\n    g_hook_depth--;\n'
                                        '    return r;\n}\n}\n',
                    'ratchet_lint guard: FAIL mods/coop/c2.cpp:6: g_hook_depth'),
    'lifecycle': ('mods/coop/d.cpp', 'namespace d {\nstatic int g_n;\nvoid on_player_leave(int id)\n{\n    g_n = id;\n}\n}\n',
                  'ratchet_lint lifecycle: FAIL mods/coop/d.cpp:3: d::on_player_leave'),
}


def self_test():
    me = os.path.abspath(__file__)
    bad = 0

    def tree(extra=None, base=BASE):
        d = tempfile.mkdtemp(prefix='ratchet_lint_')
        for rel, text in list(CLEAN.items()) + ([extra[:2]] if extra else []):
            p = os.path.join(d, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'w') as fh:
                fh.write(text)
        os.makedirs(os.path.join(d, 'tools'), exist_ok=True)
        with open(os.path.join(d, BASELINE), 'w') as fh:
            json.dump(base, fh)
        return d

    def lint(d, *args):
        r = subprocess.run([sys.executable, me, '--root', d] + list(args), capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    d = tree()
    rc, out = lint(d)
    if rc != 0:
        print(f'ratchet_lint self-test: FAIL the clean tree fails:\n{out}')
        bad += 1
    shutil.rmtree(d)
    for name, plant in PLANTS.items():
        d = tree(plant)
        rc, out = lint(d)
        if rc == 0 or plant[2] not in out:
            print(f'ratchet_lint self-test: FAIL the planted {name} violation is not caught (rc {rc}, want "{plant[2]}"):\n{out}')
            bad += 1
        rc2, out2 = lint(d, '--lower')
        if rc2 == 0:
            print(f'ratchet_lint self-test: FAIL --lower accepted the planted {name} violation')
            bad += 1
        shutil.rmtree(d)
    # below the baseline: passes, says how to lower, --lower writes the lower number
    d = tree(base={'object_alive': {'total': 3}, 'call_guard': {'x::g_in_gone': 1}, 'lifecycle': {}})  # re_lint: not-a-sym
    rc, out = lint(d)
    if rc != 0 or 'lower it with' not in out:
        print(f'ratchet_lint self-test: FAIL a count below the baseline (rc {rc}):\n{out}')
        bad += 1
    rc, out = lint(d, '--lower')
    with open(os.path.join(d, BASELINE)) as fh:
        nb = json.load(fh)
    if rc != 0 or nb['object_alive']['total'] != 1 or nb['call_guard']:
        print(f'ratchet_lint self-test: FAIL --lower did not write the lower baseline: {nb}\n{out}')
        bad += 1
    shutil.rmtree(d)
    print(f'ratchet_lint self-test: {len(PLANTS)} planted violations, {bad} problem(s)')
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default=ROOT)
    ap.add_argument('--lower', action='store_true', help='write the current (lower) counts into the baseline')
    ap.add_argument('--self-test', action='store_true')
    ap.add_argument('-v', '--verbose', action='store_true', help='list every object_alive( call')
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    return run(a.root, BASELINE, a.lower, a.verbose)


if __name__ == '__main__':
    sys.exit(main())
