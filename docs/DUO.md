# SporeCoop Duo private test build

The current local combat checkpoint is 0.7.3.6-duo12-dev. Use the same build
on every machine. Duo 12 adds the Cell combat receipt used to synchronize
fractional damage with the host.

Windows, Spore with Galactic Adventures, current Steam or GOG 2024 executable.
Start with a disposable campaign. This preview includes connection checks, distinct
Tribal villages with shared progression, and automatic two-species Civilization entry.
Complete Civilization play and the transition from two civilizations
to one empire with two founding species are under development.
Both players share Space diplomacy. The intended player-side portrait for a contact is
the speaking player's species. A conversation must never change the empire's species globally.

## Development milestones (2026-11-05)

The private Tribal checkpoint is 0.7.3.6-duo10-dev. Duo 9 remains available
for rollback with its own pre-Duo-10 saves. The first
Cell-to-Creature segment passed its isolated Windows checks. The live
Creature-to-Tribe transition and automatic founding now pass. Native duo
Tribal outfitters, shared clothing unlocks and saved appearance lineage pass
the normal, cancel, cold-load, interrupted reconnect and resumed-editor paths.
Both villages also receive the earned native hut upgrade, retained after cold
load with repair disabled. The fresh automatically founded campaign now reaches
five native victories with both founders intact. Host and guest raids earn
shared history and unlocks. Native graduation, matching custom designs and
both species' capitals also pass, including a prolonged editor lifetime check.
Guest departure before advancement and during vehicle editing both pass:
the host advances alone and reconnect preserves both species and their current
outfits. The packaged DLL also cold-loads both capitals and designs before
the guest rejoins. Real two-PC connectivity and broad compatibility remain
release gates. See the qualification limits below.

| Order | Deliverable | Acceptance |
|---|---|---|
| Test foundation | Reproducible isolated Windows pair and retained evidence | Known build, fixtures, logs and rollback package |
| Verified locally | Cell to Creature, two species | Separate native conversion and editors, shared DNA, recovery, save/rejoin, cold restart and install/remove pass; prior C&C model/paint coverage retained |
| Verified locally | Two playable Tribal villages | Separate species/ownership; shared five-victory campaign, unlocks and history; revival, capture/rebuilding, outfitters, native graduation and reconnect |
| Required next | Two Civilizations | Automatic transition and per-player controls; team victories, unlocks, history and advancement remain shared |
| Required next | One Space empire, two species | Shared diplomacy and resources; each speaking player's portrait; location and reconnect persistence |
| Required next | Supported Galactic Adventures | Captains, objectives, combat, rewards and return to campaign agree |
| Release | Campaign and compatibility qualification | Full path, declared mod combinations and real two-PC VPN checks |

### Personal Space launch

Every founding player should create and name a captain of their own species,
design their own spacecraft, and receive the native flight tutorial, including
the game's choice to play or skip it. One player's choice must never mark a
partner's tutorial complete. Both belong to the same empire and use its
shared campaign progress and treasury.

The host can advance while a partner is offline. A returning founder with
unfinished setup should resume their own setup. Closing the game during flight
training keeps the ship and captain and restarts the unfinished lesson. A founder who
finished it should recover their own captain and ship. Store these choices
against the persistent founder identity; a reused network seat must never
inherit them. Verify the actual two ship models and captain bodies on both
machines, independent tutorial choices, cold reload and rejoin. Extend the
same rule to the third and fourth founders when those seats are playable.

Reconnect policy (user decision, 2026-11-04): the host may advance while the
guest is offline. The returning guest joins the host's current stage with
their own species preserved. The host's world does not rewind. Founder
identity and pending evolution choices must persist independently of a live
network seat, and required editor/setup steps complete before ordinary control
resumes. Test departure before each transition, several skipped stages, host
save/restart, interrupted catch-up and repeated reconnect without duplicate
species or settlements. Rebuilding must retain the same campaign identity.
Tribe-to-Civilization catch-up now passes departure before advancement, departure
during editing, native saves and a full process restart. The complete multistage
catch-up policy, including several skipped stages and pending per-player
evolution choices, is not implemented or qualified yet.

