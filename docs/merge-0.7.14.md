
## Decisions taken in the merge (2026-11-15)

Wire and ids (every peer of this build is the same build, so layouts may change):

* `PlayerFlags` became `uint16_t` on the wire: ours `PF_DOWNED = 128`, upstream's `PF_RESYNC` moved from 128 to 256.
* `PlayerState` tail: upstream's `body` right after `agl`, then ours (`captain_scale`, `energy`, optional `editor`,
  optional `gate`); tests/net_tests.cpp adjusted to the new offsets.
* Message ids: ours 65-69 collided with upstream's new 65-69; ours moved to MSG_CAMPAIGN_RULES 88, MSG_CELL_COMBAT 89,
  MSG_CIV_TURRETS 90, MSG_CIV_ECONOMY 91, MSG_CIV_INFLUENCE 92.
* player_rules kinds: ours K_SUMMON / K_RECRUIT (3, 4) collided with upstream's K_UFO / K_HOLD: ours are 11 and 12.
* creations bundle kinds: our kPortrait (6) collided with upstream's design kinds 6..8: kPortrait is 9.

Same feature on both sides (one path kept):

* Summon Flock of a guest: our guest pack (BUG-9129) wins; upstream's creature.flock_relay (BUG-0736) only runs when the
  guest pack is off.
* Partner creature size: ours (captain_scale, BUG-9113 / BUG-9206) drives it; upstream's creature.puppet_scale
  (BUG-0733) is kept but off by default.
* Game saved box: ours BUG-9057 / BUG-9101 folded into upstream's world.saved_box_only (BUG-0846), world.saved_box_cursor
  (BUG-0804) and world.saved_box_offscreen (BUG-0806); our hover before the click stays.
* Download retries: upstream's (BUG-0755) replace ours; our Lineages release feed parsing stays.
* Folder move retry: one `slotfs::move_staged` (upstream BUG-0848, 1.5 s) honouring both switches; ours (BUG-9028)
  lost its own COOP_FIX (the name world.staged_move was taken twice).
* creature.bar_spend: upstream's BUG-0630 switch also covers our BUG-9092 progress sync (one switch).
* Share collection: upstream's slotfs::collect (BUG-0847, complete reads) replaces our world.cpp collect; it skips our
  `.duo-tmp-` side-file temp copies.
* Virtual cursor: upstream's BUG-0804 (`vcursor_state(x, y, bool& virt)`, release after 2 frames) replaces our BUG-9101.
* uninstall.bat and validate.h: upstream's (supersets of ours).
* Rivals budgets: keyed by our identity key (BUG-9198) with upstream's newcomer rule (BUG-1029).
* Fleet: our leader model (BUG-9197) with upstream's shots (BUG-0677), friendly-fire and War rules (BUG-0420, BUG-1032).
* Session limit: our seat_limit and upstream's max_players / join_closed both apply (the smaller counts).
* The Windows toolchain has no `<condition_variable>`: sporekit/console.cpp polls instead.

More ids moved (every peer runs this build):

* civ commands: upstream keeps 5..8 (space build, BUYCITY, CAPTURE, specialty); ours CMD_WALL_EDIT 9, CMD_WALL_ACK 10,
  CMD_CITY_PURCHASE 11. The Civ vehicle snapshot is ours (45 bytes) plus upstream's top-speed tail (BUG-0787).
* tribe commands: ours keep their numbers; upstream's CMD_CONQUER (4) is 11.
* host_world.h `msg_class` (default deny, BUG-0420 class) now names every Lineages message: Session (identity, library,
  campaign rules, first creature, migration, progress debt, launch, rescue, outfit, adventure), HostWorld (Tribe and
  Civ rows, drama, Cell combat), Module (adventure actors).

The big files (merged by helpers, checked by the build):

* coop_ui.cpp: our one-page Co-op window (BUG-9117) stays; upstream's step window (0.7.12 F1, BUG-0765 name step) and
  its "More >" LAN paging are dropped (core.ui_name_first is gone). Kept from upstream inside our 760x600 window: player
  colours (F2), the session settings page (F3), the report round (F4), Evolve Now (BUG-0516), one-creature mode (F5),
  give-row sizing (BUG-1062), the Space picker (BUG-1050), the pause menu's "Report a problem". The new widgets'
  places need a screenshot check.
* coop.cpp: our evolution request (tag 0x64AA168) is marked by a call guard (core.lineages_evo_req) so upstream's stage
  end rule (BUG-1085) does not take our join, migration and mating editors for a stage end. tribe.graduation_save is
  one switch (ours BUG-9050 and upstream BUG-0849 are the same fix). One `coop.adventure` command with both sides'
  subcommands. Upstream's partner arrows are off while our chevrons (ui.partner_arrows) are on; F2 colours replace our
  gold / blue tribe tags.
* tribe.cpp: upstream's BUG-0612 / BUG-0610 / BUG-0844 replace our identical backports (BUG-9090 / 9091 / 9051, same
  switch names). Our BUG-9183 PlayCinematic log no longer hooks (world.cpp's BUG-0626 owns that function).
* civ.cpp: one hook of cCity::BuyByCiv (ours, BUG-9082); upstream's h_CityBuy (BUG-0877) runs through it. A partner's
  away capture (BUG-0878) goes to that partner's founder nation (untested).
* npc.cpp / cellsync.cpp: one hook of the cell damage getter (ours scales, upstream's BUG-0714 forced damage first).
  Cell meals now go through upstream's in-step food queue (BUG-0633), our earned_food inside it.

Was open (owner decision): upstream's Creature stage end (BUG-1085) is a host vote and a guest's own request is refused;
ours made every Creature editor personal. Which rule for the stage end?
Decided (owner 2026-11-15, ROADMAP 4F A): the stage end stays the host's vote for the whole party; every other
Creature editor stays personal. This is the merged behaviour, no code change.
Next (ROADMAP 4G): when to release the merge as 56-dev.
