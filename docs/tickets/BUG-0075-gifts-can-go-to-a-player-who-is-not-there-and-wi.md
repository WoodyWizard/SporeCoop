---
id: BUG-0075
title: gifts can go to a player who is not there, and with 3+ players the Give button only reaches one player
status: fixed
kind: bug
stage: Tribe, Civ
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#53
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-civ
commits: 4c70be2
tests: make test (validate_tests: gift recipient)
signature:
docs: 
related: #53
switch: give.pick
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/53 (full text, discussion).

The host accepts a gift of tribe members, vehicles or a city to any player id, without checking that the player is in the session. A gift to an absent or invalid id loses those units for everybody. Tribe members given to `0xFF` can even be selected by everyone. Separately, the co-op window's Give button always gives to the first other player in the list, so in a 3- or 4-player game you cannot choose the recipient: a guest can only give to the host. Severity: wrong behaviour (minor).

### Where
- Tribe host command, `to` taken from the message unchecked: [tribe.cpp#L519-L522](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L519-L522), `give_on_host`: [tribe.cpp#L489-L505](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L489-L505)
- Civ host command: [civ.cpp#L1175-L1204](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L1175-L1204)
- Selection filters treat `0xFF` as "unknown, allowed": [tribe.cpp#L215-L224](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L215-L224), [civ.cpp#L211-L219](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L211-L219)
- `give_target()` and the console command: [coop.cpp#L2350-L2369](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2350-L2369), [coop.cpp#L2493-L2496](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2493-L2496), [coop.cpp#L2536](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2536), [coop.cpp#L2799-L2803](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2799-L2803)

### How to reproduce
Unvalidated id:
1. Tribe stage, host + 1 guest. The guest selects 2 of its members.
2. The guest runs `coop.give 7`.
3. Host `coop.tribe` lists those members with `owner=7`. Nobody can order them until the player set changes. With `coop.give 255`, both players can select them.
4. Civ stage, same with `coop.civ select <id> 1`, then `coop.give 7`: the vehicle is lost for the rest of the session.

UI with 3 players:
1. Tribe or Civ stage, host + guests A (id 1) and B (id 2).
2. On B, select own members or vehicles and open the co-op window: the Give button says "to <host>". There is no way to give to A.

### Expected
The host refuses a gift to an id that is not a present player (and tells the sender). The co-op window lets the player choose any other present player as the recipient, for example by cycling through them or by a target dropdown.

## Root cause
**What happens**

1. A guest's gift is `CMD_GIVE` with the target id: in Tribe it is the `action` field, in Civ a `to` byte. The host checks only that the sender owns what it gives, then writes `owner = to`.
2. Nothing checks that `to` is in `C.sess->players()`. Possible results:
   - `to` is a player who left a moment ago, or any id typed into `coop.give <id>`: the units belong to an absent player. In Civ nothing re-splits vehicles later; in Tribe only the next change of the player set does.
   - `to == 0xFE`: nobody can ever select them (`owner != my_id`).
   - `to == 0xFF`: the owner reads as "unknown" and the selection filters let **every** player select them.
   - `to == 0xFD` for a city (`kShared`): it becomes the shared capital-style city.
3. The notice names the target with `name_of(to)`, which is empty or a stale name for such ids.
4. UI: `give_target()` returns the first player that is not me. The button text and `give_do` both use it. On the host that is always the lowest guest id; on a guest it is always the host (id 0). With 3 or 4 players, the other guests can never receive a gift from the window.

### Confidence
CONFIRMED by reading the full path (sender, host handler, selection filters, UI). Not run on the lab.

## Fix
The unvalidated recipient was fixed already by the network message hardening (BUG-0201): both
`tribe::host_command` and `civ::host_command` refuse a `CMD_GIVE` whose target is not a player of
the session (`net::is_player`).
The co-op window part is fixed on fix/w3-civ (4c70be2): the recipient is picked among the other
present players (`net/give_target.h`): a ">" button next to Give (shown with 3 or 4 players) and
`coop.give next` go to the next one; a pick that left falls back to the first other player.
`coop.give fix 0` restores the old "first other player".

## Verification
Unit test in `make test` (tests/validate_tests.cpp, "gift recipient"): with players {0,1,2}, guest 2
picking 1 gives to 1 (the old logic, `fix=false`, gives to 0), fallbacks and the cycle. Not run in
the game with 3 players (the button only calls the same function).

### Related
- Vehicles of a player who left stay theirs (separate issue; same root of owners never pointing to present players).
- docs/re/tribe.md, section "Co-op features (pincer, duet, healers, chief, epic)"; docs/re/civ.md, section "Co-op features (siege, roles, trade, alarm, superweapon)".

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #53.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: recipient validation found fixed by BUG-0201; the recipient choice fixed on fix/w3-civ (4c70be2), unit test.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: switch: give.pick (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
