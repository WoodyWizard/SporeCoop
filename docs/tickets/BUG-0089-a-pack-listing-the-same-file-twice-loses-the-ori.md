---
id: BUG-0089
title: a pack listing the same file twice loses the original file on rollback
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#67
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-world
commits: 6e8e765
tests: tests/net_tests.cpp test_updater_dup (make test), tools/tests/win_unit.py (apply_pack)
signature:
docs: 
related: #67
switch: update.dup
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/67 (full text, discussion).

`parse_pack` accepts an update pack that lists the same path twice. `apply_pack` then handles both entries one after another on the same target: the second entry deletes the first entry's `.old` backup (which holds the player's original file), the second swap fails, and the rollback "restores" the new file instead of the original. The update reports failure, but the installed file is changed and the original is gone. Severity: minor (only a malformed pack triggers it; `tools/mkpack.py` does not produce one today), but the rollback promise in winupdate.h is broken.

### Where
* `parse_pack` (no duplicate check, also not case-insensitive): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/pack.cpp#L86-L150
* `apply_pack` (steps 1 and 2 and the rollback): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/winupdate.cpp#L206-L305
* the contract ("on any failure everything replaced so far is put back"): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/updater/winupdate.h#L27-L32
* the pack builder: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/mkpack.py#L16-L28

### How to reproduce
Host harness (Linux, g++). It uses the real `parse_pack` / `write_pack` and replays `apply_pack`'s steps on an in-memory file system:

```cpp
// g++ -std=c++20 -I. dup_pack.cpp updater/pack.cpp updater/sha256.cpp -o dup_pack && ./dup_pack
#include "updater/pack.h"
#include <cstdio>
#include <map>
#include <string>
int main()
{
    upd::Pack d;
    d.version = "0.8.1";
    upd::PackFile f;
    f.path = "SporebinEP1/mods/coop.dll";
    f.data = {'N', '1'};
    d.files.push_back(f);
    f.data = {'N', '2'};
    d.files.push_back(f);
    upd::Pack q;
    std::string err;
    bool ok = upd::parse_pack(upd::write_pack(d), q, err);
    printf("parse_pack: %s, %zu files, err='%s'\n", ok ? "accepted" : "rejected", q.files.size(), err.c_str());

    std::map<std::string, std::string> fs = {{"coop.dll", "ORIGINAL"}};
    auto exists = [&](const std::string& p) { return fs.count(p) != 0; };
    auto move = [&](const std::string& a, const std::string& b) {
        if (!exists(a)) return false;
        fs[b] = fs[a]; fs.erase(a); return true;
    };
    struct Step { std::string target, fresh, old; bool had, moved_old = false, placed = false; };
    std::vector<Step> steps;
    for (auto& pf : q.files) { // 1. every new file next to its target (both write coop.dll.new)
        Step s{"coop.dll", "coop.dll.new", "coop.dll.old", exists("coop.dll")};
        fs[s.fresh] = std::string(pf.data.begin(), pf.data.end());
        steps.push_back(s);
    }
    bool failed = false;
    for (auto& s : steps) { // 2. swap
        if (s.had) {
            fs.erase(s.old); // DeleteFileW(s.old)
            if (!move(s.target, s.old)) { failed = true; break; }
            s.moved_old = true;
        }
        if (!move(s.fresh, s.target)) { failed = true; break; }
        s.placed = true;
    }
    if (failed) // rollback, same order as apply_pack
        for (auto& s : steps) {
            if (s.placed) fs.erase(s.target);
            if (s.moved_old) move(s.old, s.target);
            fs.erase(s.fresh);
        }
    printf("apply: %s\n", failed ? "failed, rolled back" : "ok");
    for (auto& [k, v] : fs) printf("  %s = %s\n", k.c_str(), v.c_str());
    printf("original kept: %s\n", (fs.count("coop.dll") && fs["coop.dll"] == "ORIGINAL") ? "yes" : "NO");
}
```

