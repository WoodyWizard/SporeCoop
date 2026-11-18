"""Targeted library test on two already-running disposable LineagesCache profiles.

Uses native Sporepedia captions and ResourceManager provenance, not only mod
counters. Does not load/replay a campaign. --verify-only checks a cold restart.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sporectl import Console


def command(console, text):
    result = console.call(text)
    if not result.get('ok'):
        raise AssertionError(f'{text}: {result}')
    return result['out']


def rows(console):
    value = command(console, 'coop.library list')
    assert 'failed=0' in value and 'export_failed=0' in value and 'error=' not in value, value
    return {m[0]: {'key': m[1], 'kind': m[2], 'details': m[3]} for m in
            re.findall(r'^([a-f0-9]{64}) ([A-F0-9/]+) (local|received) (.*)$', value, re.M)}


def wait_until(predicate, seconds=45):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if predicate():
            return
        time.sleep(.3)
    raise AssertionError('Library did not converge within the targeted timeout')


def browser(console, name):
    # GalaxyGameEntry exists during the entrance animation, before Share takes
    # clicks. Retry opening until the real native sidebar is present.
    for attempt in range(10):
        command(console, 'click 56 201')
        command(console, 'wait.frames 20 3000')
        if console.call('coop.ui find "Lineages Cache"').get('ok'):
            break
    else:
        raise AssertionError('Native Lineages Cache category did not appear')
    command(console, 'click 65 553')
    command(console, 'wait.frames 20 3000')
    assert 'Lineages Cache' in command(console, 'coop.ui find "Lineages Cache"')
    assert name in command(console, f'coop.ui find "{name}"')
    command(console, 'click 787 18')
    command(console, 'wait.frames 15 3000')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, default=4)
    parser.add_argument('--guest', type=int, default=5)
    parser.add_argument('--port', type=int, default=47816)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--feed-control', action='store_true', help='Requires already imported fixture cards')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        assert 'GalaxyGameEntry' in command(c, 'game.mode')
        assert 'total faults: 0' in command(c, 'exc')
        wait_until(lambda: any(v['kind'] == 'local' for v in rows(c).values()) and
                   ' scan=0 ' in command(c, 'coop.library'))
        own = next(v for v in rows(c).values() if v['kind'] == 'local')
        source = command(c, 'coop.creations resource ' + own['key'].replace('/', ' '))
        assert 'sporekit-lineagescache' in source.lower(), 'Refusing non-cache QA profile'
    if args.feed_control:
        command(h, 'coop.fix library.feed off')
        try:
            try:
                browser(h, 'Stabbella')
            except AssertionError:
                assert h.call('coop.ui find "Lineages Cache"')['ok']
                assert not h.call('coop.ui find "Stabbella"')['ok']
                print('EXPECTED FAIL: native cache card missing with feed fix off', flush=True)
            else:
                raise AssertionError('Negative control unexpectedly passed')
        finally:
            command(h, 'coop.fix library.feed on')
            command(h, 'click 787 18')
            command(h, 'wait.frames 15 3000')
        browser(h, 'Stabbella')
        print('PASS native cache card restored with feed fix on', flush=True)
    elif args.verify_only:
        expected = json.loads((args.output / 'received.json').read_text())
        for i, c in enumerate((h, g)):
            actual = rows(c)
            for digest, entry in expected[i].items():
                assert actual.get(digest) == entry, 'Cold restart changed a cache entry'
                path = command(c, 'coop.creations resource ' + entry['key'].replace('/', ' '))
                assert 'Pollination.package' in path, path
        browser(h, 'Stabbella')
        browser(g, 'DuoHostVoyager')
        print('PASS cold restart, native resources and offline cache cards', flush=True)
    else:
        h_before, g_before = rows(h), rows(g)
        h_local = {k for k, v in h_before.items() if v['kind'] == 'local'}
        g_local = {k for k, v in g_before.items() if v['kind'] == 'local'}
        command(h, f'coop.host {args.port} CacheHost')
        command(g, f'coop.join 127.0.0.1:{args.port} CacheGuest')
        wait_until(lambda: g_local <= rows(h).keys() and h_local <= rows(g).keys())
        wait_until(lambda: 'pending=0' in command(h, 'coop.library') and 'pending=0' in command(g, 'coop.library'))
        results = [rows(h), rows(g)]
        for index, (c, expected_hashes) in enumerate(((h, g_local-h_local), (g, h_local-g_local))):
            for digest in expected_hashes:
                entry = results[index][digest]
                assert entry['kind'] == 'received'
                native = command(c, 'coop.creations resource ' + entry['key'].replace('/', ' '))
                assert 'Pollination.package' in native, native
        browser(h, 'Stabbella')
        browser(g, 'DuoHostVoyager')
        print(f'PASS native two-way import: host {len(g_local-h_local)}, guest {len(h_local-g_local)}', flush=True)
        command(g, 'coop.leave')
        command(g, f'coop.join 127.0.0.1:{args.port} CacheGuest')
        wait_until(lambda: 'peers=1' in command(g, 'coop.library'))
        command(h, 'wait.frames 100 10000')
        assert [rows(h), rows(g)] == results, 'Reconnect duplicated or replaced native entries'
        print('PASS reconnect without duplicate imports', flush=True)
        command(g, 'coop.leave')
        command(h, 'coop.leave')
        browser(h, 'Stabbella')
        browser(g, 'DuoHostVoyager')
        received = [{k: v for k, v in r.items() if v['kind'] == 'received'} for r in results]
        (args.output / 'received.json').write_text(json.dumps(received, indent=2))
        print('PASS cache remains available outside co-op', flush=True)
    for c in (h, g):
        command(c, 'wait.frames 30 3000')
        assert 'total faults: 0' in command(c, 'exc')


if __name__ == '__main__':
    main()
