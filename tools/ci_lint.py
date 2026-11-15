#!/usr/bin/env python3
"""CI supply-chain lint (A3, BUG-1121; in `make test`):

  python3 tools/ci_lint.py [workflow.yml ...]     (default: .github/workflows/*.yml)

* every `uses:` names its action by a full commit SHA with the tag in a comment
  (`actions/checkout@<40 hex> # v4.4.0`): a moved or hijacked tag cannot change what CI runs;
* tools/toolchain.env pins the llvm-mingw version and its tarball's SHA-256, the CI Python and Pillow;
* the workflow checks the tarball with `sha256sum -c` against LLVM_MINGW_SHA256, installs Pillow
  as `pillow==${PILLOW_VERSION}` and does not pin a second toolchain version of its own.
Exit 1 with one line per problem."""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
USES = re.compile(r'^\s*(?:-\s+)?uses:\s*(\S+)(.*)$')
PINNED = re.compile(r'^[\w.-]+/[\w./-]+@[0-9a-f]{40}$')
TAG_COMMENT = re.compile(r'^\s+#\s+v\d+(?:\.\d+)*\s*$')
ENV_KEYS = {
    'LLVM_MINGW_VERSION': r'\d{8}',
    'LLVM_MINGW_SHA256': r'[0-9a-f]{64}',
    'CI_PYTHON': r'3\.\d+',
    'PILLOW_VERSION': r'\d+\.\d+\.\d+',
}


def read_env(path):
    env, bad = {}, []
    with open(path, encoding='utf-8') as f:
        for n, line in enumerate(f, 1):
            line = line.rstrip('\n')
            if not line.strip() or line.startswith('#'):
                continue
            m = re.match(r'^([A-Z_][A-Z0-9_]*)=(\S*)$', line)
            if not m:
                bad.append(f'{os.path.relpath(path, ROOT)}:{n}: not a KEY=value line (no quotes, no spaces): {line!r}')
                continue
            env[m.group(1)] = m.group(2)
    return env, bad


def lint_workflow(path):
    out = []
    rel = os.path.relpath(path, ROOT)
    with open(path, encoding='utf-8') as f:
        text = f.read()
    for n, line in enumerate(text.splitlines(), 1):
        m = USES.match(line)
        if not m:
            continue
        ref, rest = m.group(1), m.group(2)
        if ref.startswith('./'):
            continue  # a local action is part of the checkout
        if not PINNED.match(ref):
            out.append(f'{rel}:{n}: {ref}: pin the action by its commit SHA '
                       f'(gh api repos/<owner>/<action>/commits/<tag> --jq .sha)')
        elif not TAG_COMMENT.match(rest):
            out.append(f'{rel}:{n}: {ref}: the tag the SHA was resolved from goes into a comment (# vX.Y.Z)')
    if 'llvm-mingw' in text:
        if not re.search(r'sha256sum -c', text) or '${LLVM_MINGW_SHA256}' not in text:
            out.append(f'{rel}: the llvm-mingw tarball is not checked with sha256sum -c against ${{LLVM_MINGW_SHA256}}')
        if re.search(r'^\s*LLVM_MINGW_VERSION:', text, re.M):
            out.append(f'{rel}: LLVM_MINGW_VERSION is set in the workflow; it lives in tools/toolchain.env only')
        if 'tools/toolchain.env' not in text:
            out.append(f'{rel}: the workflow does not read tools/toolchain.env')
    for m in re.finditer(r'pip install[^\n]*', text):
        if 'pillow' in m.group(0).lower() and 'pillow==${PILLOW_VERSION}' not in m.group(0):
            out.append(f'{rel}: Pillow is installed unpinned ({m.group(0).strip()}); use "pillow==${{PILLOW_VERSION}}"')
    return out


def main(argv):
    flows = argv or sorted(glob.glob(os.path.join(ROOT, '.github', 'workflows', '*.yml')))
    env_path = os.environ.get('CI_LINT_ENV', os.path.join(ROOT, 'tools', 'toolchain.env'))
    problems = []
    env, bad = read_env(env_path)
    problems += bad
    for k, rx in ENV_KEYS.items():
        if not re.fullmatch(rx, env.get(k, '')):
            problems.append(f'{os.path.relpath(env_path, ROOT)}: {k} missing or malformed (want {rx}): {env.get(k)!r}')
    for p in flows:
        problems += lint_workflow(p)
    for p in problems:
        print('ci_lint: ' + p)
    print(f'ci_lint: {len(flows)} workflow(s), {len(problems)} problem(s)')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
