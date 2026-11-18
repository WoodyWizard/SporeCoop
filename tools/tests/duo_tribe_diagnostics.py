"""Verify full population reports on connected disposable Tribal games (BUG-9061).

Requires a late campaign whose guest report exceeds the old 8191-byte limit.
The header counts and member lines come from the same native frame.
"""
import argparse
import re
from duo_villages import Console, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    a = p.parse_args()
    longest = 0
    for instance in (a.host, a.guest):
        c = Console(instance)
        assert 'Tribe' in command(c, 'game.mode')
        report = command(c, 'coop.tribe')
        longest = max(longest, len(report.encode('utf-8')))
        tribes = re.split(r'(?=^tribe \d+)', report, flags=re.M)[1:]
        assert tribes and report.endswith('\n'), 'Missing or truncated native report'
        total = 0
        for tribe in tribes:
            expected = int(re.search(r': (\d+) members', tribe)[1])
            actual = len(re.findall(r'^  #\d+ .* revive=[\d.]+$', tribe, re.M))
            assert actual == expected, f'Truncated tribe: expected {expected}, got {actual}: {tribe[:100]}'
            total += actual
        assert 'total faults: 0' in command(c, 'exc')
        print(f'PASS instance={instance} bytes={len(report)} tribes={len(tribes)} members={total}', flush=True)
    assert longest > 8191, 'Fixture too small to exercise the original truncation'


if __name__ == '__main__':
    main()