Scope correction (user decision, 2026-11-04): baseline means the complete
linked co-op campaign described above, including its required later stages,
recovery, reconnect and installation support. Independent progression,
mixed-stage worlds and party merge/split are unapproved ideas, not milestones.
Adding them requires explicit approval. A local checkpoint is not completion
and is not a reason to stop available baseline development.

Shared progression clarification (user confirmation, 2026-11-04): Tribal and
Civilization stages retain separate species and settlements/factions, but one
team campaign. Either founder's capture or alliance counts for both players.
Both receive the same campaign unlocks, aligned history and shared advancement.
Separate ownership and controls never imply separate progression tracks. The
Space handoff still produces one empire with both founding species.

Four-species preparation approved (2026-11-04): complete and qualify the duo
campaign first. Founder identity now uses a bounded collection of persistent
campaign slots, separate from reusable network seats. Team membership checks
use that collection. Existing DUO1/CIV1 saves retain their exact byte format;
the writer rejects populated extra slots rather than losing their data.
Four-species gameplay, its save-format extension and player binding remain
later work. This structural preparation does not enable four-founder games.

Approved scope update (2026-11-06): qualify one to four persistent founders,
in duo, trio, then quartet order. Their species, settlements and team progress
survive a disconnect. Offline command of another founder's Civilization units
defaults off and is to be a campaign option. Difficulty Scaling defaults to
100% for new campaigns. Each additional founder adds the selected percentage
of the stage's baseline, rounded up per founder for whole rivals. A duo at
50% needs eight rivals; a quartet at 100% needs twenty. Save the target with
the campaign and keep legacy saves at their original targets. Cell food and
Creature earned DNA use the reciprocal workload multiplier with fractional
credit carried forward. Keep part prices, refunds and death penalties intact.
Prove a six-rival campaign before enabling the larger counts. These are
development requirements; the qualified checkpoints below retain duo rules.

Campaign setup reserves one to four founders before play. The host may start
alone. Difficulty uses the active stage's reserved count, including empty seats.
In the F9 lobby, enter a number from 1 to 4 and select Next stage seats.
The console equivalent is coop.seats N. Use the current count to cancel.
Space uses Next load seats. Leave and rehost in the same running Space world
to keep the change pending. Loading the named Space save activates it.
The host protects seats already occupied in that campaign, including after
disconnect. If a pending shrink conflicts with a newly occupied seat it is
cancelled. Older seat files lack occupancy history and protect their original
capacity. They can still grow. The new client and host must use the same build.

In a four-founder campaign at 100% scaling, a solo host takes
on the four-founder workload. A later player occupies a reserved founder
and creates their own species on arrival. Unclaimed seats reserve space only. They
provide no AI-controlled founders, settlements or units. The new player inherits
team progress. Joining neither raises the target again nor
spawns a new rival wave. The reservation and difficulty settings must
survive saves and restarts. Enemy combat settings are independent of objective scaling. Each extra
reserved founder adds 15% of base enemy health and 5% of base enemy damage
by default. Four seats therefore give 145% enemy HP and 115% damage even with
only the host online. The host can change the defaults for new campaigns in
the campaign section of the mod configuration using enemy_health_per_extra_founder
and enemy_damage_per_extra_founder. Existing reserved campaigns retain
their saved percentages. The intended combat coverage includes hostile combatants
from Cell through Space, including hostile Civ vehicles, armed defenses and
enemy spacecraft. Initial live checks cover Cell predators, Creature enemies,
Tribal fighters, and pirate ships in a colony raid.
In newly reserved campaigns, Creature hunger drains 2.5% faster per extra
reserved founder (7.5% faster at four seats). Set hunger_basis_points_per_extra_founder
in the campaign section to configure new planets, where 250 means 2.5%.
Existing reserved worlds keep their earlier hunger rate. Where a stage supports enemy groups, add one enemy per extra
reserved founder to a newly generated encounter. The host sets the group size
once. A join or reconnect must not generate another wave. Extra enemy groups
still need qualification in each stage that supports them.

