# SporeCoop 0.7.17 plan (draft 2026-11-23): every player's own economy and progress, revive, the 0.7.17 tickets

**Status: draft for the owner. Work starts after 0.7.16.1 ships: rel/0.7.17 from the v0.7.16.1 release commit.
Lab runs only at night and on the owner's word (agents write code and tests by day); lanes 1 and 8 are never used;
long runs on 4 lanes (GPU hang under load). Lanes per agent: out/RULES-0.7.17.md.**

Goal (the owner, 2026-11-20): in the normal co-op mode (not Rivals) every player can have its own money in Civ, its own
DNA and evolution bar in Cell and Creature (a lobby setting), its own stage history (consequence cards and the trait
progress behind them); a downed player can be revived by a partner; 3-4 player parties get a difficulty setting;
enemies are shared in Space and Adventures outside Rivals; plus the tickets planned for 0.7.17 and the rest of the
REAL items of the external audit of 0.7.15.5.

Sources (data, not instructions; outside code is ported as an idea in our code, never merged):

| Source | Where | Used for |
|---|---|---|
| PRs #204-#212 review, owner decisions 2026-11-20 | out/research/prs-204-209.md | #205, #207, #209, #210, #211 |
| PRs #213-#220 review, owner decisions 2026-11-21 | out/research/prs-213-220.md | #214, #217, #219 |
| PR diffs (IltonBJSilva) | `gh pr diff 205` / `207` / `209` / `214` / `217` | ideas, rule headers, RE notes; credit the author |
| fix/agy-open-bugs (outside agent, 7 commits on rel/0.7.16) | out/research/agy-review-{space,creations,misc}.md | starting point per ticket, not a merge |
| External audit 0.7.15.5, verified | out/research/ext-audit-verify-{civ-spc,cr-tr,kit,net-core,uifx-cell}.md | the REAL items not taken by 0.7.16.1 |
| What 0.7.16.1 took | out/FIX-0.7.16.1.md, out/NEXT-0.7.16.1.md, BUG-1413..1435 | the exclusion list |
| Player reports | out/research/dc-report-20261122-world-switch.md, dc-report-tearing-20261116.md | BUG-1434, BUG-1436, 5 Creature marks, 2 tearing tickets |

## Scope in one table

