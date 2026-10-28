---
id: BUG-0047
title: RefPack decoder reads one byte past a 5-byte compressed record
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#25
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-net
commits: aa6d418
tests: net_tests test_refpack_short (ASan in test-asan)
signature:
docs: docs/06-testing.md#testing-net-w3
related: #25
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/25 (full text, discussion).

`dbpf::refpack_decode` only checks `n >= 5`. When the first flag byte has bit 0x80 set (4-byte size), it reads `be32(src + 2)`, which needs 6 bytes, so for a 5-byte input it reads one byte past the buffer. This is minor: today only local packages (EditorSaves, Pollination, game packages) reach the decoder, not network data, and the over-read is a single byte. But the function is meant to take untrusted input (its own comment says creations come over the network), and AddressSanitizer flags it.

### Where
- https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/dbpf.cpp#L107-L118
- Callers: `dbpf::decode` https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/dbpf.cpp#L70-L77 (used by `Package::read`, `find_keys` and `creations.cpp` `records_from_game`)

### How to reproduce
```cpp
#include <cstdio>
#include <vector>
#include "net/dbpf.h"
int main() {
    uint8_t* p = new uint8_t[5]{0x90, 0xFB, 0, 0, 0};
    std::vector<uint8_t> out;
    printf("%d\n", dbpf::refpack_decode(p, 5, out));
    delete[] p;
}
```

Build: `g++ -std=c++20 -g -fsanitize=address -Imods/coop h.cpp mods/coop/net/dbpf.cpp`

Observed: `ERROR: AddressSanitizer: heap-buffer-overflow ... in dbpf::refpack_decode ... dbpf.cpp:115`.

### Expected
A 5-byte input with the 4-byte-size flag is rejected (`false`) without reading past `n`.

## Root cause
**What happens**

```cpp
if (n < 5)
    return false;
size_t o = 2;
uint32_t size;
if (src[0] & 0x80) {
    size = be32(src + 2);   // reads src[2..5]: src[5] is out of bounds when n == 5
    o = 6;
}
```

With `n == 5`, `src[5]` is read. Afterwards `o = 6 > n`, so the decode loop does not run and the function returns `out.size() == size`, which is true only if the bogus size is 0. A record of exactly 5 bytes with the compression flag set (a damaged or crafted package entry) is enough.

### Confidence
CONFIRMED by harness (ASan). Not verified: whether any real package has such an entry. Network reachability was checked by reading: received creation bundles are written raw with `dbpf::write` and parsed by the game, not by this decoder.

## Fix
`dbpf::refpack_decode`: with the 4-byte-size flag the header needs 6 bytes; `n < 6` returns false
before `be32(src + 2)`. A bounds check, no switch.

## Verification
Unit test `test_refpack_short`: the 5-byte record `{0x90, 0xFB, 0, 0, 0}` in an exact-size heap
buffer, CHECK the result is false.
- FAIL: the ASan build of net_tests with main's `dbpf.cpp` (built by hand, 2026-10-28):
  `AddressSanitizer: heap-buffer-overflow ... READ of size 1`.
- PASS: `make test` (`net_tests` and `net_tests_asan`).

### Related
- `test_refpack_untrusted` (the existing untrusted-input checks of the same decoder).
- docs/re/creations.md (package formats).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #25.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-net (wave 3).
- 2026-10-28: released in 0.7.3.0.