Only bugs blocking the active milestone or threatening saves enter its scope.
Research without a verified finding is reviewed after 45-60 minutes. Existing
findings are searched before new probes. Tests that share a game pair run in
sequence, and every restart waits for the previous processes to exit fully.
Native state on both games, save/reload and clean exit are acceptance evidence.
A lab command used to prepare a fixture is identified as such; it does not prove
that the corresponding player-facing action works. Two local instances do not
prove connectivity or compatibility on a second PC.

### Baseline evidence

- Verified components include distinct Cell conversion and first editors,
  Creature revival and shared DNA expiry, C&C model/paint transfer, native
  species persistence, and installer restoration. See the checks below.
- Creature checks now cover native death captions, migration and walking after
  recovery, guest departure during a timer, reused network seats, native save,
  rejoin and cold restart. The final-build landfall also passed separate native
  conversions, adding and saving legs, and walking with distinct species and
  shared DNA. The installed Duo 9 package cold-loaded that new campaign with
  both species and 65 DNA. Uninstall restored the original Steam API DLL,
  retained the save and choices, and a clean reinstall passed.
- Tribal controls, recovery and shared progression have passed isolated checks.
  The fresh campaign now earns all five objectives and both species graduate.
  Offline Tribal advancement and cold reconnect pass. Full Civilization controls,
  later transitions, the shared Space empire and full adventures remain unfinished.
- The controlled guest reconstruction now retains its original faction and
  species in a fresh native tribe. Both peers pass native ownership/population
  checks and guest walking controls after a full restart. The defeated-host
  path also passes native ownership and walking after a checkpoint reload;
  reconstruction uses a reload to refresh native controls and buildings.
  The development build now performs that save, guest transfer and host reload
  automatically. Enemy last-villager defeat and partner rebuilding through F9
  pass in both directions. Missing helpers and occupied sites are rejected.
  Later component checks also cover hut defeat/rebuilding, native combat,
  births/growth, tool purchasing/equipment, healing, eating, food gifts and
  guest conquest rewards/checkpoint transfer. Musical performance and repeat
  wins pass. Shared alliance credit and checkpoint transfer now pass, including
  a cold restart preserving native history, colors, unlocks and relationships
  with both founders (BUG-9038). F9 performance start, NPC target click and
  native instrument keys pass on two games (m2-performance-ui-b.log).
  Both founders now use the shared campaign's native population limit;
  earned capacity9 and guest F9 births through that limit pass (BUG-9039).
  Automatic guest founding after Tribal entry now passes native creation,
  archived landfall species, ownership, walking and cold restart (BUG-9040).
  This exposed and fixed archive validation for compressed paint headers and
  inherited stock Cell paints (BUG-9041/9042). The final patch also passes the
  complete live Creature-to-Tribe path, both species' own editors, automatic
  founding, shared save/reload and guest movement (m2-live-complete-b.log).
  Native outfitters and saved lineage also pass normal/cancel, interruption,
  reconnect and cold-load checks. Fresh five-victory completion, host mouse
  raids, the native City Hall/vehicle editors and automatic separate capitals
  pass. Test food, travel and nonlethal damage shorten raid fixtures; native
  AI earns their victories. These are two local game instances, not a two-PC test.
- Current third-party gameplay-mod coverage is incomplete. The existing exact
  build and C&C join checks are not a general mod compatibility guarantee.

### Qualification limits

This is a private test build, not a claim of a bug-free or desync-free release.
Native economic, military and religious graduation have full local replays
on the development branch. Internet/VPN play on two PCs, extended
soaks and arbitrary third-party mod combinations still need qualification.

