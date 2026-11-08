#!/usr/bin/env python3
"""Lint of the game tests (make test, BUG-0430: tests and harness that hide failures).

  suite     every game test (tools/tests/coop_*.py, kit_*.py) runs in a lab suite (tools/tests/suite.py), is
            imported by one that does (a library of other tests), or says why not near its top:
            `# suite: none (<reason>)` (a measurement tool, a probe run by hand)
  counter   a check (`ck(...)`) whose condition rests on a counter of the mod (a `key=` of a mod status line
            printed from a g_ variable: `acts_sent=`, `cmds_done=`, ...) instead of the game's own state on the
            other machine (CLAUDE.md 3: "check what the other player sees, not our bookkeeping"). Such a check
            needs a reason: `# counter: <reason>` on the check's line, or an entry of COUNTER_ALLOW below (the
            checks that were there when the lint came, each with why it may stay or what should replace it)

  test_lint.py [--root <checkout>]    exit 1 on a problem
"""
import argparse
import ast
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# (test file, counter key): why the check may assert the mod's counter. "pending:" = it should assert the game's
# state instead (BUG-0430), the replacement named; the others assert the mod's own mechanism on purpose, next to a
# check of the game's state in the same test.
COUNTER_ALLOW = {
    ('coop_creature_e84.py', 'strays_removed'):
        'the same check also asserts the game objects (no untuned creature left); the counter says the removal ran',
    ('coop_creature_social.py', 'hidden_frames'):
        'the name tag is the mod\'s own overlay (no game state): its draw counter is what the player sees',
    ('coop_creature_social.py', 'acts_sent'):
        'the message to the host; the result is checked on the game state next (species impressed, DNA on both)',
    ('coop_creature_social.py', 'cards_refreshed'):
        'pending: the goal card text of the guest\'s game (coop.uistate) instead; DNA on both games checked before',
    ('coop_creature_social.py', 'goal_dna_paid'):
        'paid once: the DNA on both games is checked next (the game\'s value); the counter tells "once" apart',
    ('coop_creature_social_meter.py', 'visible_at_win'):
        'read from the game\'s meter window at the win message (the mod samples the game\'s flag)',
    ('coop_creature_social_meter.py', 'stage_win_msgs'):
        'the game\'s own message reached the game\'s handler (counted in the hook); the meter state is checked next',
    ('coop_creature_social_meter.py', 'open_for'):
        'with the game\'s own `visible=` flag of the meter window in the same check',
    ('coop_creature_social_prompts.py', 'prompt_fails'):
        'pending: the game\'s record of the Kook (not impressed, coop.social species) is checked on the line before',
    ('coop_exit_menu.py', 'solo_dna'):
        'pending: the guest\'s game DNA (coop.dna) after the host comes back instead of the mod\'s kept value',
    ('coop_join_other_world.py', 'local_removed'):
        'pending: the guest\'s own NPCs in its game (coop.npc creatures) instead of the removal counter',
    ('coop_migration.py', 'deferred'):
        'the order of the removal (after the cinematic) is the fix; the game\'s herd of the avatar is checked next',
    ('coop_migration.py', 'done_after'):
        'as deferred',
    ('coop_notify.py', 'shown'):
        'the panels\' screen rectangles (the game\'s UI windows) are checked on the next line',
    ('coop_select.py', 'guards'):
        'a crash guard: that the guarded path ran is the point (the crash itself is the lab\'s invariant)',
    ('coop_space_stale.py', 'down'):
        'not a counter: `coop.space hp` prints the ship\'s disabled state read from the game (spacecoop.cpp hp line)',
    ('coop_space_stale.py', 'repair'):
        'as down (the game\'s repair timer of the ship)',
    ('coop_tribe.py', 'cmds_done'):
        'pending: the host\'s member doing the order (coop.tribe: the member\'s action / target) instead',
    ('coop_tribe.py', 'rejected'):
        'pending: the host\'s member gathering at the tree (coop.tribe) instead of no rejection',
}


def mod_counter_keys(root):
    """keys printed as `key=%u` from a g_ variable (or its size) of the mod and never from anything else"""
    counters, other = set(), set()
    spec = re.compile(r'%[-+ #0-9.]*(?:ll|l|z|h)?[udixXfgcsp]')
    for f in glob.glob(os.path.join(root, 'mods', 'coop', '**', '*.cpp'), recursive=True):
        src = open(f, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'((?:"(?:[^"\\]|\\.)*"\s*)+),([^;]*?)\);', src, re.S):
            fmt = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
            specs = list(spec.finditer(fmt))
            if not specs or '=' not in fmt:
                continue
            args, depth, cur = [], 0, ''
            for ch in m.group(2):
                depth += ch in '([{'
                depth -= ch in ')]}'
                if ch == ',' and depth == 0:
                    args.append(cur.strip())
                    cur = ''
                else:
                    cur += ch
            args = [a for a in args + [cur.strip()] if a]
            if len(args) < len(specs):
                continue
            args = args[-len(specs):]
            for i, sm in enumerate(specs):
                km = re.search(r'(\w+)=$', fmt[:sm.start()])
                if km:
                    a = re.sub(r'^\((?:unsigned|int|double|float|long|uint32_t|size_t)\)\s*', '', args[i])
                    (counters if re.fullmatch(r'\(?g_\w+(?:\.size\(\))?\)?', a) else other).add(km.group(1))
    return counters - other


