# SporeCoop 0.7.12 plan (draft 2026-11-07): a feature update

0.7.12 is a feature release built from the Discord suggestions of 2026-10-31 .. 11-04 (forum "suggestions"),
plus what 0.7.11 leaves open. It starts after 0.7.11 is released. The same rules as docs/plan-0.7.11.md apply:
every ticket of this plan closes in this release; every feature gets an automated test on game instances that
checks what the other player sees; a feature keeps the old behaviour behind its switch.

**Status: approved by the owner (2026-11-08).** Decisions: every setting of the F3 list as proposed, with the
proposed defaults and scopes (friendly fire included, off by default); the combined report as proposed, plus a
host button "Ask the players for their reports" usable at any time; the player types the name (no Steam / GOG
profile name).

## Sources (Discord, forum "suggestions")

| Post | Who | Asks for | Package |
|---|---|---|---|
| Arrow or something to indicate which player is where | Sterben, M0ng0r | an arrow with name / colour to each partner (Cell: players lose each other) | F2 |
| We can see each other on map | M0ng0r | partners on the minimap in their own colour (random or chosen) | F2 |
| Shared healing in cell stage should be optional | M0ng0r (3 reactions) | turn the Cell regeneration near partners off | F3 |
| create unique switches to every stage | Arsgallik | switches per stage (PvP in Cell, one civilization or separate) | F3 (PvP), 0.8 Rivals |
| Restyle coop menu | ефим | a co-op window in steps (name, Host / Join, lobby), session settings in the lobby, the bug report in the pause menu | F1 |
| Comfortable bug reports | ефим | the host's report holds every player's report | F4 |
| Same creature for everyone | Sterben | a setting: every player plays the host's creature, only the host edits it | F5 |
| Loading pages | Lucas4001MC | no evolution buttons for a guest who cannot change the creature | F5 |
| If one is evolving creature in editor the other must too | Sterben | done since 0.7.4 (the editor vote); answer in the thread | - |

## Work packages