Additional Tribal validation: both peers retained one conquest and four
alliances, the same victory colors and all nine tool unlocks after a cold
restart. This mixed-history campaign graduated as economic, retaining both
species' capitals. Fresh NPC villages exposed a political-ID collision that
sent diplomacy to the wrong tribe; the source build corrects it and passed a
native two-instance regression. The previous packaged checkpoint predates this
fix. Two walking replays still show unexplained host relocations followed by
guest corrections (BUG-9062). Tribal validation is not complete.

On the patched fresh campaign, the first NPC alliance credited both founders;
both games agreed on its totem, color, level and four unlocked tools. At the
next NPC the guest reached friendly status but its repeat performance stalled
(BUG-9064). The subsequent replay completed all five alliances. A separate
fix counts guest-earned allies in the game's native consequence calculation.
The same saved world selected military with the fix disabled and religious
with it enabled. The normal religious vehicle editor and both distinct
Civilization capitals passed with the fix. This is source-build coverage.
The opt-in six-rival experiment also reached the normal City Hall editor.
Two original founders and six distinct rivals fit Vantiegan at its original
size. Both peers retained five allies at 4.333333/5 before restarting,
then earned a sixth alliance, five visible totem segments and 5/5 points.
The trial starts with three native victories. General scaling for a new
campaign and separate three- and four-founder gameplay are still pending.
Both founders graduated into separate religious capitals with matching
custom designs. With one city each, both Civilization HUDs displayed 20%
team progress; the disabled control displayed 10% on both games.
The default duo target of ten rivals plus two founders also fit a separate
Vantiegan copy at normal planet size. All twelve villages survived a restart.
Ten-victory progression and four-founder capacity still need qualification.
In a Civilization capture fixture, Space readiness stayed locked through
nine team cities and opened at ten. Each of the two original founder nations
owned five actual cities. Both games agreed on technology, unlocks and the
campaign's eight captures. This check used the native city capture routine.
The same ten-city save opened the native spacecraft editor after a ready
vote. The host built a ship, named the captain, completed Mission Control
and flew into orbit. Only then did the guest's Space checkpoint load.
Both ships were controllable in Vantiegan, both games reported native
empire 800021CA and both retained founder IDs 291489FF and 7CF26CCB.
A cold orbital load and a subsequent guest rejoin preserved the campaign
treasury. Each local ship had 2250 HP. This verifies founder records;
individual Adventure captain bodies and speaking-player portraits need
their own tests.

The personal launch replay now opens a native ship editor and captain name
prompt for each founder. Both native captain bodies match their respective
species. In one branch the host skipped flight training and went into orbit
while the guest accepted training and stayed on Vantiegan. The guest saved
a cosmetic change through the native captain outfitter. A cold orbital save
and guest rejoin retained the two captain models, treasury and different
ships, including the actual remote ship models rendered on both games.
A returning guest also played the missed launch from a saved Civilization
victory, and from the galaxy menu with the host already in Space. In the
menu test the host offered its retained ten-city victory. The guest loaded
it, designed a ship, named its own captain and reached Mission Control.
A second guest joined from an earlier save with one city per founding
nation. It recovered five cities per nation, designed its own ship and
reached its own tutorial. A network disconnect at Mission Control also
preserved the guest's open choice, ship parts and captain model on rejoin.
When the guest closed Spore after one orb, the next launch kept the same
ship and captain. The guest confirmed the saved ship in Civilization
History and restarted the native lesson at seven cities. These tests used
two local instances; a full seven-city flight still needs qualification.
Inherited upstream citizen/animal interpolation issues (BUG-0386, BUG-0410,
BUG-0445) remain open, as does the startup rendering artifact investigation
(BUG-9055). The Tribal progression tests do not establish that those are fixed.

