# SporeCoop Documentation

1. [Architecture and loading into the game](01-architecture.md) - how code gets into the Spore process, lifecycle, symbol database.
2. [SporeKit and the mod toolkit](02-modkit.md) - mod API, console, `sporectl`, hot reload, screenshots, input.
3. [Co-op gameplay](03-gameplay.md) - what it looks like for players, shared-world rules, stages.
4. [Netcode](04-netcode.md) - protocol, synchronization, interpolation, host authority.
5. [Reverse engineering](05-re-notes.md) - methodology (REA + porting ModAPI addresses), discovered functions and structures.
6. [Testing](06-testing.md) - Bottles, two game instances, fake peer, automated tests.
7. [Co-op in all modes](07-modes.md) - mode adapters, places, shared resources, phase transitions.
8. [Interface](08-ui.md) - "Co-op" entry in the main menu, connection window, lobby, LAN game discovery (native Spore windows).
9. [Playing together in all phases](09-coop-design.md) - the "one world, one team" concept, what's done, engine breakdowns in [re/](re/).
10. [Development pipeline](10-dev-pipeline.md) - test lanes, the lab, warm resets, invariants, chaos bot, bad network, fixtures, git and knowledge rules. Bugs not fixed yet: [known-issues.md](known-issues.md).
11. [Rivals: own bases in one world](11-rivals.md) - the second session mode (design): every player with its own nest, tribe, nation and empire, a race through every stage, war, peace and alliances between players, influence over NPC factions, the draft and the Chronicle.

* [Reporting a problem](REPORTING.md) (for players), [known issues](known-issues.md), [contributing](../CONTRIBUTING.md). Release trust (antivirus false positives, provenance, signing; owner steps): [release-trust.md](release-trust.md); security: [SECURITY.md](../SECURITY.md).

Target game versions: GOG "SPORE Collection", `SporebinEP1/SporeApp.exe`:
the 2016 build, SHA-256 `922af7c7908af2614b9caca2ffb1ec2353442022fd124866105724acaa24c26e`,
24,888,320 bytes (not supported by Spore ModAPI, which only knows Disk and March2017, so its
addresses are ported separately), and the 2024 build, SHA-256 `dc04aee5…`, 24,895,536 bytes, which
has the March2017 layout - see section 5. Where each was tested: [INSTALL.md section 2](INSTALL.md#where-sporecoop-has-been-tested).
