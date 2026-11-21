---
id: BUG-9306
title: Tribe (guest): the chief's stage powers (Fire Bomb, Summon) are refused, later stage powers likely too
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-powers
commits: 0306419f, db507eef, 8bd69ecc, 8836d797, 212b8595
tests: tools/qa_win (live two-game QA on the MSI and TITAN, 2026-11-21, switch off and on)
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
- Live QA 2026-11-21 (lead, two games on the MSI: host MSITribeHost instance 2, guest MSIGuest instance 1; the Creature part first with the host on TITAN QA 9): PASS (relay): guest `coop.tribe powers`: 0x36 unlocked (status 0); `coop.tribe powers use 0x36`: guest sent=1, host 00:39:06.035 "tribe: host: MSI's chief 13 uses power 0x36 (ability 15)", run=1; a second press 00:39:30 "refused ... the power is recharging". The power's effect was not seen on screen; Fire Bomb itself (a carnivore chief) not tested; off-run not done.
- Live QA 2026-11-21, second run (lead, MSI two games, build f07775ca, a fresh Creature -> Tribe entry, all six powers granted to the guest with `coop.tribe powers grant`, each pressed on the guest with `coop.tribe powers use`; the host's power log at 0.5 / 2 / 8 s): every power ran its own node on the partner's chief and started its recharge on the host (0x36 node 060B7090, 0x37 06088765, 0x38 060B70B4, 0x39 0609EACA, 0x34 060B708E; progress 0.00 -> 0.03 by 8 s). On the guest's own screen: 0x34 animals walk to the guest's chief, but the lure basket is not shown there (host object, not copied); 0x35 the guest's villagers carry food bundles, its food rose (28.5 -> 36); 0x37 (Fire Bomb) an explosion by the guest's chief at 1.5 s, the raiding rival tribe fled (images/live68_guest_power_0x37_firebomb.png); 0x39 tame sparkles over two animals beside the guest's villagers (images/live68_guest_power_0x39_tame.png; whose pet list they joined was not checked: the TribalGame's single list, see the history). 0x36 (the chief walks to a spot) and 0x38 (social) ran and recharged, but no effect was clear in the screenshots. CORRECTION (2026-11-21): the first run's "0x36 / 0x37 never ran on the partner chief" (recharging 0) did not repeat; cause not known. Off-run (`tribe.guest_powers off`) not done in this run; 67-dev (the report) is the off behaviour.

- Live QA 2026-11-21, 7Q A (aeaedfc3) (QA agent, fix/l47-qa 9d64f85d = fix/l47-powers aeaedfc3 + BUG-9308, MSI two games: host MSITribeHost instance 2, guest MSIGuest instance 1, a live Creature -> Tribe entry from Vandh): PASS (Tribe). Guest `coop.cards own 1 B6295724` ("sent to the host"; host
  "MSI's own creature card B6295724 kept"); guest `coop.tribe powers`: 0x37 status 0, the other five status 10, "partner_cards
  ... -> power bits 00800000"; `powers use 0x37`: host 04:10:35 "MSI's chief 10 uses power 0x37 (ability 16, its own cards)",
  the Fire Bomb burst at the guest's chief on the guest's screen (out/qa-shots/t_g_firebomb_own.png). Host `coop.cards
  set 1 1 E5F355B3`: the guest's 0x37 went to status 10 and 0x34 to 0 ("chief powers follow its own cards: ... unlock word
  00100000"); a 0x37 press is then stopped by the guest's own bar (status 10), so the host's refusal line was not
  exercised. Off at runtime (`tribe.partner_cards off` on both): the guest's unlock word stayed 00100000 (a runtime
  switch does not recompute; restart needed for an off-run). A real Creature -> Tribe graduation (tribe_up) logged no
  "cards:" line on either game, `coop.cards` caught=0, host records=0, the creature card 00000000 on both games
  (effective cell 5ECE4770): the guest's game wrote no card of its own at the co-op graduation (or none is caught).
  Civ (7Q A, TITAN, see BUG-9311): FAIL, the partner's card unlocks also reach the host's nation. Civ superweapon from
  the partner's nation (9e3e326a, TITAN): the refusal works ("your nation cannot fire superweapon 9 now (unlocked=0 ...)"),
  a granted fire crashed the host (BUG-9310). The 0x39 tamed animals' list: not reached (no tool reads the TribalGame
  list).