The first interruption patch passed host death, native hatching and walking
after a guest left. It also retained a departed guest's pending penalty across
reuse of the network seat, without knocking out the replacement. The disabled
fix reproduced the cancelled timer. A long guest death screen exposed a freed
wildlife proxy (BUG-9012). Checking native object membership before every
Creature movement pass prevents the observed crash. Two fresh host-first
recovery sequences now pass, including one with a 125-second death screen.
Both species and 52 shared DNA survived saving, reconnecting and restarting
both games. The earlier BUG-9011 migration stall did not recur; its root cause
is not established and its regression remains. A failed cinematic workaround
was removed. See docs/tickets/BUG-9010-creature-recovery-partner-leave.md,
docs/tickets/BUG-9012-creature-proxy-freed-between-checks.md and
docs/tickets/BUG-9011-guest-respawn-migration-stall.md.

## Campaign rules under development

A downed player gets a timed window for a partner to revive them. The cost of
expiry depends on the stage. The implemented Tribe rule gives a conqueror ownership
of the defeated player's old village. The surviving founder can help the defeated
player found a different village with the original species.

With two registered founders and village orders enabled, Duo 8 gives the last
living villager a 10-second rescue window. Bring a living member of the other
founding tribe within 15 metres for 1.5 seconds to revive it at 40% health.
Both players see a REVIVE countdown above the member. If nobody revives it and
its village is intact, that same member returns to the food mat at 40% health.
Its village loses 10% of its current food, rounded up and capped at the balance.
This is a provisional tuning value. The other village keeps its own food.
The mod's checkpoint command waits for recovery to finish.

A registered surviving guest village also prevents the host's native
population-loss rewind in a two-player session. Packaged Duo 9 retains the
food-loss respawn behavior; capture and refounding are in development builds.

Unreleased Duo 10 development behavior: enemy last-villager defeat now captures
the old hut and food mat when the rescue timer expires. The conquered site and
the waiting founder's original species survive save/load. The surviving player
selects a living helper, moves it at least 60 metres from all village huts, and
chooses F9 > Village > Rebuild. The defeated species receives three citizens and
15 starting food at the helper's location; the old village stays conquered.
The mod saves and refreshes both worlds automatically. Registered village
controls restore when the world loads. The same F9 page exposes guest Move and
Gather actions. Hut-destruction defeat and rebuilding also pass local tests. This is
not in the packaged Duo 9 ZIP, and the development version is deliberately
different so its content handshake does not accept that older release.

## Going from Cell to Creature

When a connected guest plays a different cell, Duo 7 uses Spore's native conversion
to build that cell's first land body. Both players get their own early Creature
editor on the same planet. Add legs and save as usual. The guest's new creation
has the cell as its parent and keeps its name and thumbnail. Its campaign selection
is stored on that guest's PC for the same host name and planet name. An explicit
Creature selection in F9 takes priority if you want to bring an existing creation.

In a two-player Creature session on the same planet, a lethal hit gives the partner
10 seconds to reach the downed creature. Stay within 6 game units for 1.5 seconds to
restore 40% health. A revive preserves the pack's DNA. On expiry the host deducts
10% of the shared spendable DNA, rounded up to a whole point and limited to the
balance, and Spore runs the native death and nest respawn. The rate is provisional.
In Cell the existing recovery uses a 10-second window and a missed revive halves
body size. In Space an eligible disabled ship gives a partner 20 seconds to repair it.

Each player launches the same Galactic Adventure and uses their own captain. Duo 6
shows the partner's position with a name marker. When both players are on an act
with one “Move To” goal requiring one target, either captain can reach it. Once
Spore advances the arriving captain to the next act, it also credits the waiting
captain through the native goal system. Dialogue, combat, multi-goal acts, the
final mission result and captain XP are still local. Use the adventure's own menu to exit.

## Playing Tribal

Enter Tribe together from the shared Creature campaign. Both players use their
own species' native editor. The second village is founded and registered
automatically. The host uses Spore's native selection and orders. The guest
opens F9 > Village to select villagers, move, gather, fight, raid, build and
equip tools, perform, heal or rebuild. Both share victories, history, unlocks
and advancement while keeping separate species, villages and food stores.
The native outfitter vote opens each player's own species with the same earned
clothing palette. Five team victories make native History graduation available.
Complete the host's City Hall and vehicle editors when both players are ready.

