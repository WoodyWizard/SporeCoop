#!/usr/bin/env python3
"""Summary of the divergence detector over lab runs (docs/sync-architecture-plan.md L0).

Tests run with SPORE_DETECTOR=<s> print, from their monitor (tools/tests/monitor.py), one
`DETECTOR-SAMPLE {...}` line per comparison of the host's world with a guest's and one `DETECTOR {...}`
line per finding (tools/tests/worlddiff.py). This script reads the test logs of the given lab run
directories (default: the runs of today) and writes, per stage, how often each finding signature was
seen, in which tests, with examples, and which ticket already covers it (KNOWN).

  tools/detector_report.py [run dir ...] [--since YYYYMMDD-HHMM] [--out out/detector/<date>.md]
"""
import argparse
import collections
import datetime
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'tests'))
import worlddiff  # noqa: E402  (noun names: older logs carry hex kinds)


def named(sig):
    p = sig.split(':')
    if len(p) == 3 and re.fullmatch(r'[0-9A-F]{8}', p[2]) and int(p[2], 16) in worlddiff.NOUNS:
        p[2] = worlddiff.NOUNS[int(p[2], 16)]
    return ':'.join(p)

# signature (or its prefix up to a kind) -> the ticket that already covers it
KNOWN = {  # <stage>:<what>:<kind> (or <stage>:*:<kind>, <*>:*:<kind>) -> the ticket that covers it
    'Creature:scale:creature': 'BUG-0461',
    '*:*:fruit': 'BUG-0472',
    'Tribe:*:game_bundle': 'BUG-0472',
    'Creature:*:game_plant': 'BUG-0473',
    'Creature:*:rock': 'BUG-0473',
    'Creature:*:interactive_ornament': 'BUG-0473 / BUG-0459',
    'Tribe:*:egg': 'BUG-0474',
    'Creature:*:egg': 'BUG-0421 class (eggs, BUG-0384)',
    'Tribe:scale:citizen': 'BUG-0475',
    'Civilization:scale:citizen': 'BUG-0475',
    'Civilization:*:citizen': 'BUG-0476',
    '*:*:default_tool_projectile': 'BUG-0477',
    '*:*:resource_projectile': 'BUG-0477',
    '*:*:cultural_projectile': 'BUG-0477',
    '*:*:default_beam_projectile': 'BUG-0477',
    '*:*:default_ao_e_area': 'BUG-0477',
    'Civilization:*:vehicle': 'BUG-0478',
    'Civilization:*:inter_city_road': 'BUG-0478',
    'Space:*:ufo': 'BUG-0479',
    'Space:*:planetary_artifact': 'BUG-0479',
    'Cell:*:cell': 'BUG-0480',
    'Creature:guest_local:creature': 'BUG-0421 class (guest-local objects)',
    '*:*:tribe': 'BUG-0483', '*:*:tribe_hut': 'BUG-0483', '*:*:tribe_food_mat': 'BUG-0483',
    'Civilization:*:tribe_tool': 'BUG-0483', 'Tribe:guest_only:tribe_tool': 'BUG-0483', 'Tribe:guest_only:citizen': 'BUG-0483',
    'Civilization:*:cultural_target': 'BUG-0484', 'Civilization:*:game_bundle': 'BUG-0484', 'Civilization:*:turret': 'BUG-0484',
    '*:scale:node': 'BUG-0485',
    'Creature:*:ufo': 'BUG-0486',
    'Creature:host_only:creature': 'BUG-0487', 'Creature:hp:creature': 'BUG-0487', 'Creature:alive:creature': 'BUG-0487',
    'Creature:host_local:creature': 'BUG-0487', 'Creature:guest_only:creature': 'BUG-0487',
    'Tribe:unmatched:tribe_tool': 'BUG-0488', 'Tribe:unmatched:citizen': 'BUG-0488', 'Tribe:*:nest': 'BUG-0488',
    'Tribe:*:interactive_ornament': 'BUG-0488',
    '*:*:gameplay_marker': 'BUG-0489',
    'Space:*:citizen': 'BUG-0490', 'Space:*:vehicle': 'BUG-0490',
}


def known(sig):
    st, what, kind = (sig.split(':') + ['', '', ''])[:3]
    for k in (sig, f'*:{what}:{kind}', f'{st}:*:{kind}', f'*:*:{kind}'):
        if k in KNOWN:
            return KNOWN[k]
    return ''