def counter_problems(root, keys):
    out, used = [], set()
    key_re = re.compile(r'(\w+)=\(')
    for f in sorted(glob.glob(os.path.join(root, 'tools', 'tests', 'coop_*.py'))):
        src = open(f, encoding='utf-8').read()
        try:
            tree = ast.parse(src)
        except SyntaxError as e:
            out.append(f'{os.path.relpath(f, root)}: does not parse ({e})')
            continue
        lines = src.split('\n')
        base = os.path.basename(f)

        def direct(node):
            return {k for k in key_re.findall(ast.get_source_segment(src, node) or '') if k in keys}
        for fn in ast.walk(tree):
            if not isinstance(fn, (ast.FunctionDef, ast.AsyncFunctionDef)):
                continue
            assigns = []  # (line, name, keys)
            for n in ast.walk(fn):
                if isinstance(n, ast.Assign):
                    ks = direct(n.value)
                    for t in n.targets:
                        for nm in ast.walk(t):
                            if isinstance(nm, ast.Name):
                                assigns.append((n.lineno, nm.id, ks))
            for n in ast.walk(fn):
                if not (isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == 'ck' and n.args):
                    continue
                ks = direct(n.args[0])
                for nm in ast.walk(n.args[0]):
                    if isinstance(nm, ast.Name):  # the nearest assignment above the check
                        last = [a for a in assigns if a[1] == nm.id and a[0] <= n.lineno]
                        if last:
                            ks |= max(last, key=lambda a: a[0])[2]
                if not ks:
                    continue
                text = '\n'.join(lines[n.lineno - 1:(n.end_lineno or n.lineno)])
                if re.search(r'#\s*counter:\s*\S', text):
                    continue
                left = [k for k in sorted(ks) if (base, k) not in COUNTER_ALLOW]
                used |= {(base, k) for k in ks}
                if left:
                    out.append(f'{os.path.relpath(f, root)}:{n.lineno}: the check rests on the mod\'s counter(s) '
                               f'{", ".join(left)}: assert the game\'s own state on the other machine, or say why '
                               f'(`# counter: <reason>` on the line, COUNTER_ALLOW in tools/tests/test_lint.py)')
    for k in sorted(k for k in set(COUNTER_ALLOW) - used if os.path.exists(os.path.join(root, 'tools', 'tests', k[0]))):
        out.append(f'tools/tests/test_lint.py: COUNTER_ALLOW {k} matches no check any more: remove it')
    return out


def suite_problems(root):
    sys.path.insert(0, os.path.join(root, 'tools', 'tests'))
    import suite  # noqa: E402
    cmds = ' '.join(t['cmd'] for t in suite.T)
    in_suite = {os.path.basename(f) for f in glob.glob(os.path.join(root, 'tools', 'tests', '*.py'))
                if re.search(r'(?:^|[\s/])' + re.escape(os.path.basename(f)) + r'\b', cmds)}
    imported = set()
    for name in in_suite:
        src = open(os.path.join(root, 'tools', 'tests', name), encoding='utf-8').read()
        imported |= {m + '.py' for m in re.findall(r'^\s*(?:from|import)\s+(\w+)', src, re.M)}
    out = []
    for f in sorted(glob.glob(os.path.join(root, 'tools', 'tests', 'coop_*.py')) +
                    glob.glob(os.path.join(root, 'tools', 'tests', 'kit_*.py'))):
        name = os.path.basename(f)
        if name in in_suite or name in imported:
            continue
        head = open(f, encoding='utf-8').read().split('\n')[:40]
        if any(re.search(r'#\s*suite:\s*none\s*\(\s*\S', ln) for ln in head):
            continue
        out.append(f'tools/tests/{name}: in no lab suite (tools/tests/suite.py) and imported by no suite test: add it '
                   f'to a suite, or say why not near its top: `# suite: none (<reason>)`')
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.dirname(os.path.dirname(HERE)))
    a = ap.parse_args()
    keys = mod_counter_keys(a.root)
    problems = suite_problems(a.root) + counter_problems(a.root, keys)
    for p in problems:
        print(p)
    print(f'test_lint: {len(keys)} mod counter keys, {len(problems)} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
