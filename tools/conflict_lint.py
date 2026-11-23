#!/usr/bin/env python3
"""Fails when a tracked file still holds git conflict markers (a merge committed half resolved).

Checks every tracked text file for lines that start with `<<<<<<< `, `>>>>>>> ` or are exactly `=======`
next to one of those. Run by `make test` (unit-lint)."""
import subprocess
import sys

files = subprocess.run(['git', 'ls-files', '-z'], capture_output=True, check=True).stdout.split(b'\0')
bad = []
for f in files:
    if not f:
        continue
    try:
        data = open(f, 'rb').read()
    except OSError:
        continue
    if b'\0' in data[:8192]:
        continue
    for n, line in enumerate(data.split(b'\n'), 1):
        if line.startswith(b'<<<<<<< ') or line.startswith(b'>>>>>>> '):
            bad.append(f'{f.decode(errors="replace")}:{n}: {line[:60].decode(errors="replace")}')
if bad:
    print('conflict_lint: conflict markers in tracked files:')
    print('\n'.join(bad[:50]))
    sys.exit(1)
print(f'conflict_lint: {len(files) - 1} files, no conflict markers')
