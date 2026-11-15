#!/usr/bin/env python3
"""Run a unit test binary of `make test` (net_tests, net_tests_asan) as N shards at once (feat/lab-speed,
docs/06-testing.md#testing-lab-speed).

  shards.py N BINARY [ARGS...]                every shard (`--shard K/N`) must end with "ALL OK" and exit 0
  shards.py N BINARY --nofix --expect-fail     the fixes-off run: every shard must run to its end (last line
                                               "ALL OK" or "FAILED (n)", no abort), at least one FAILED in all
  --out FILE                                   the shards' output also into FILE (in shard order)

Most tests of net_tests wait in real time (sessions over localhost UDP), so the shards' time is the longest shard's,
not the sum. The output is printed shard by shard in order, a FAIL line as it was. The environment of this process
(ASAN_OPTIONS, NET_TESTS_TIMES) goes to every shard.
"""
import os
import re
import subprocess
import sys


def main(argv):
    out_file, expect_fail = None, False
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == '--out':
            out_file = argv[i + 1]
            i += 2
            continue
        if argv[i] == '--expect-fail':
            expect_fail = True
        else:
            args.append(argv[i])
        i += 1
    n, binary, rest = int(args[0]), args[1], args[2:]
    procs = [subprocess.Popen([binary, *rest, '--shard', f'{k}/{n}'], stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, errors='replace') for k in range(1, n + 1)]
    texts, bad, fails = [], [], 0
    for k, p in enumerate(procs, 1):
        text = p.communicate()[0]
        texts.append(f'--- shard {k}/{n}\n{text}')
        last = text.rstrip().splitlines()[-1] if text.strip() else ''
        m = re.match(r'FAILED \((\d+)\)$', last)
        if expect_fail:
            if m and p.returncode == 1:
                fails += int(m.group(1))
            elif not (last == 'ALL OK' and p.returncode == 0):
                bad.append(f'shard {k}/{n}: did not run to its end (exit {p.returncode}, last line {last!r})')
        elif not (last == 'ALL OK' and p.returncode == 0):
            bad.append(f'shard {k}/{n}: exit {p.returncode}, last line {last!r}')
    whole = ''.join(texts)
    if out_file:
        with open(out_file, 'w') as f:
            f.write(whole)
    if not expect_fail:
        sys.stdout.write(whole)
    if expect_fail and not fails and not bad:
        bad.append('no shard failed: with the fixes off the tests must FAIL')
    name = os.path.basename(binary) + (' ' + ' '.join(rest) if rest else '')
    if bad:
        if expect_fail:
            sys.stdout.write(whole[-4000:])
        print('\n'.join(f'{name}: {b}' for b in bad))
        return 1
    print(f'{name}: {n} shards, ' + (f'{fails} expected FAILs, every shard ran to its end' if expect_fail else 'ALL OK'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
