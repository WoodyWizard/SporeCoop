---
id: BUG-9306
title: Tribe (guest): the chief's stage powers (Fire Bomb, Summon) are refused, later stage powers likely too
status: open
kind: bug
stage: Tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch: fix/l47-powers
commits: 0306419f
tests:
signature:
docs:
related: BUG-9303
---

## Report
Live 67-dev, Tribe, a guest (owner, 2026-11-20): "Firebombs did not work for BigTexas." "They don't need to hold torches,
the chieftain will always be able to throw the bombs and he refused." Fire Bomb is the chief's power from ending the
Creature stage as a carnivore (7N B). "The Summon power is the same type of power as Firebombs. Same with the MOAB and
stuff later on in the game." The owner expects every stage power a guest has to be broken.

## Root cause
Not found yet. Likely: the guest's chief power order is not in the host's allowlist (host_command, BUG-9303), or it
acts on the host's chief.

CORRECTION (2026-11-20, fix/l47-powers): neither. The press never left the guest's game (below); the host's allowlist
never saw it.

Observed in the code and in Ghidra (GOG 2024, Steam dump on TITAN; docs/re/tribe.md#tribe-chief-powers), not yet in a game:
* A Tribe chief's stage powers are the leader's six extra abilities, types 0x34..0x39. The bar's press (AbilityBar_Use
  -> 0xD48AB0 -> Ability_Use 0xD1E930) runs `DoAction(22, no target, Property(uint32 = the ability's index))` on the
  bar's avatar, which in Tribe is `GetPlayerTribe()->leader`. CitizenAction 22 is the chief power; its behaviour nodes
  (.data 0x1593C90..) start the recharge and play the power.
* On a guest the avatar is its own game's mirror of its village's chief (tribe.own_player_village). Ability_Use called
  DoAction on that mirror directly, not through DoActionForObjects (the order relay), so nothing went to the host; and
  the mirror's behaviour tree is skipped by h_BTTick, so nothing happened on the guest either. (Inferred: this is the
  "refused" the owner saw; a refusal hint from the bar's status, 10 not earned / 0xB / 0xF, is the other possibility:
  `coop.tribe powers` on the guest shows which.)
* The unlock check (0xD1F400) reads the local cPlayer's cell and creature cards (+0x10FC, +0x1100), cached as player
  properties 0x5FF7BAF / 0x5FF7BB6; the guest's bar offers what the guest's own cPlayer earned (if the guest's cPlayer
  came from the host's save it may hold the host's cards: unknown).
* Two power nodes name the PLAYER tribe, not the acting chief: the lure object's end messages (sender the player
  tribe's chief, 0xBCBDF0) and the animals that join the TribalGame's list (0xC7DE10, the player's pets). Run for a
  partner's chief on the host they credit the host's tribe (inferred, not seen).
* Civ (survey, code only): a guest's FireSuperweapon (types 0..11: the three archetype abilities per card and the
  planet-wide ones, docs/re/civ.md "Superweapons") goes to the host as CMD 5 (civ.cpp h_FireSW /
  host_superweapon_request), and the host fires it with `player_civ()`, the HOST's nation (its unlock, money, cooldown,
  nearest city), also with civ.multi_founders on. A partner's own nation's power is therefore refused when the host's
  nation lacks it or its money (CanUseSuperweapon false: nothing fires, nothing is said), or it fires from the host's
  cities with the host's money.
* Space (survey, code only): each player's own game fires its own ship's tools, the archetype / consequence abilities
  among them (space_trade.cpp lists them as tools); only terrain sculpt / colour hits are relayed (spacecoop.cpp). Not
  checked: whether a power's effect on shared state (a colony, relations, a planet) reaches the other players.

## Fix
Done on fix/l47-powers (built; not run in a game yet):
* `tribe.guest_powers`: a hook on Ability_Use. On a guest (village orders, Tribe, not local play) a press of a power
  0x34..0x39 sends CMD_CHIEF_POWER (16: the chief's member id, the type) instead of running on the mirror. The host checks
  the sender commands a partner village, the id is that village's current native chief, the chief is alive, up (+0xB5E,
  founder rescue), its AI word lacks 0x10000, it has that ability and it is not recharging; then it runs the game's own
  `DoAction(22, 0, Property(uint32 index))` on that chief (the property Ability_Use builds) and logs
  `tribe: host: <name>'s chief <id> uses power 0x.. (ability n) rec=...`. A refusal is logged through the BUG-9303 line
  with its reason. Off: the press stays on the guest's mirror (nothing happens).
* Tools: `coop.tribe powers` (bar status, cards, unlock words, counters, last power), `coop.tribe powers grant <type>`
  (tests: the local player earns the power, a saved player property: test worlds only), `coop.tribe powers use <type>`.
* Not done (plan):
  1. Which type is which power (Fire Bomb, Summon Animals, Soothing Song, ...): read in a game with `coop.tribe powers`
     for founders with known cards, then name them in docs/re/tribe.md.
  2. The guest's bar never sees the recharge (the mirror's +0xC28 / +0xBF8 are not synced); presses during it are
     refused on the host and logged. Mirror the two fields for the own chief if players find it confusing.
  3. The two player-tribe effects above (lure messages, pets list): check in a game; if they credit the host, run those
     with the partner's village as the player tribe for the call (the BUG-9153 swap in reverse) or re-own the animals.
  4. Civ: fire a partner's request on its own seat's nation (`a_CivByPol(nm(), g_founder_nations.entries[seat].faction)`
     as the city purchase does) and tell the sender when CanUseSuperweapon refuses; first verify in Ghidra that CanUse /
     FireSuperweapon work for a non-player cCivilization (its unlocks, the player event table 0x1469DA8) and which civ the
     guest's own UI passes to FireSuperweapon. New switch `civ.guest_superweapon`.
  5. Space: a two-player check of one archetype ability with a shared effect per card (Planet Buster, Global Happiness,
     Zoo Planet) to see whether the other game sees the effect.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
- 2026-11-20: fix/l47-powers: tribe.guest_powers; Civ and Space surveyed (plan above). Static analysis + build only.