- Live QA 2026-11-21, third run (fix/l47-tribe, MSI two games, Tribe from Vandh, powers granted with `coop.tribe powers
  grant`), the leftovers:
  * 0x36: its effect A86F8D7E plays at a spot about 230 m from the chief, for the host's own chief (09:53:22, at 266.9
    -334.9 -257.6) and a partner's (09:54:15, 282.9 -324.7 -253.8); the relay dropped both ("not on the local player").
    Fix `tribe.power_effects` (effects.cpp): with it the guest's press at 11:14:30 made the host send it (`coop.effects`
    "chief power 0x36 effects sent=2") and the guest's game played it ("effect: from 0 played on other id A86F8D7E ...
    pool 5", 11:14:44). Off-run with a partner's press not done (recharge); before the fix the same press logged only
    "not on the local player" (09:54:15).
  * 0x38: no world effect at all for either chief's press; the B490E2EC logged 0.1 s after the host's own press is a
    selection ring, not the power (docs/re/tribe.md#tribe-partner-totems). Its result is citizens walking to the chief
    (positions, mirrored): nothing to add.
  * 0x34 lure basket on the guest: not changed (a visual-only copy of a host object; plan unchanged).
  * 0x39 / pets: new tool `coop.tribe powers pets` (the TribalGame list +0x94..+0x98 and every tribe's political id).
    A guest's 0x39 (11:28:24, its chief at its own village): three animals tamed (sparkles over them on the guest's
    screen, out/qa-shots/bug9306_guest_0x39.png); the HOST's list then held 3 entries, each with political id 0x228C
    (the guest's village), the guest's own list empty. The animals follow the partner's tribe, but they sit in the
    host's single pet list and use up the host's cap of 4 (prop 0x217997F4): gameplay question for the owner (one
    shared cap for the party, or one per village).

- Live QA 2026-11-21, fix/l47-tribe2 (MSI two games, Vandh), decision 7W A (each village its own cap of 4 tamed
  animals, as solo play): switch `tribe.pet_cap_per_village` (on). Both chiefs walked to a group of wild animals
  (`coop.tribe order <chief> 4 x y z`), power 0x39 granted on both games.
  * FAIL (`coop.fix tribe.pet_cap_per_village off`, 12:45): the host's press tamed 4 (list 4 entries, all 800021D3);
    the guest's press then tamed one more, and it was one of the HOST's pets: its political id became 0x228C and the
    list held it twice (5 entries, 32DAFAD8 at 3 and 4). So with one shared list a partner's press both counts against
    the host's cap and takes the host's own pets. (Why the native count let one more in at 4 of 4 is not known: the
    prop's value in this world may be 5, or the node's step ran twice.)
  * PASS, partial (on, 13:05): the host tamed 4 (list 4); the guest's press ran the node 137 times with the list showing
    only its own pets (`split=137 restored=548`), the host's 4 pets kept the host's id and the list stayed at 4 (no
    re-tame, no duplicate): the candidates exclude every animal already in the list (their dead flag +0xB5E set for
    the call only). It tamed nothing: no adult animal was left in reach (the two others were young). A second press at
    another group 9 minutes later was refused on the host ("the power is recharging", while the guest's bar showed
    status 0). Not seen: a partner taming its own 4 while the host has 4.
  * Seen on the way: the host's 4 pets fought wild animals (13:08 a pet killed npc 96) and by 13:14 the list was empty,
    the pets' objects reused by the creature pool for another species (they died). ? Whether the per-call dead flag can
    play a part: not seen in the node's code (the flag is read as the candidate filter), not ruled out at runtime.
  * The lure basket (0x34) on the guest: not done. A display-only copy needs an object the guest's game draws but never
    saves or simulates; the mod has no such model-only object (a cOrnament copy, cOrnament_SetModel, would be a
    planet record the guest's own saves keep, and its removal before every save, load, editor and stage change would
    be new code), so it is left as an open issue (docs/known-issues.md#known-tribe-l47-tribe2).

- Live QA 2026-11-21, fix/l47-final-tribe (MSI two games, host instance 2, guest instance 1, a fresh Creature -> Tribe
  entry from Vandh; power 0x39 granted on both games, each chief walked to its own herd with `coop.tribe order <chief>
  4 x y z`). The native cap is **5**, not 4: `coop.tribe powers pets` now prints each chief's 0x39 ability prop
  0x217997F4, `cap=5` for both chiefs in this world. That explains the earlier off-run's "one past 4": the host had 4
  pets, the shared cap was 5, so one more came in (there it re-tamed a host pet, see below). Each village now gets the
  game's own cap (5 here), as solo play.
  * Host 14:00:28: the host's press tamed 5 (list 5, all 800021D3, `cap=5 pets_in_list=5`).
  * FAIL (`coop.fix tribe.pet_cap_per_village off` on the host), 14:00:51: the guest's press ran on the host (the
    partner chief's recharge started, progress 0.04) and tamed **none**: the list stayed at the host's 5 (one cap for
    the party) (out/qa-shots/bug9306_fail_off_partner_tames_none.png).
  * PASS (on), 14:02:04 (the partner's recharge cleared with mem.write; `coop.tribe powers ready <type> <seat>` does it
    from now on): the guest's press tamed **5 of its own** (list 10: five 0000228C, the host's five still 800021D3, no
    entry twice); `split=9 restored=45 taming_steps=18`. Both chiefs were then walked home: 40 s later each village's
    five pets stood at their own chief's village (partner's at 311..313 -391..-394, host's at 227..235 -443..-447)
    (out/qa-shots/bug9306_pass_guest_pets_follow_own_chief.png, bug9306_pass_host_pets_follow_own_chief.png; the
    tame sparkles over them).
  * The one-call dead flag (+0xB5E): now set only in the node's taming step (step 3 here, read from the node's own jump
    table: `power 0x39's taming step is 3`), not on every frame the node runs (275 calls for two presses, 18 in the
    taming step). Pets left the TribalGame list later in both sessions (here all 10 at once between 14:04 and 14:08,
    while no node call ran: the counters did not move after 14:02), so the earlier "the pets died" is not the flag but
    the game's own (a timed tame, or the "rival tribe formed" cinematic that started 14:06:16: not told apart); the
    animals went back to their herds (#109 was at its herd 14:17).

## History
- 2026-11-20: opened.
- 2026-11-20: fix/l47-powers: tribe.guest_powers; Civ and Space surveyed (plan above). Static analysis + build only.
- 2026-11-21: live two-game QA (see Verification).
- 2026-11-21: live test (coordinator, feat/lineages-47 5f077c4f, MSI two games, all six powers granted): the host's own
  chief: 0x34 lure basket, 0x35..0x37 run (recharging after); 0x38, 0x39 nothing (no recharge). A partner's presses: the
  host ran all six ("uses power"), the partner chief showed a recharge for 0x34 and 0x35 only; on the guest 0x34 brought
  animals to its chief (the basket not shown on the guest), 0x35 gave food. Analysis (Ghidra, fix/l47-powers): the type
  -> node map is in docs/re/tribe.md#tribe-chief-powers (0x37 is most likely Fire Bomb, 0x39 the animal taming, 0x34
  the lure basket); no player-tribe test was found on the 0x36 / 0x37 paths. Added: the host's energy check (status 1),
  a 0.5 / 2 / 8 s state log of every power (partner's and host's own), reach / cost / energy / in-use in `coop.tribe
  powers`. Not changed: the lure basket on the guest (a host object the mod does not mirror; showing it needs a
  visual-only copy whose own update is held, plan), the tamed animals' list (the TribalGame's one list; their political
  id already follows the acting chief, ~).