For a custom guest creature, the fork archives its original model and metadata beside the
planet save before founding the village. Both games restore saved models before loading
that planet, including when the creator is offline. The required parts and paints must still be installed.
Each saved planet accepts up to 16 archived model versions, each at most 8 MiB. A damaged
archive or an unavailable part stops the load and leaves the save available for repair.

Both native food stores follow the host, and the co-op HUD shows both balances.
Registered village ownership restores on load. Developer founding/registration
commands are for disposable legacy fixtures; normal campaign entry does not
require them.

While village orders are on, the host decides when new NPC villages form. If the guest
still has an extra village for six seconds after receiving a complete host village list,
the guest pauses its orders and asks for the host's current save. The host automatically
saves and reloads the guest once per host world/session. If a difference recurs, both
players can leave and rejoin before continuing.

## Civilization test

Native Tribal graduation automatically establishes separate capitals for the
two saved species. Both native cities and their custom City Hall/vehicle designs
are checked after transfer. The developer `bind_guest` command is for old
disposable fixtures and is not required by the campaign handoff. Complete
Civilization progression and the Space transition remain under development.

`coop civ citizens` reads the creatures Spore has actually instantiated. Move the camera close
to the second city to see its residents. `coop founders` reads the identities in the save.

The development build reserves a disconnected founder's cities and vehicles
by default. On the host, F9 > Nations enables temporary command while the
partner is away. The guest regains command on reconnect. The setting is saved
per campaign. The original factions, cities and species survive the handoff.

## Install and remove

1. Both players close Spore and extract the complete ZIP into a folder they can keep.
2. Open SporeCoopSetup.exe. Confirm the Galactic Adventures folder and click Install.
   The adjacent .pack contains the mod. Keep the setup and pack together for reinstalls.
3. To remove it, close Spore, open the same setup and click Uninstall. If Windows
   denies write access to the game directory, run the setup as administrator and retry.
   Uninstall restores the original Steam API DLL and keeps player saves, settings and logs.
   Keep a pre-mod save for vanilla play. The fork registers archived custom founders when
   their planet loads. Reinstall the matching build to resume those campaigns with their models.

If Steam shows error 1004 on the first launch, start Galactic Adventures once from your Steam library to
finish its setup. If the red Creepy & Cute palette is missing, check the full Creature Editor. For a Steam
installation with separate game folders, the Spore ModAPI community explains how to copy the owned
bp1content folder into the Galactic Adventures folder. https://launcherkit.sporecommunity.com/support/creepyandcute

## Play with a friend

Use the same Duo build, content and gameplay mods on both machines. Start with only
Galactic Adventures and the matching Creepy & Cute configuration. In the galaxy menu
choose Co-op (or press F9). One player selects Host. The other selects Join and enters
the host's virtual LAN address. The host then loads the disposable save.

At a hotel, share the host PC with your friend through Tailscale. If you already use
Hamachi, join the same Hamachi network and use the host's Hamachi address instead.
The game listens on UDP 47810. The VPN is installed and managed separately by each player.

The join check compares the fork rules and the loaded Creepy & Cute parts. A check of
every third-party mod and gameplay setting is still pending. The host and guest should
both see the same creature, pack parts and paint before using any longer-running save.

## What passed on Windows

Two Steam Galactic Adventures instances on one PC shared a saved planet, retained its
two built-in tribal species after restart, and agreed on the founding roster after a new
guest joined. In a separate test, both games built a custom guest creation with 48 parts,
including one Creepy & Cute part, and reported the same three paint layers, including
two pack paints. This checks native model data. Independent armies, end-to-end species
progression, diplomatic portraits and a two-PC internet session still need testing.