| Package | What | Notes |
|---|---|---|
| **F1 Co-op window 2.0** | The window in steps: (1) first start: the player types the name (owner 2026-11-08: no Steam / GOG profile name), (2) Host or Join, (3a) Host: port, Host, (3b) Join: games found on the network, an address field that takes host names (`sporecoop.local`, the port 47810 when none is typed), (4) Lobby: players (name, colour, stage, ping, ready), the host's address, Ready, the session settings (F3; the host edits them, guests see them), Send my world (host), Leave / Close session. In game (F9): players, chat, the settings (read-only for guests, the host's changes allowed for the settings marked "live"), Report a problem. "Report a problem" also in the game's pause menu. | Spore's own UI style (the current window toolkit, docs/08-ui.md). Test: ui tests drive every step by real clicks. |
| **F2 Find each other** | (a) An arrow at the screen edge for every partner off screen, in the player's colour, with the name and the distance; on screen the name tag stays. Every stage with an avatar view: Cell, Creature, Tribe (the chief), Civ (optional: the partner's capital), Space (the partner's ship). Off in Rivals mode (and switchable, F3). (b) The partners on the minimap of Creature and Tribe (and the Cell's, if it has one) as triangles in their colour. (c) A player colour: chosen in the lobby, else assigned (4 distinct colours), shown in the lobby, the arrows, the map and the chat. | Test: a guest at 80 m off screen gets an arrow pointing at the host's real position (checked in the guest's game), no arrow in Rivals. |
| **F3 Session settings** | One settings framework: the host's choice is sent to every guest when it joins and when it changes; stored with the host's world (the next session keeps it); shown in the lobby and F9. Each existing co-op mechanic gets a setting (list below) instead of an ini line or nothing. The ini keeps the defaults for a new world. | Every setting has a test with it on and off. The lobby shows which settings differ from the defaults. |
| **F4 One report for the session** | The host's "Report a problem" asks every guest for its report, and the host has its own button "Ask the players for their reports" (lobby and F9, any time, without making the host's own report; owner 2026-11-08); each guest sees "The host asks for your report: send / don't send"; the reports go to the host over the session connection; the host gets one `SporeCoop-session-report-<time>.zip` with `Host-<name>.zip` and `Guest-<name>.zip` inside. A guest can still make its own report. | Safety rules below. Test: a 4-player session, every report arrives, a guest that says no is listed as "not sent", a too big or malformed report is refused. |
| **F5 One creature for everyone** | A session setting for Cell and Creature: "Each player's own creature" / "Everybody plays the host's creature". In the second mode every guest plays the host's species, only the host evolves it (the guest's editor and evolution buttons are hidden, the vote is the host's alone), and an evolution changes every player's creature at once. | Builds on BUG-0345 / BUG-0638 (the world's species). Test: the host evolves, every guest's avatar changes to the new creature, no guest editor opens. |
| **B1 Left from 0.7.11** | The open bugs 0.7.11 leaves: a guest's ship copy lags on the host in fast turns (sync-check p<guest>), the host's chief member jumping on the guest's mirror, the conquest cinematic sometimes missing in tribe_conquer_orders, a fruit gather order on the guest only, a knocked-out carnivore swallowing meat, NPC store spice prices per game, a latent tribe spawn marker; missing tests: the duet, terraforming, an NPC mission reward, a guest's colony building | BUG-0758, 0759, 0761, 0744, 0746, 0747, 0741, 0748, 0749, 0750, 0751 |
| **B2 Found in 0.7.12** | The terrain sculpt and paint tools not sent to the partner; a guest's colony building not built while the host's ship is on another planet | BUG-0763, 0766 |

## F3: settings (approved 2026-11-08)

Scope: **session** = the host sets it in the lobby before the start, kept with the world; **live** = the host can
change it during play (F9).

| Group | Setting | Default (proposed) | Scope | Today |
|---|---|---|---|---|
| All stages | Partner arrows | on (off in Rivals) | live | new (F2) |
| All stages | Partners on the minimap | on | live | new (F2) |
| All stages | Name tags over players | on | live | always on |
| All stages | Friendly fire: players can hurt each other (Cell, Creature, Tribe, Civ) | off | session | not possible; Space has rivals |
| All stages | Progress pace: DNA / food at the single-player pace, or every player's gain counts whole (faster) | single-player | session | ini `dna_pace` |
| All stages | NPC animals treat partners like the host's creature | on | session | ini `npcs_target_partners` |
| All stages | Players who can join (2-4), joining after the start allowed | 4, yes | live | fixed |
| Cell | Knock-out instead of death next to a partner (revive within 10 s) | on | session | always on |
| Cell | Regeneration near a partner ("shared healing") | on | live | always on |
| Cell | Respawn next to the partner as a small cell | on | session | always on |
| Creature | Pack rescue (a deadly hit near a partner leaves 40 % health) | on | session | ini `teamplay` (all Creature bonuses at once) |
| Creature | Partners heal each other nearby | on | live | as above |
| Creature | Joint hunt bonus (+25 % damage) | on | session | as above |
| Cell, Creature | Creature mode: each player's own / everybody the host's (F5) | each own | session | the world's species by default, own one in F9 |
| Tribe | Pincer bonus (+50 %), chief aura (+20 %), duet, epic creature event | on (each) | session | always on (log: "tribe co-op: ... on") |
| Civilization | Roles (military, economic, religious) | on | session | always on |
| Civilization | Joint siege bonus (+50 %) | on | session | always on |
| Civilization | Superweapons need a vote | on | session | always on |
| Space | A destroyed ship next to a partner is only disabled for 20 s | on | session | always on |
| Space | Features: trade, contracts, shared diplomacy, allies, finale together, rivals mode | as today | session | F9 Space page |

Not proposed: anything that changes the game's own difficulty or rules for a single player.

## F4: safety of the combined report

The host's PC receives files made by other people's PCs. Rules:
* Consent: a guest sends only after it clicks "send"; the host sees who sent and who did not.
* Size: at most 25 MB per player (a report is 1-5 MB); the transfer stops at the limit.
* Content: the guest's report is the same zip its own "Report a problem" makes; the host checks that it is a zip
  with only the known file names (logs, summary, screenshot, crash text and dump) and plain names (no `..`, no
  drive letters, no other folders than the six the report has always had, one level: logs/, previous/, crashes/,
  reports/, shots/, settings/; decided 2026-11-08, docs/REPORTING.md#reporting-session), never unpacks it and never runs anything from it; it is stored inside the session zip
  as received.
* Privacy: the reports already mask the Windows user name and paths (log.cpp mask_paths); the guest's IP address
  is not written into the host's file. The guest's dialog says what is sent.
* A malformed or too big report is refused and listed as "refused" (no crash, no partial file).

## Order

1. F3 framework first (settings sent host -> guests, stored with the world), with two settings wired (Cell
   regeneration, partner arrows) to prove it.
2. Then in parallel (at most 3 lab agents): F1 (window, uses F3), F2 (arrows, map, colours), F4 (report).
3. F5 and the rest of the F3 settings.
4. Final check: every new test, ui tests by real clicks, play_* of all stages with the default settings and with
   the non-default ones, campaign_*, a 4-player session; then the release.
5. After the release: answer the suggestion posts that were implemented (with the owner's yes on each text).