Observed output:

```
parse_pack: accepted, 2 files, err=''
apply: failed, rolled back
  coop.dll = N2
original kept: NO
```

### Expected
`parse_pack` rejects a pack with a path that appears twice (compared case-insensitively, since the game runs on Windows), with an error such as "the pack lists a file twice: ...". `apply_pack` never runs on such a pack.

## Root cause
**What happens**

Pack with two entries for `SporebinEP1/mods/coop.dll` (data N1, then N2), the game folder has the original `coop.dll`:

1. `parse_pack` checks each entry's path and hash and accepts both (nothing compares paths with each other).
2. `apply_pack` step 1: `s.had = exists(target)` is true for both entries; both write `coop.dll.new`, so it holds N2 (N1 is lost).
3. Step 2, entry A: `DeleteFileW(coop.dll.old)`, move `coop.dll` (original) -> `coop.dll.old`, move `coop.dll.new` (N2) -> `coop.dll`. `placed = true`.
4. Step 2, entry B: `DeleteFileW(coop.dll.old)` deletes the original. Move `coop.dll` (N2) -> `coop.dll.old`. Move `coop.dll.new` -> `coop.dll` fails (the file was already moved): `err = "cannot install ..."`, `goto rollback`.
5. Rollback, entry A: `placed`, so `DeleteFileW(coop.dll)` (nothing there); `moved_old`, so move `coop.dll.old` (N2) -> `coop.dll`. Entry B: its `.old` is gone, nothing to restore.
6. End state: `coop.dll` = N2, the original is gone, and `apply_pack` returns false ("update failed"). The game is left with a file from a failed update and a version file of the old version.

On Windows the same happens with two paths that differ only in case (`SporebinEP1/mods/coop.dll` and `SporebinEP1/mods/COOP.dll`), for example two `.sym` files staged on Linux whose names differ only in case.

### Confidence
CONFIRMED by harness for `parse_pack` accepting duplicates. The `apply_pack` sequence is CONFIRMED by reading and replayed in the harness on a simulated file system; not run against a real Windows game folder.

## Fix
Fixed on fix/w2-world (6e8e765). `upd::duplicate_path` compares the paths without case (and with
'\\' as '/'); `parse_pack` rejects a pack with a repeated path ("the pack lists a file twice: ...")
and `apply_pack` checks the same before step 1 (packs built in memory). Old behaviour: `upd::g_dup_fix
= false` (`net_tests --old`, `win_tests --old`).

The review suggested:

In `parse_pack`, keep a `std::set<std::string>` of lower-cased paths and fail on a repeat. As defence in depth, `apply_pack` can check the same before step 1 (it also receives packs built in memory). Optionally `tools/mkpack.py` asserts unique lower-cased paths so a bad staging folder fails the release build.

## Verification
`make test` (test_updater_dup): `build/host/net_tests --old` FAILS (both duplicate packs accepted),
`net_tests` PASSES. Win32: win_unit `test_apply_pack` installs a pack listing coop.dll twice: with
`--old` the original is lost and the player's coop.dll.old overwritten; with the fix nothing is
changed.

The planned test was:

Add to the updater part of tests/net_tests.cpp:

```cpp
upd::Pack dup;
dup.version = "1";
dup.files.push_back({"SporebinEP1/mods/coop.dll", 0, {1}});
dup.files.push_back({"SporebinEP1/mods/coop.dll", 0, {2}});
CHECK(!upd::parse_pack(upd::write_pack(dup), q, err));
dup.files[1].path = "SporebinEP1/mods/COOP.dll"; // the same file on Windows
CHECK(!upd::parse_pack(upd::write_pack(dup), q, err));
```

Both CHECKs fail on main (the pack is accepted) and pass with the fix (`make test`).

### Related
None known.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #67.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-world: duplicate paths rejected in parse_pack and apply_pack (net_tests, win_unit).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: update.dup (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
