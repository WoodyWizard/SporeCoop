---
id: BUG-9081
title: Guest does not receive native trade routes or economic purchase progress
status: fixed
kind: desync
stage: Civilization
reported: 2026-11-07
source: Targeted economic playability check
role: guest
versions: duo22-dev
fixed_in: 0.7.3.6-duo23-dev
branch: feat/lineages-civ-combat
commits: aba17900
tests: tools/tests/duo_civ_economic_state.py
signature: Host route count 1 and buy 16.813276; guest route count 0 and buy 0
docs: docs/re/civ.md
related: BUG-9079 BUG-9077
---

## Reproduction

On isolated DuoQAPlayabilityTradeE with the installed Duo 22 DLL, native
relationship categories were made friendly (4) in both directions as fixture
setup. Guest mouse input bought an economic vehicle, selected it and ordered
a route to the rival. The host accepted the exact target and its native AI
created the route and switched to delivery order 5. After its first delivery,
host city +55C had one route and +758 held 16.813276 for buyer 00002231.
The guest had no route and zero purchase progress. Both had zero faults.
No route or buy progress was written by the fixture.

Evidence: build/local/playability-trade-e-controls.log and
playability-trade-e-progress.log. This is distinct from BUG-9079's hostile
route refusal. Existing city snapshots carry military capture only; there
is no native route or economic meter replay.

## Scope

Replicate host-owned route and economic progress state to guest native cities.
Preserve native trade AI, prices, shared treasury, city ownership and campaign
credit. Test removal and reconnect separately from the final purchase action.

## Verification

The optional host economic snapshot replays native routes, meter/buyer and
the complete per-buyer pricing map outside the render frame. It validates
world, epoch, sequence, live city identity, bounded payloads and planner
holds. Native map insertion and iterator erasure preserve game allocation.
The regression switch is civ.economic_state.

FAIL on installed Duo 22: bug9081-before.log. A route/meter-only patch
still failed the stronger pricing check in bug9081-price-before.log.
PASS complete fix: bug9081-pass-c.log and bug9081-pass-d.log (after real
reconnect into a new native slot). Both verify route removal/recreation,
changed native purchase progress and erasure of a stale guest pricing key.
bug9081-pass-e.log additionally verifies a newly changed pricing value.
These controlled changes test replication; the initial route and delivery
were earned by native AI from guest mouse input in TradeE.

Windows unit suite: bug9081-unit.log ALL OK. Full portable networking suite:
bug9081-net-unrestricted.log ALL OK. The sandboxed run failed localhost
connection tests; rerunning the identical executable outside the sandbox
passed. Expanded pricing-packet cases: bug9081-packet2.log ALL OK.
Docs/symbol lints pass. Both final games reported zero faults and exited
normally; the newest crash report remained the older BUG-9080 test report.

This does not qualify the guest's final native Buy City interaction.

## History

2026-11-07: Fixed and verified on isolated Windows peers; included in the
private Duo 23 ZIP, whose install/uninstall/reinstall test passes.