In the separate-village test the guest earned 5 food in its own village and both games
reported the same balance. A guest-only change of +100 to the home food mat was corrected.
An earlier build allowed extra NPC villages on the guest (BUG-9002). On Duo 4 an
injected guest-only village did not cascade, and automatic checkpoint recovery restored
the two founding villages. The same test passed after reconnecting.

In Civilization, 19 citizens of the assigned nation used the guest species on each game
after a process restart. A fresh offline profile with the custom guest's archived save
also built 19 citizens with its Creepy & Cute creation, without an editor library or co-op cache.

On a live Cell-to-Creature transition, both players landed on Brois. The native
avatars used the host's species and the guest's preselected Creepy & Cute species.
This run started from one shared cell model. In a second run, two different cells
evolved into Gaal and Stabbella. Their actual early editors opened on different
bodies, and editing and saving legs on each kept both species after landfall.

In Adventure Town either captain could complete the town gate for both players.
The other captain stayed in place and received one goal. Both games reached the
second act and returned through the native menu. In Creature both the host and
guest could be revived, and an expired timer debited both DNA displays once.
Both games reloaded the saved balance of 92 DNA after a cold restart.

In regular Space, one partner could stay on the home planet while the other flew
in orbit. When both ships met in the system, the host repaired the guest from
1 to 751 of 1500 HP. This check started in Space and used the existing ship system.

The F9 host screen accepts the number of seats and the objective scaling
percentage for a new planet. Start hosting in the galaxy menu before creating
the planet. Each new named campaign records these settings on its first save.
A solo Cell test reserved four founders. A cold replay selected two seats and
50% in the lobby, then loaded the saved four-seat planet and recovered its
original 100% setting. The UDP host also enforces its seat limit at admission.
These checks cover setup and persistence. Four simultaneous species and the
full scaled rival roster still need a live playthrough.

On a four-seat Cell planet at 100% scaling, four raw food points now add
one to the native food total, even with only the host online. Plant and
meat each retain their own fraction. Three saved quarter-points and another
point after a cold restart paid exactly one. Eight raw guest points then
paid two into both native totals. The food-credit file is bound to the
native planet contents, so a mismatched save cannot reuse newer credit.

With four reserved seats at 100%, a guest eating a native plant worth 12 food
gave both players three points. A Creature kill worth seven DNA gave the team
1.75 DNA. Direct four-point adjustments kept their full value.

The initial combat fixture used 25% hostile health and 10% outgoing hostile
damage per extra seat. A four-seat test with two games measured an engaged
Creature enemy at 10.5 HP instead of six, and a two-point hit dealt 2.6.
A Tribal rival with 162 native HP had 283.5 on both peers and a four-point hit
dealt 5.2 to a founder. These checks cover creatures and rival Tribal fighters.
The original fixture preceded Space combat support. Civ combat remains open.

On QuartetTribe the solo host had one native village. The read-only terrain
probe found 23 more compatible positions for four founders and twenty rivals
on that seed. Native rival placement, access routes and durable founder
reservations need separate tests.

Two local Space games saw the same native pirate maximum health. A bomber
with 250 native HP had 362.5 HP at four seats. A twenty-point hit took 23 HP
from the host. Hitting the guest's ship took 23 there, once. Leaving the
session returned the bomber to its native maximum. This test used a disposable
colony and a returning pilot's own ship.

In Cell the host keeps separate hundredths for qualifying enemy HP and hits.
At two reserved seats, 39 natural enemy spawns totaling 234 native HP received
35 extra HP and carried 0.10. Bite tests on the host and guest accounted for
two extra damage on 40 native damage. The host owns both fractions.
The guest rejoined without resetting either total. After a cold restart, the
host recovered nonzero fractions for both HP and damage. Existing Cell food
credit files retain their plant and meat balances on the new build.
Extra predators use the native encounter's species and size. The host skips
additions when the Cell pool is full or a clear position is unavailable.
Cell enemy HP caps at 255, the largest value its network state can carry.