- 2026-11-21: second live run, all six powers run for a partner (see Verification).
- 2026-11-21: Civ (owner: "Also test the Civ superweapons like MOAB"): `civ.guest_superweapons` on fix/l47-powers: the
  host fires a partner's superweapon request with the sender's own seat's nation (its unlocks, money, cooldown, cities;
  Ghidra: FireSuperweapon / CanUse need no player nation, only the history record and stats event are player-only,
  docs/re/civ.md#civ-guest-superweapons) and tells the sender when its nation cannot fire it. Tools: `coop.civ sw` (per
  nation), `coop.civ swgrant <seat> <type> [money]`. Open: the partner nation's card-based unlocks come from the host's
  cPlayer (RefreshUnlocks); the global types 2/5/8 reach the other founders' cities (owner decision). Built, not run.
- 2026-11-21: CORRECTION (fix/l47-powers): asked to make the host use a partner's own Civ unlocks ("the guest's bar
  shows its own unlocks"). Found in the code instead: wsync.stages (BUG-0577) writes the host's cPlayer stage cards
  (+0x10FC..+0x110C) into every connected guest's cPlayer in every planet stage, so the guest's own game also offers
  the host's cards' powers (Tribe chief powers via 0xD1F400, Civ unlocks via RefreshUnlocks); host and guest agree on
  the host's history. The mod keeps no per-founder record of a partner's own cards, so there is nothing to send or
  apply yet. Not implemented; decision asked: (A) record each founder's own card at each graduation per seat (the
  guest's game reads its new card before wsync overwrites it, sends it, the host keeps it in the founder roster and
  the world save), then use it for that founder's chief powers (an exception in wsync.stages for the unlock words, or
  the host checks) and its nation's Civ unlocks (RefreshUnlocks run with that seat's cards swapped into cPlayer, as
  h_Unlocks already swaps the city view); (B) keep one shared history for the party (today's behaviour).
- 2026-11-21: decision 7Q A ("Own powers") on fix/l47-powers: mods/coop/cards.{h,cpp} (MSG_CARDS 110: a guest's own new
  card caught in wsync's "stages" row before the host's replaces it, kept by the host per identity in `<save>.duo-cards`,
  sent back to the guest); `tribe.partner_cards` (the guest's chief-power unlock words from its own cards; the host
  refuses a power the partner's own cards did not earn); `civ.partner_unlocks` (a partner nation's unlocks computed
  with its own cards; team sharing of unlocks only for the global types). Founders without a record keep the shared
  cards (logged). Tools: `coop.cards [own|set|clear]`, `coop.tribe powers`, `coop.civ sw`. Built, not run. Open: whether
  a guest's game writes its own card at a co-op graduation (to be seen in the log at the next graduation).
- 2026-11-21: second live QA run (see Verification).
- 2026-11-21: third live run (fix/l47-tribe): 0x36 effect relayed (`tribe.power_effects`), 0x38 has no effect, pets tool.
- 2026-11-21: decision 7W A on fix/l47-tribe2: `tribe.pet_cap_per_village` (FAIL off seen; PASS partial, see
  Verification); the lure basket's stand-in not done (blocker recorded).
- 2026-11-21: fix/l47-final-tribe: the real PASS (both villages at the cap, each with its own pets) and FAIL; the cap
  is the game's 5; the split and dead flag only in the taming step; `coop.tribe powers pets` caps,
  `coop.tribe powers ready`.
- 2026-11-21: set to fixed for 68-dev: merged into feat/lineages-47 with the live FAIL / PASS in Verification (any open part stays in docs/known-issues.md).
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
