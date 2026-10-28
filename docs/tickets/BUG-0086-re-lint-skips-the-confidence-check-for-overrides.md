---
id: BUG-0086
title: re_lint skips the confidence check for overrides entries whose name is also in a generated .sym
status: fixed
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#64
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: 342e273
tests: tools/tests/tools_selftest.py relint (make test)
signature:
docs:
related: #64
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/64 (full text, discussion).

`tools/re_lint.py` (in `make test`) should fail on every entry of an `overrides_*.sym` file that does not say how sure it is. It keeps only the first entry it finds for each symbol name, and which file comes first depends on directory order. When an override corrects a name that a generated file (`gog2016_922af7c7.sym`, `march2017_dc04aee5.sym`) already has, which is the usual reason to write an override, its note is never checked. An override with `; TODO` passes the lint.

### Where
https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/re_lint.py#L12-L19 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/re_lint.py#L44-L47

### How to reproduce
In a scratch clone:

```sh
n=$(grep -m1 -oE '^[A-Za-z_][A-Za-z0-9_:]*' data/symbols/march2017_dc04aee5.sym)   # Anim::AnimatedCreature::PlayAnimation
echo "$n=0x00401000 ; TODO" >> data/symbols/overrides_dc04aee5.sym
echo "Foo::NewThing=0x00401000 ; TODO" >> data/symbols/overrides_dc04aee5.sym
python3 tools/re_lint.py | tail -3
```

Output:

```
overrides_dc04aee5.sym:311 Foo::NewThing: say how sure it is (verified / likely / unknown) and what it does
re_lint: ... 1 problem(s)
```

Only the new name is flagged; the override of the existing name with `; TODO` passes.

### Expected
Both lines are flagged: every line of every `overrides_*.sym` file is checked, whatever other files hold the same name.

## Root cause
**What happens**

```python
for f in glob.glob(os.path.join(ROOT, 'data', 'symbols', '*.sym')):
    for i, line in enumerate(open(f, errors='replace'), 1):
        m = re.match(...)
        if m:
            ...
            known.setdefault(m.group(1), (os.path.basename(f), i, (m.group(3) or '').strip()))
...
for name, (f, i, note) in sorted(known.items()):
    if f.startswith('overrides') and not re.search(r'\b(verified|likely|unknown|high|ported)\b', note, re.I):
```

`glob` returns files in directory order (unsorted). On the checkout used for this review it returned `gog2016_922af7c7.sym, overrides_922af7c7.sym, overrides_dc04aee5.sym, march2017_dc04aee5.sym`. A name in both a generated file and an override keeps the entry of whichever file came first. When the generated file wins, the override is not checked, and the lint also names the wrong file for that symbol. A second entry for the same name inside one overrides file is never checked either.

Smaller points on the same line: `high` and `ported` are accepted although CLAUDE.md lists only `verified` / `likely` / `unknown`, and the word may appear anywhere in the note ("calls an unknown function" passes).

### Confidence
CONFIRMED by running.

## Fix
re_lint checks every line of every `overrides_*.sym` in its own loop, independent of the
first-found-wins `known` table; `high` and `ported` are no longer accepted (CLAUDE.md lists only
verified / likely / unknown, and every current entry has one of them); all globs are sorted.

## Verification
`tools/tests/tools_selftest.py relint`: a copy of the tree with three appended override lines with `; TODO`: a name of
the generated march2017 file, a new name, and a second entry of a name the overrides file already has.
- FAIL with main's re_lint: the second entry of an existing override name is not flagged
  (deterministic). The generated-name case depended on directory order: on this file system main's
  re_lint happened to see the overrides file first and flagged it; on the reviewer's it did not.
- PASS with the fix: all three flagged; the real tree still has 0 problems.

### Related
CLAUDE.md section 1 ("`tools/re_lint.py` (in `make test`) fails when ... an entry has no confidence word").

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #64.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