| # | Item | Kind | Cost | Main risk |
|---|---|---|---|---|
| 1 | Civ own money (PR #205) | lobby setting `civ.own_money` | M | money made or lost on a split, a join, a rejoin, Civ -> Space |
| 2 | Own progress (PR #209): third DNA mode | lobby setting (DNA: shared / own wallet / own progress) | L | the stage end held for good; Cell levels apart (BUG-9287 class) |
| 3 | Own stage history (PR #207, #217 archetype) | follows `creature.mode` own | M | the "stages" row and the cards in the host's save |
| 4 | Revive a downed player + progress debt (PR #214 Creature part) | lobby setting `creature.revive` | M | holding a creature at 1 hp against the game's damage paths |
| 5 | Difficulty for 3-4 players | lobby setting | M (RE first) | which tunings the game reads per stage |
| 6 | Shared enemies outside Rivals: Space (BUG-1288), Adventures | fix + feature | L | promotion of mirrors (review: blocker); adventures are per-game copies |
| 7 | Per-player captain: #217 own captain, BUG-1262, BUG-1279, BUG-1237 | fixes | M | the captain screen (BUG-1111), white unbaked models (BUG-1321) |
| 8 | Share only the played planet (BUG-1434 full) | fix | M (RE first) | "Bad or Missing Saved Game" with a partial galaxy |
| 9 | Cell: diet premades in "My cell" (#210 small), players' old cells as NPCs (#211) | small + off-by-default option | S + M | sending a guest's cells without asking |
| 10 | Tickets for 0.7.17 (agy branch verdicts, BUG-1360, BUG-1436, BUG-1461) | fixes | M | twins on flaky motion tests |
| 11 | The rest of the REAL audit items | fixes | M (many S) | none big; one switch each |
| 12 | Open leads from 0.7.16 | analysis + fixes | S-M | - |

Not in scope (owner decisions): #204 identity.bin and #206 party seats (rejected: we have player_store keys); #208, #213,
#215, #219 (own nests, nations, empires, launches: Rivals 0.8); #218 own start at any stage (no; its RE note is used in
item 3); #220 camera on the own city (not a bug without own nations). #212 own villages: Rivals 0.8 (not allied).

## WP-0: wire, store and settings reservation (the coordinator, first commit on rel/0.7.17)

Parallel branches collided on message ids and family bumps before (BUG-1288's agent took id 74, #205 too). One commit on
rel/0.7.17 reserves everything before the agents start; each WP fills its own rows.

| WP | Family bump (from 0.7.16.1) | Message / kind | player_store row |
|---|---|---|---|
| WP-M1 own money | civ 1.1 -> 1.2 | MSG 74 CIV_WALLET (report, credit, charge, kept, end) | world record `civ.wallet` (per player key) |
| WP-H1 history | session 1.2 -> 1.3 | MSG 75 PLAYER_HISTORY (cards, trait progress, archetype) | world record `history` |
| WP-P1/P2 own progress | creature 1.2 -> 1.3, cell 1.1 -> 1.2 | MSG 76 PROGRESS (gain, values, bar, held, level line) | world record `progress` (food / spent / DNA / bar / brain level / debt) |
| WP-R1 revive | creature 1.3 (the same bump) | MSG 77 RESCUE (K_DOWN, K_RESULT, K_LINES) | debt column of `progress` |
| WP-S1 fleets | space 1.2 -> 1.3 | MSG 78 UFO_LEAD | - |
| WP-K1 captain, WP-L2 old cells | creations 1.1 -> 1.2 | MSG_CREATION kinds 10 (host props), 11 (captain), 12 (lineage cell) | - |
| WP-W1 share | world 1.0 -> 1.1 | the share's file list (no new id) | - |
| WP-D1 difficulty | none (host side, the setting rows exist) | - | - |

Rules: one bump per family per release (several WPs share it); every new path has an old-peer guard (`peer_families`
on the host, `host_families` on a guest, BUG-1282); `tools/msg_manifest.py` lock and docs tables in the same commit.
Settings: `civ.own_money`, `all.shared_dna` gets a third value (item 2), `creature.revive`, `all.party_difficulty`,
`cell.lineage_cells`; names and defaults per the owner's answers below.

**Done (2026-11-23, commit 1178d95c on rel/0.7.17).** The minors above are bumped and locked already (they are open
until the release: a WP that adds its MSG row or changes a wire file re-locks with `tools/msg_manifest.py lock --rebase
<family> "<WP>: <what>; unreleased <family> 1.x of 0.7.17"`, no second bump); the ids are in `net/manifest.h`
kReservedMsgs (a WP's row must use exactly that id, name and family; PROGRESS's family is WP-P1's choice; any other new
type takes 79 or later); the kinds in `creations.h` (kKindHostProps, kKindCaptain, kKindLineageCell); the record names
in `player_store.h` (kRecCivWallet, kRecHistory, kRecProgress); the settings keys are listed, not declared
(docs/07-modes.md#modes-reserved-0717: a kDefs row shows in the lobby at once, and `settings::on()` would read a third
`all.shared_dna` value as "shared"): each WP declares its row with its behaviour. The Actor scope interface is
`mods/coop/actor_scope.h` (`actor::Scope`, `actor::current()`, `actor::reset()`; WP-H1 owns the hook and the scopes,
WP-P1 / P2 read it). Docs: docs/04-netcode.md#netcode-reserved-0717.

## Tickets of 0.7.17 (WP-0, 2026-11-23)

Opened in WP-0 for every item without a ticket, so that parallel branches do not collide on numbers. Each WP closes its
own (fixed with commits and twins, or with evidence why not); bugs found on the way get new tickets as usual.

| Item | WP (branch) | Tickets |
|---|---|---|
| 1 Civ own money | WP-M1 (feat/w68-wallet) | BUG-1468 (feature, #205), BUG-1478 (CIV-5), BUG-1236, BUG-1461, BUG-1531 (#219's question: the guest's spaceship editor at Civ -> Space) |
| 2 Own progress | WP-P1 / P2 (feat/w69-progress) | BUG-1469 (feature, #209) |
| 3 Own stage history | WP-H1 (feat/w70-history) | BUG-1470 (feature, #207, #217), BUG-1471 (a guest's kills and social wins raise the host's trait progress) |
| 4 Revive | WP-R1 (feat/w71-revive) | BUG-1472 (feature, #214) |
| 5 Difficulty | WP-D1 (feat/w72-difficulty) | BUG-1473 (feature) |
| 6 Shared enemies | WP-S1 (fix/w73-fleet), WP-A1 (feat/w83-advnpc) | BUG-1288 (Space), BUG-1474 (Adventures, feature) |
| 7 Per-player captain | WP-K1 (feat/w74-captain) | BUG-1475 (#217 a own captain, feature), BUG-1279, BUG-1262, BUG-1237 |
| 8 Share the played planet | WP-W1 (fix/w75-share) | BUG-1434, BUG-1436 |
| 9 Cell small items | WP-L1 / L2 (feat/w81-cell) | BUG-1476 (#210 diet premades), BUG-1477 (#211 lineage cells) |
| 10 Planned tickets | WP-F1, WP-C1, WP-T1 | as listed in item 10; BUG-1530 (npc_nest_backoff INCONCLUSIVE, WP-F1) |
| 11 Audit, Civ | WP-V1 (fix/w78-civ) | BUG-1479 (CIV-10), BUG-1480 (CIV-11), BUG-1481 (CIV-15 a); CIV-5 is BUG-1478 (WP-M1) |
| 11 Audit, Space | WP-X1 (fix/w77-space) | BUG-1482 (SPC-3), 1483 (SPC-4), 1484 (SPC-5), 1485 (SPC-6), 1486 (SPC-11), 1487 (SPC-13), 1488 (SPC-14), 1489 (SPC-15), 1490 (SPC-16), 1491 (SPC-17) |
| 11 Audit, Tribe | WP-B1 (fix/w79-tribe) | BUG-1492 (TR-5), 1493 (TR-7), 1494 (TR-8), 1495 (TR-9), 1496 (TR-11) |
| 11 Audit, Net and Core | WP-N1 (fix/w80-net) | BUG-1497 (NET-1), 1498 (NET-2), 1499 (NET-3), 1500 (NET-4), 1501 (NET-5), 1502 (NET-8), 1503 (CORE-4), 1504 (CORE-8), 1505 (CORE-13), 1506 (CORE-14) |
| 11 Audit, Cell | WP-L1 (feat/w81-cell) | BUG-1507 (CELL-4), 1508 (CELL-6), 1509 (CELL-7), 1510 (CELL-8), 1511 (CELL-9), 1512 (CELL-11), 1513 (CELL-13), 1514 (CELL-14) |
| 11 Audit, Creature / UI / FX | WP-C1 (fix/w82-creature), WP-N1 | BUG-1515 (CR-13), 1516 (UIFX-4), 1517 (UIFX-6) (WP-C1); BUG-1518 (UIFX-8), 1519 (UIFX-10) (WP-N1) |
| 11 Audit, SporeKit | WP-N1 (fix/w80-net) | BUG-1520 (KIT-13), BUG-1521 (KIT-21) |
| 12 Tribe save_end | WP-B1 (fix/w79-tribe) | BUG-1522 |
| 12 Five Creature marks | WP-C1 (fix/w82-creature) | BUG-1523 (host's puppet hidden), 1524 (Cell portrait, no herbivore mouth), 1525 (100 % Cell progress, no legs DNA), 1526 (no partner HP, healing effect stays), 1527 (extinction count) |
| 12 Tearing report | WP-N1 (fix/w80-net) | BUG-1528 (Report a problem opens Explorer in full screen), BUG-1529 (minimized stops logged as hitches); BUG-0779 gets a History line |
| 12 Raw-path catch_prop jumps | WP-F1 (fix/w76-motion) | part of BUG-1235 |

## Item 1: Civ own money (PR #205)

**Goal.** Each player has its own Civ money (owner's friend's request): its cities' income is its own, its purchases are
paid from it, a newcomer gets a fair part, nothing is made or lost.

**Design (port of #205's idea; its `civ_wallet_logic.h` rules are sound).**
* The host keeps a wallet per guest (the host's own is its game's cCivilization +0x98). Income the host's game takes is
  split by the player cities each player owns (civ.cpp city owners); a shared city (the capital while a player has none)
  counts for every player with a wallet in equal parts; the city of a player without a wallet (away, older peer, not in
  Civ) stays with the host. Which AddMoney calls are city income comes from BUG-1236's return addresses (0xBF7366,
  0xBE1A26, 0xBE1C27, objdump-checked): one classifier for both (item 10).
* A guest's purchase the host's game makes (a vehicle, a city bought, a building, a superweapon via CMD_SUPER) is paid
  from that guest's wallet (`PayAs`: the host's money is swapped for the call and put back); refused with a notice when
  the wallet is short. The Ledger (guest report + host operations not yet acknowledged) is the host's view.
* A newcomer (never seen in this world) gets floor(host money / holders): money moves, none is made. A rejoin gets its
  kept wallet from player_store (`civ.wallet` world record by stable key).
* **Missing in #205, to add:** the old-peer guard (a guest below civ 1.2 has no wallet: shared money as today, its cities'
  income stays with the host); the wsync `civ_money` row (wsync.cpp r_/s_civ_money) does not write the host's money on a
  guest with a wallet (its money comes from MSG_CIV_WALLET), and BUG-1461 (the row copies money that an NPC nation's AI
  on the guest then spends while the host's caption pauses) is fixed in the same row; the superweapon; **turning it off**
  (a session without the setting on a world with stored wallets: the wallets' money goes back to the host's money once,
  the rows are cleared); **Civ -> Space** (RE first: what the game carries from Civ money into the Space start; shared
  Space: the wallets end and the host's game decides as today; Space Rivals: each budget starts from its wallet's share);
  CIV-5 (a guest elsewhere or the host away: income and purchase hooks gated on local play) since it is the same hooks.
* The guest's game shows its own wallet in the HUD (writes its cCivilization +0x98; guests already ignore AddMoney for the
  player civ).

**Wire / store.** civ 1.2, MSG 74 CIV_WALLET; `civ.wallet` world record. **Switches.** Setting `civ.own_money` (off: 0.7.16);
`civ.wallet_row` (the row skips a wallet guest; off: BUG-1461's behaviour), `civ.income_cause` (BUG-1236, renamed from
`shared.civ_income_cause` per the review's nit), `civ.local_play_gate` (CIV-5).

**Tests** (`coop_civ_own_money.py`, civ fixture): the join split (host 2000 -> 1000 / 1000 on both games' +0x98); 20 s of
idle income split by cities; a guest's land vehicle paid by the guest (host's money unchanged); a refused purchase (short
wallet, notice, no vehicle); a superweapon bought by the guest; a rejoin gets the kept wallet; the setting off on the next
session gives the money back to the host (sum before = sum after); Civ -> Space (`campaign` segment from the civ fixture
near the end) with the result the RE gives. Twins: setting off (one money), `SPORE_FIX_OFF=civ.wallet_row` (the guest's
money jumps back to the host's every 0.5 s), `civ.income_cause` (toast count on the guest, the review's recipe),
`civ_money_pause` for BUG-1461 (host caption, guest NPC AI, wsync_civ reads 0 at 3 s without the fix). Old peer: a
0.7.16.1 guest (lab `--old-guest`) plays shared money, no wallet traffic.

**Risks.** Money made on races (a purchase while a split is in flight: the Ledger's seq covers it, unit tests first); the
host's save holds only the host's money (the guests' in the store: the 0.7.16 rule); a shared capital's income split
equally may feel odd (by design, #205's rule). **Tickets.** New feature ticket (credit IltonBJSilva, PR #205), BUG-1236,
BUG-1461, CIV-5. **WP-M1** `feat/w68-wallet` (one agent; unit tests first, lanes 2-3 at night).

## Item 2: Own progress as a third DNA mode (PR #209)

**Goal.** A lobby setting with three values (owner, 2026-11-20): **shared** (one pool, `all.shared_dna` 1), **own wallet**
(0.7.16's default: every gain counts for everybody, each spends its own), **own progress** (each earns alone, its own DNA
and evolution bar; nobody evolves alone: the stage moves on when every bar is full and the vote agrees).

**Design (on our dna_own.h and player_store, not #209's side files and identity).**
* Own progress: no MSG_DNA total, no 1/N pace forwarding (`dna_route.h`), a guest's gain is applied on its own game; a
  guest's kill of a host NPC and a guest's social win are credited to the guest (the same Actor scope as item 3: one hook,
  WP-H1 owns it); each game pays its own species goal card. Cell: a guest's meal is acknowledged but not added to the
  host's food; the guest's cell-editor spend stays its own (wsync `cell_spent` row reads the mode).
* The host keeps every guest's values in player_store (`progress` world record: Cell food / spent, Creature DNA / bar /
  brain level, debt); a guest gets them back after every load of the world. The brain level is written through a named
  symbol (`SporeCoop::g_BrainLevel`, add to the .sym with evidence), never a raw address.
* Stage end. Cell: hook `Cell_EndSequence` 0xE72C80 (BUG-9266's finding: hold the click before the white fade, not after):
  the host's early click is kept and replayed outside the frame when every bar is full; a guest's click is its "I'm
  ready". Creature: the stage-end request (tag 0x64AA168) is refused until every bar is full, then BUG-1085's vote.
  Notices "your bar is full, waiting for X (64 %)" every 60 s.
* Cell levels together (BUG-9287): an own meal that would cross the next level line (every 100 food) while a partner is
  below is clipped; the rest is banked and released when every partner reached the line or the session ends. (The other
  option, populating each partner's area at its level, overflows the 511-entry buffer, BUG-0975: not now.)
* The setting: `all.shared_dna` keeps its ini key; values 0 own wallet (default), 1 shared, 2 own progress; the lobby
  shows one row "DNA and progress" with three names. A peer below creature 1.3 / cell 1.2 in the session: the host
  refuses value 2 at the start (a notice names the player) and a late joiner below it is told to update.
* Take from #209: the "waiting for partners X 64 %" notice, the h_Submit gate, the social-win credit, the g_BrainLevel
  name; not its CellStage_End double hook (BUG-1269 is ours) nor its storage. BUG-1261 nit 7 (a guest's TierBoneCleanup
  fades the host's bones for everybody) is checked under own progress.

**Wire / store.** creature 1.3, cell 1.2, MSG 76 PROGRESS; `progress` world record. **Switches.** `core.own_progress`
(value 2 read as 0 when off), `cell.end_click`, `cell.level_together`, `core.progress_restore`.

**Tests.** `coop_own_progress.py --part cell --first host|guest`: each game's food bar its own (host 300, guest 120); the
host full first: its click held, white share of the screenshot < 0.5, no vote; the guest fills, the vote starts by itself,
both in Creature. `--part creature`: own DNA and bar, the host's request refused, vote after both full. `level_together`:
the guest a level below, the host's meal clipped, banked, released. A rejoin restores the values. Twins: each switch off
(own progress -> the guest's food is the host's; `cell.end_click` off -> white 0.5+; `level_together` off -> levels apart).
Key frames: both bars, the notice.

**Risks.** A stage end held for good (a partner AFK): the vote's existing "move on without" path must still work (test
it). Brain level writes outside the editor. The CELL-4 spend paths (item 11) interact: same agent or after this WP.
**Tickets.** New feature ticket (credit #209), plus one per found bug. **WP-P1** Cell part and **WP-P2** Creature part:
`feat/w69-progress` (one agent; P1 then P2).

## Item 3: Own stage history (PR #207, #217 archetype)

**Goal.** Each player's consequence cards (Cell diet, Creature, Tribe, Civ, Space archetype) and the trait progress behind
them are its own; one player's choices never change another's.

**Design (on player_store, no identity or party files).**
* cPlayer holds the cards (+0x10FC..+0x110C) and Creature trait progress (+0x1278 / +0x127C); every game loads the host's.
  Hook `cPlayer::AddTraitProgress` 0xC75840 (ret 8, verified by #207 and by us): inside an Actor scope (npc.cpp around a
  guest's hit landing as the host's, social.cpp around a guest's social win) the amount goes to that guest's `history`
  record, not the host's cPlayer (BUG-9264's finding: today a guest's kills raise the host's combat).
* At a stage end the host decides each guest's card: Cell from the guest's own game's diet decision; Creature by the
  game's rule on the guest's progress; Tribe and Civ the host's (one shared tribe and nation; question 3); Space the
  archetype of the guest's four cards. MSG 75 sends a guest its record; its game writes it into its cPlayer; the wsync
  "stages" row leaves a guest with its own history alone.
* Space (#217 b): the guest's own archetype tools via `SpaceGame_SetArchetype(game, false, false)` 0x1005C20 once per
  world or new card (twice gives a duplicate tool: gate it).
* A new game started after Cell has no cards at all on the host (#218's RE note): a guest's history then starts empty too.
* Follows `creature.mode` own (host mode: one shared history as today). The host's save keeps the host's cards only.

**Wire / store.** session 1.3, MSG 75; `history` world record. **Switches.** `core.own_history`, `core.trait_credit`,
`space.own_archetype`. **Tests** (`coop_own_cards.py`): the guest kills an animal of another species
(`coop.npc hit`, a real lethal hit): the guest's combat progress rises, the host's not; the guest's Creature card differs
from the host's after the stage end (cPlayer::GetConsequenceTrait on each game), also after a rejoin and a host reload; a
Space world: the guest's archetype index (+0x58) and its tools. Twins: each switch off (host's card / host's progress).
**Risks.** The cards decide later gameplay (powers): a wrong write is visible at once; the Space archetype path near the
captain screen (BUG-1111). **Tickets.** Feature ticket (credit #207, #217), BUG-9264's finding as our ticket. **WP-H1**
`feat/w70-history` (one agent; owns the Actor scope hook used by WP-P1/P2).

## Item 4: Revive a downed player and progress debt (PR #214, Creature part)

**Goal.** A lethal hit on a player's creature while a partner is in the world downs it instead of killing it; a partner
revives it; otherwise it dies the game's way. The cost is charged to the downed player, not the host.

**Design (port #214's rule header and K_DOWN / K_RESULT / K_LINES flow onto teamplay.cpp).**
* Down: 1 hp, held (no moves, no hits taken), 60 s window; a partner standing within 6 m for 1.5 s revives it at 40 %
  health; the window running out or "hold G" gives up: the game's death, respawn at the shared nest (not #208's own nest).
  Everyone down for 2 s: a party wipe (each dies the game's way). No world save while someone is down (savestrip hold).
* Cost: own wallet: the downed player's wallet pays N % of its DNA; own progress: a debt on its bar (stored in the
  `progress` record, paid from the next gains); shared: the shared pool. Never 10 % of the host's total (#214's rule).
* The host decides (it holds the NPCs' damage); the tags "DOWN 55 s: name" and arrows use our name tags and partner
  arrows. Pack rescue (40 % once a minute) stays as it is unless question 4 says otherwise.
* Old peers: an older guest never sends K_DOWN (dies as before); the host never downs a guest below creature 1.3.

**Wire / store.** creature 1.3, MSG 77 RESCUE; debt in `progress`. **Switches.** Setting `creature.revive`; fix
`creature.revive_hold` (the 1 hp hold), `creature.revive_debt`. **Tests** (`coop_creature_revive.py`, creature fixture):
the guest downed by a real NPC hit: the game's dead flag false on both games, health 1, the tag on the host's screen
(key frame); the host walks within 6 m: after 1.5 s health 40 % on both; timeout: dead flag, respawn at the nest, debt
charged (own wallet and own progress variants); party wipe; no save while down (a save request waits). Twins: setting
off (the hit kills), `revive_hold` off (the creature dies under the hold). **Risks.** The game's other damage paths
(drowning, falls, epic stomps, a pausing caption: #214's `kill_retries`) bypass the hold: each is a test row; a held
creature in a cinematic (BUG-1173). **Tickets.** Feature ticket (credit #214). **WP-R1** `feat/w71-revive` (one agent;
after WP-P1/P2 merged for the debt).

## Item 5: Difficulty for 3-4 players

**Goal.** With 3-4 players the NPCs fall too fast; a lobby setting scales the challenge.

**Design (RE first).** The game reads its difficulty tunings through `DifficultyTuning_GetFloat` (in our .sym, used by
space_trade.cpp). RE: which tuning keys each stage reads for NPC health, NPC damage, raid and epic strength (Creature,
Tribe, Civ, Space); then the host (authoritative for NPCs) answers those keys multiplied by the setting's factor; guests
mirror the result. Where a stage has no tuning key, scale in our own paths (the guest hits landed by the host in npc.cpp,
creature_hp.cpp). Setting `all.party_difficulty`: 100 % (today), 125, 150, 200, or "auto" (+25 % per player over two).

**Switches.** The setting (100 % = today); fix `core.party_tuning`. **Tests** (`coop_party_difficulty.py`, 3 games via
coop_multi's extra instance): an NPC's hit on a player and the hits to kill one NPC measured at 100 % and 150 % (the game's
own health fields on both games); twin: the fix off gives the same numbers at 150 %. **Risks.** Tunings that also change
the economy or the AI's choices; a key read once at load (then the setting applies after a reload). **Tickets.** Feature
ticket. **WP-D1** `feat/w72-difficulty` (one agent; RE report in docs/re first, then the code).

## Item 6: Shared enemies outside Rivals

**Space (BUG-1288, agy commit 3b126046).** Verdict "merge after fixes": the wire part is clean (lock, guards, fuzzed
decoders). Rework on our side:
1. The `!in_space()` reset and the session-end `in_space()` guard behind `fleet.leader` (off must be 0.7.16 exactly; today
   mirrors can reach a guest's save, BUG-0522 class).
2. No promotion of mirrors for now: on a handover the followers drop their mirrors and mirror the new leader (ships pop
   once); never promote the departing player's escort (type 3); nothing changes in a 2-player session.
3. A sticky leader picked and announced by the host per place (fixes the guests' disagreeing election in mixed versions
   and the re-leadership on every zoom).
4. Lab test: host in system A, two guests in system B (coop_multi extra instance): the guests' NPC ship ids and positions
   match, a follower's shot damages the leader's ship; a 2-player check that the host leaving leaves no ghost or immortal
   ships; twin `SPORE_FIX_OFF=fleet.leader`. Record the findings in docs/re/space.md.
**WP-S1** `fix/w73-fleet` (one agent; space 1.3, MSG 78).

**Adventures.** Each game plays its own copy of an adventure (docs/re/adventure.md#adventure-two-captains): the enemies
are each game's own. RE first: the adventure's creature list and act spawns, whether the Creature-stage NPC proxy path
(npc.cpp) works in kScenario with the herd rule of #adventure-puppet-herd. Then host-authoritative enemies mirrored on
the guest behind an off-by-default switch `adventure.shared_npcs`; test `coop_adventure_tutorial.py` act 1 (Skex and
Thorns): the same enemies, a guest's hit lands on the host's enemy, a kill seen on both, key frames. Scope per question 6.
**WP-A1** `feat/w83-advnpc` (one agent; RE report first).

## Item 7: Per-player captain (#217 a, BUG-1262, BUG-1279, BUG-1237)

Starting point: agy commit 4e7490e6 and its review (out/research/agy-review-creations.md).
* **BUG-1279** (captain property lists of the host's captain on a guest; "merge after fixes"): verify
  `CaptainProps_Save` (still `likely`, arg 3 unknown) and its callers' use of a false return (0xF16D60, 0x5804A4) and
  record it in the .sym; review the instance-collision block (a guest's own creation sharing the host captain's id);
  twin test: a host Space save whose captain has items, the guest's captain screen shows the item limits, FAIL off.
* **BUG-1262** (the partner's captain look in an adventure; "redo"): switch the puppet only on a real bake (never after a
  timeout: BUG-1321's white model); rebuild the puppet in the frame, not from an outside-frame task (BUG-1254); kScenario
  only (no captain traffic in Space); gate the receive path; give the 0x7A copy its property list or show no adventure
  path reads it; first show an adventure captain can be read from a package (the review's #9); twin: the puppet's bem
  hash equals the partner's captain.
* **BUG-1237** (own creature plain green for ~2 s; "merge after fixes"): `key_known` + `species_by_key` guard, retry
  until the swap; twin: `model_baked` == 1 at the SetSpecies moment or a key-frame colour check.
* **#217 a** own captain species on the guest's own game in Space: `cEmpire_SetCaptainKey` (+0xC4), put back for every
  save write (the species-line rule), through BUG-1111's guard path; the 0.7.16 decision "captain and crew later" ends here.
* creations 1.2 for the new MSG_CREATION kinds (review nit 13). **WP-K1** `feat/w74-captain` (one agent).

## Item 8: Share only the played planet (BUG-1434 full)

0.7.16.1 shipped the safety part (a copy with own progress is never replaced; the host is told whose copy it plays). The
full fix: a share sends only the played planet's `.spo` plus the galaxy's own files, so copies hold no old planets. RE
first: GGEUserData.dat lists the saved games (a missing `.spo` shows "Bad or Missing Saved Game"): rewrite it for the copy
or keep the entries valid. Plus a "continue the last co-op world" entry for a new host after a role swap (known-issues
BUG-1434 next steps). world 1.1: an old guest gets the whole galaxy as before. Tests: `copy_slot_planets` (host slot with
three planets; the guest's copy has one, its galaxy menu loads it, key frame of the menu; a role swap starts the session's
world); twin `SPORE_FIX_OFF=world.share_planet`. Also: **BUG-1436** dump analysis (the guest's crash in the galaxy-menu
sim reset after the mod's quit, Player_SetProperty+0x14D): read the player's .dmp with REA, a ticket update; a fix if it
points at our quit path (h_SimMsg). **WP-W1** `fix/w75-share` (one agent).

## Item 9: Cell, small owner items (#210, #211)

* **#210 small part**: a guest with no cells in My Creations gets the three diet premades (E219E5D2 / 252A132F / 0C176D89)
  in the "My cell" picker; take #210's RE (diet flags of 44 cell models, `Cell_GetConsequenceTrait` 0xE5C9D0). Test: an
  empty test profile picks a carnivore premade, both games show it. Full own start: Rivals.
* **#211**: players' My Creations cells as NPC species (about 17 % of native spawns), one chain with our kin filter hook
  (GetModelKeyForCellResource 0xE65640, BUG-1257 / 1333); off-by-default setting `cell.lineage_cells`; a guest's cells are
  sent only with its consent (a co-op window check box); creations 1.2 kind 12 (old peers drop it); the cell pool is not
  saved (#211's RE: check by a save parse anyway). Test with cells in the test profiles, a save parse, a twin, key frames.
  Take `cCellGFX::PreloadCreature` 0xE64980 (its group global on GOG 2024 is *(0x015A7D0C)).
**WP-L1 / WP-L2** `feat/w81-cell` (one agent, after WP-P1; also the Cell audit items of item 11).

## Item 10: Tickets planned for 0.7.17

`git grep "0\.7\.17" rel/0.7.16.1 -- docs/tickets` names BUG-1360, BUG-1434, BUG-1436, BUG-1461; the owner's list adds the
agy tickets. Verdicts from the reviews are the starting point; no commit of fix/agy-open-bugs is merged as is.

| Ticket | Status | What | Review verdict | WP |
|---|---|---|---|---|
| BUG-1434 | open (safety part in 0.7.16.1) | share only the played planet | - | WP-W1 |
| BUG-1436 | open | guest crash in the galaxy-menu sim reset | dump first | WP-W1 |
| BUG-1461 | open | Civ: civ_money row during the host's caption pause | - | WP-M1 |
| BUG-1288 | open | Space fleets in non-host systems | merge after fixes (item 6) | WP-S1 |
| BUG-1279 | open | captain property lists on a guest | merge after fixes | WP-K1 |
| BUG-1262 | open | partner's captain look in an adventure | redo | WP-K1 |
| BUG-1237 | open | own creature green for ~2 s | merge after fixes | WP-K1 |
| BUG-1236 | open | "X earned N money (shared)" every 6 s | merge after fixes (twin missing) | WP-M1 |
| BUG-1234 | open | proxy 16-18 m off after a caption thaw | merge after fixes: drop `cont - earliest > kLateGapMax`, raise kLateGapMax over the Civ 1 s quiet period (3 s) | WP-F1 |
| BUG-1235 | open | Tribe member mirrors jump at 30 fps | merge after the lab (body_trail_tribe*, tribe_walk_mobile, tribe_coop) | WP-F1 |
| BUG-1360 | open | walked NPC proxy plays the idle for 1-6 s | merge after the lab; the mound `place` needs npc_nest_backoff, mound_view, repeated twins | WP-F1 |
| BUG-1244 | open | the host's sing not on its puppet | merge as is (test only); one `SPORE_FIX_OFF=creature.avatar_bhv_anims` run for the ticket | WP-F1 |
| BUG-1261 | open | bones the host lacks on the guest | merge after fixes: twin lab test both halves, the kGone trust (a host load / editor return among bones), the old-peer gate | WP-C1 |

Anything 0.7.16.1 leaves open (BUG-1450, BUG-1457, a failed twin) rolls into 0.7.17 with its ticket. Open tickets not
planned (game bugs without a fix idea yet): BUG-1231, 1256, 1347, 1388, 1394, 1404, 1456, 1458 (question 7). Test-only
tickets BUG-1358, 1408, 1410, 1448, 1452, 1454, 1459, 1462..1466: **WP-T1** `fix/w84-tests` (cheaper and steadier lab
nights; no game code).

**WP-F1** `fix/w76-motion` (one agent): BUG-1234, 1235 with the raw-path catch_prop jumps (item 12), 1360, 1244, and the
npc_nest_backoff precondition (item 12). Each fix keeps its agy switch name (`drive.lead_gap`, `drive.lead_cap`,
`npc.walk_gait`) and gets its twin; a twin that passes once is run again (docs/06-testing.md).

## Item 11: The rest of the REAL items of the external audit

0.7.16.1 took SPC-2, SPC-8, CIV-1, CIV-6, CIV-7, CORE-2, CORE-3, CORE-9, CORE-11, NET-12, KIT-25, UIFX-3, CR-2, TR-1,
TR-6, CELL-1, CELL-3, CELL-5 (BUG-1414..1433). The REAL rows left (each row of the verify tables has the lines, the
failure and the fix size); every one gets a ticket naming the audit id, a switch and a twin where it changes behaviour.
REAL-THEORETICAL items stay unscheduled unless a WP touches the same lines (then fix on the way, say so in the report).

| Area | Items (fix size) | WP |
|---|---|---|
| Civ | CIV-5 guest hooks ignore "elsewhere" (~10 lines; with the wallet), CIV-10 a city's specialty answer lost (2), CIV-11 CMD_SPACE_BUILD held in an editor (~3), CIV-15 a: the orphan sweep kills a candidate waiting for its design (~3) | WP-M1 (CIV-5), WP-V1 |
| Space | SPC-3 adopt moves a Rivals budget nobody sends, SPC-4 Rivals money traffic on a guest's own Space save (5-8), SPC-5 restore_star without rebuild_near (3, BUG-1144 signature), SPC-6 sale payment after a world change (10-15), SPC-11 contract against another empire's colony (~10), SPC-13 false raid notices on own-star mirrors (~3), SPC-14 c restore applied on a guest's own save (2), SPC-15 NaN terraform from a modified peer (2), SPC-16 a disabled ship jumps away (3), SPC-17 offered spice stuck (~6) | WP-X1 |
| Tribe | TR-5 a village conquered while the host was away does not count (goal_target), TR-7 a mirror hangs in the air, TR-8 gather order reads two masks as one u64 (1 line), TR-9 part 2 a guest elsewhere paused by the host's game-over caption, TR-11 a host theme keeps playing after the session | WP-B1 |
| Net and session | NET-1 pointer into a temporary vector (tiny), NET-2 a stale rejoin slot token, NET-3 store after_save, NET-4 `coop.store adopt` cannot find world-record players, NET-5 raw `puppet_of` without liveness (3 sites), NET-8 settings of one world written into another during an editor or load | WP-N1 |
| Core | CORE-4 host-mode Creature editor opens before the vote checks, CORE-8 paint patch on an `FE N` run, CORE-13 a failed share leaves the ground reload due, CORE-14 a guest's spend owed applied in another world | WP-N1 |
| Cell | CELL-4 a solo guest still sends spend and mate, CELL-6 partners' poison suppressed during the host's knockout, CELL-7 flush hands tiles over while scaling, CELL-8 a shadow tile counts as populated, CELL-9 a breaking cell can die twice, CELL-11 partner positions in the wrong scale for revive / regen, CELL-13 own poison clouds hurt the owner (friendly fire), CELL-14 a plant's shrink not sent | WP-L1 (after WP-P1) |
| Creature, UI and FX | CR-13 the social meter hidden a frame after the finish, UIFX-4 mirror FX dropped including stops, UIFX-6 pool-3 one-shots not relayed without the host's puppet, UIFX-8 give button over the creation label with long names, UIFX-10 peer names not cleared between sessions | WP-C1 (CR-13, UIFX-4, 6), WP-N1 (UIFX-8, 10) |
| SporeKit diagnostics | KIT-13 off-stack check after the 64-site cap, KIT-21 the report zip built twice in one second | WP-N1 |

**WP-V1** `fix/w78-civ`, **WP-X1** `fix/w77-space`, **WP-B1** `fix/w79-tribe`, **WP-N1** `fix/w80-net`,
**WP-C1** `fix/w82-creature` (one agent each).

## Item 12: Open leads from 0.7.16

* **Tribe save_end g_took_t** (tribe.cpp `save_end`): puts the taken entries back after a save if the container is alive,
  but not checking the entry itself, which may have been destroyed during the save window: CIV-1's bug (BUG-1416) in
  Tribe. Ticket, a test like `civ_save_took` (a member removed by the host while the guest saves), twin. WP-B1.
* **BUG-1436** dump analysis: item 8.
* **The five Creature marks** of the world-switch report (section 5): the host's puppet hidden at first; the HUD portrait
  still the Cell creature and herbivores without a herbivore mouth; 100 % Cell progress at once and no +50 DNA for legs;
  no partner HP and a healing effect that stays; the extinction count up one then back to zero. `tickets.py find` each,
  one ticket each, then fix-player-report. WP-C1.
* **Two tearing tickets** (out/research/dc-report-tearing-20261116.md; never copy its screenshots or addresses): "Report a
  problem in full screen opens Explorer, which minimizes Spore" (do not open Explorer in exclusive full screen, or only
  from a button) and "a stop while minimized or unfocused is logged as a hitch" (tag the perf and keepalive lines with
  IsIconic / foreground, keep such gaps out of min fps). Add the History line to BUG-0779. WP-N1.
* **npc_nest_backoff**: an unmet precondition (no nest animal backing off in time) gives INCONCLUSIVE, not FAIL (the test
  helper's inconclusive path, as civ_planner_miss). WP-F1.
* **Raw-path catch_prop jumps** (BUG-1235): the raw path's catch-up of a late member mirror jumps; measured with the
  BUG-1235 twin runs. WP-F1.
* #219's question: does a shared-nation guest get its own spaceship editor at Civ -> Space? Lab check on rel/0.7.17; if
  not, #219's recipe (level 4 + GameCiv_ShowPlanetWon + PlanetWonPause on the guest's game after the vote). WP-M1 (it
  runs the Civ -> Space segment anyway).

## Order and branches

Base: `rel/0.7.17` from the v0.7.16.1 release commit; worktrees `../SporeCoop-wt/<branch>`; branches from rel/0.7.17
after WP-0; merges into rel/0.7.17 by the merge-batch skill; main gets 0.7.17 at the release.

1. **WP-0** (the coordinator, one commit): the reservation table above, out/RULES-0.7.17.md (lanes, rules), tickets
   opened per item so numbers do not collide.
2. **Wave 1, in parallel (8 agents):** w68-wallet (M1), w69-progress (P1, P2), w70-history (H1), w73-fleet (S1),
   w75-share (W1), w76-motion (F1), w78-civ (V1), w80-net (N1). Their files barely overlap; w69 and w70 share the Actor
   scope hook (w70 owns it, w69 calls it: w70 merges first, or the hook's header lands in WP-0).
3. **Wave 2, after the wave-1 merges they depend on:** w71-revive (after w69), w72-difficulty (RE first, can start in
   wave 1 as RE only), w74-captain, w77-space (after w73: squadron.cpp), w79-tribe, w81-cell (after w69: cellsync),
   w82-creature, w83-advnpc (RE first, can start in wave 1 as RE only), w84-tests.
4. **Nights (owner's word):** per WP its tests and twins on 4 lanes; regressions near each change (named in each item).
5. **Final check** (the release rule, CPU idle first): smoke + full + multi + every twin of 0.7.17, key frames reviewed
   in every new test (visual checks), old-peer runs against 0.7.16.1 for every family bump.

Every package: a ticket per fix, a switch per behaviour (FAIL twin), tests on two games (three for the 3-4 player items)
that check the game's own fields on the other machine and a key frame, a save parse where a field could reach a save,
knowledge in docs/re and the .sym (with confidence words), CHANGELOG lines in the report.

## Ending

* CHANGELOG and release notes credit IltonBJSilva for #205, #207, #209, #210, #211, #214 and #217 (ideas and RE).
* When 0.7.17 ships: close PRs #204 to #220 with thanks and the credit, one comment per PR group (as the replies on #204,
  #210 and #213 did), naming what was ported where and what went to Rivals 0.8. The text is shown to the owner first; it
  is posted after the owner's "yes".

## Questions for the owner

1. **Civ own money default**: off (as #205, the friends' request as an option) or on for new Civ worlds?
2. **DNA and progress default**: keep own wallet (0.7.16's default) and offer own progress as the third value (proposed),
   or make own progress the default?
3. **Stage history in Tribe and Civ**: the host's card for everybody (one shared tribe and nation, proposed), or each
   player's card from its own members' and cities' actions?
4. **Revive and pack rescue**: keep pack rescue (40 % once a minute) as the first line and revive after it (proposed), or
   replace pack rescue by revive? Cost of a revive timeout: 10 % of the downed player's own DNA (proposed)?
5. **Difficulty**: default 100 % (today's balance, proposed) or "auto" (+25 % per player over two)?
6. **Shared enemies in Adventures**: each game plays its own copy of an adventure today, so this is new RE. In 0.7.17 as an
   off-by-default option if the RE shows the Creature proxies work there (proposed), or move it to 0.8?
7. **#210 diet premades and #211 old cells as NPCs** (your decision of 2026-11-20): still in 0.7.17? And the open game
   tickets without a fix idea (BUG-1231, 1256, 1347, 1388, 1394, 1404, 1456, 1458): leave open, or add any to 0.7.17?