def runs_since(since):
    out = []
    for d in sorted(glob.glob(os.path.join(ROOT, 'out', 'lab', '2*'))):
        name = os.path.basename(d)
        if name >= since:
            out.append(d)
    return out


def test_logs(run):
    return [p for p in sorted(glob.glob(os.path.join(run, '*.log'))) if not p.endswith('.game.log')]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('runs', nargs='*')
    ap.add_argument('--since', default=datetime.date.today().strftime('%Y%m%d'))
    ap.add_argument('--out', default='')
    a = ap.parse_args()
    runs = a.runs or runs_since(a.since)
    samples = collections.Counter()          # stage -> comparisons made
    skipped = collections.Counter()          # reason -> comparisons skipped (not in the same place)
    seen = collections.Counter()             # sig -> comparisons it was in
    objs = collections.Counter()             # sig -> objects
    tests = collections.defaultdict(set)     # sig -> tests
    examples = collections.defaultdict(list)
    files = 0
    for run in runs:
        for p in test_logs(run):
            test = os.path.basename(p)[:-4]
            files += 1
            try:
                lines = open(p, encoding='utf-8', errors='replace').read().splitlines()
            except OSError:
                continue
            for line in lines:
                if line.startswith('DETECTOR-SAMPLE '):
                    try:
                        d = json.loads(line[16:])
                    except ValueError:
                        continue
                    if d.get('skipped'):
                        skipped[d['skipped']] += 1
                    else:
                        samples[d.get('stage') or '?'] += 1
                elif line.startswith('DETECTOR '):
                    try:
                        d = json.loads(line[9:])
                    except ValueError:
                        continue
                    s = named(d['sig'])
                    st, what, kind = (s.split(':') + ['', '', ''])[:3]
                    if kind in worlddiff.IGNORE or (st == 'Space' and what.startswith('noun_only')
                                                    and kind in ('creature', 'citizen')):
                        continue  # not compared any more (a log from before worlddiff.IGNORE had it)
                    seen[s] += 1
                    objs[s] += int(d.get('n', 0))
                    tests[s].add(test)
                    if len(examples[s]) < 3 and d['text'] not in examples[s]:
                        examples[s].append(d['text'])
    out = [f'# Divergence detector report ({datetime.datetime.now():%Y-%m-%d %H:%M})', '',
           f'{len(runs)} lab run(s), {files} test log(s). Comparisons per stage: '
           + (', '.join(f'{k} {v}' for k, v in sorted(samples.items())) or 'none')
           + (f'; skipped (not in the same place): {sum(skipped.values())}' if skipped else '') + '.', '',
           'A signature is `<stage>:<what>:<kind>`: host_only / guest_only (objects near the guest on one game only), '
           'unmatched (both have the kind, some without a partner), guest_local / host_local (objects the mod does not '
           'track), alive / scale / model / hp / pos (matched objects that differ), noun_only_* (a noun type in one '
           'game\'s whole list only). `seen` = comparisons with the finding, `share` = of that stage\'s comparisons.', '']
    by_stage = collections.defaultdict(list)
    for s in seen:
        by_stage[s.split(':')[0]].append(s)
    for stage in sorted(by_stage):
        n = samples.get(stage, 0) or 1
        out += [f'## {stage}', '', '| signature | seen | share | objects | tests | ticket | example |', '|---|---|---|---|---|---|---|']
        for s in sorted(by_stage[stage], key=lambda s: -seen[s]):
            ex = examples[s][0].replace('|', '/') if examples[s] else ''
            out.append(f'| `{s}` | {seen[s]} | {100 * seen[s] / n:.0f}% | {objs[s]} | {", ".join(sorted(tests[s]))[:80]} | '
                       f'{known(s) or "**new**"} | {ex[:160]} |')
        out.append('')
    if not seen:
        out.append('No findings' + (' (no comparisons: was SPORE_DETECTOR set?)' if not samples else '.'))
    text = '\n'.join(out) + '\n'
    dest = a.out or os.path.join(ROOT, 'out', 'detector', f'{datetime.datetime.now():%Y%m%d-%H%M}.md')
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, 'w') as f:
        f.write(text)
    print(text)
    print(f'written: {dest}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
