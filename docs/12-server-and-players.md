# 12. Server and players (design, 2026-11-17)

Decision 5G B (owner, 2026-11-17: "5GB - let's plan this."). Built on the alpha branch feat/lineages-alpha (decision 5J B: not released publicly;
what applies to the live builds goes to feat/lineages-47). The headless spike goes first (5H A); friends-level checks plus
kick and ban (5I A). The plan for separating the server from the players, so
the game runs smoothly on every player's PC while the server keeps control of what counts. A design to agree on before
building. The owner's decisions on it go to docs/ROADMAP.md (IDs from 5H).

## The goal in the owner's words
<a id="server-goal"></a>

* "let's discuss the big picture of separating the server from the players. While still preserving some control over
  what's cheating and what's not, we'll separate so the game functions smoothly between clients, allowing the host /
  server to merge and sync things." (2026-11-17)
* "Every player will have their own creature and it will feel like single player vanilla. So we need to build in a way
  that'll feel like everyone is a host of their parties." (decision 5CD, 2026-11-16)
* "we can utilize help from the Fika project for SPT. They run a 'headless client' that runs the server. Stripped of
  graphics and whatnot." (2026-11-17)
* The long-term goal (ROADMAP): an open-world MMO fork, a planet holding roughly 10-20 players.

## Where we are today
<a id="server-today"></a>

* Star network: the host is the server and also a player; guests talk only to the host (docs/04-netcode.md 4.1).
* The host's game simulates the shared world: wild creatures, herds, nests, world events (drama), tribes, the Civ
  stage's wild creatures, deaths, DNA credit, the save. Guests show copies (proxies) moved by the host's states.
* Each player's game runs that player's own creature; the others see it as a puppet.
* Since 5EA (64-dev) each partner's game also runs that partner's pack (docs/re/creature.md#creature-pack-own-game):
  the first "owned by its player" thing besides the creature itself. It proved the model: the game's own single-player
  code (follow, defend, attack) worked unchanged once the pack ran on its player's PC.
* Code: about 140 places ask "am I the host?". Many of them mean "am I the server", some mean "am I player 0". That
  difference matters for a server with no player of its own.
  CORRECTION (2026-11-17, feat/lineages-alpha): about 1,000 places (765 calls of the modules' is_host / is_guest, 175
  direct Role checks, 49 "from the host" checks); with player-hosted planets (5N A) they already mean the planet's host
  (decision 5O).

## The model: one owner per thing
<a id="server-model"></a>

Every synced thing has exactly one owner at a time: the game that simulates it. Everybody else shows a copy. Two
simulations of the same thing are never merged afterwards (that is where desyncs come from): ownership moves instead.

| Kind | Examples | Owner | Server's job |
|---|---|---|---|
| A player's own | creature, pack, Summon Flock, later its tribe, cities, ship | that player's game | check it, relay it, save it |
| Shared, near players | wild creatures, herds, nests, fruit, eggs | the game of the nearest player (hand-over as players move) | assign and move owners, check |
| Shared, nobody near | the rest of the planet | the server (or nobody: kept as saved) | simulate or keep |
| World truth | the save, deaths, extinctions, DNA and progress, relations, ownership itself | the server only | decide |

* **Owning means simulating**: the owner's game runs the game's own code for it (AI, animations, abilities) and sends
  its state (position, animation, health changes it caused); the others run no AI for their copy (5EA's mechanism:
  the AI skip, the proxies' driving).
* **Hand-over**: the server gives an owner a lease; the owner sends states; a new owner starts from the latest state.
  An owner that goes silent (leaves, lags, crashes) loses its leases after about 1.5 s (5EA's stale time) and the
  server takes them back or gives them on.
* **Hits on things you do not own** go to the owner (or the server) as requests, as a partner's bites already do
  (MSG_DAMAGE_REQ). The owner of the victim applies them.
* **The player sees no lag on their own things**: they run locally. Others see them about 100 ms late (the snapshot
  buffer), which is what partners see of each other today.

## The server: a player-host or a headless Spore
<a id="server-headless"></a>

The server is always a Spore game: the world's rules (AI, herds, nests, saves) are Spore's own code, so there is no way
to run a server without Spore. A "dedicated server" for this project therefore means a Spore instance with nobody
playing it: the headless route the owner suggested is that dedicated server, not an alternative to it.

* **Fika's headless client** (project-fika wiki): another instance of the game that hosts automatically; every player,
  the host included, joins as a client; rendering is disabled to keep it light; its purpose is to take the AI and other
  heavy work off the players' PCs; it wants its own physical machine (no GPU needed there, no VPS, Windows only).
  The idea carries over. The method does not directly: Tarkov is a Unity game with a no-graphics mode built in; Spore
  (2008, DirectX 9, 32-bit) has none, so cutting the rendering is our own work.
* **What we already have for it**: SporeKit can run the mod's frame from the game loop instead of the drawing
  (`frames=loop`, docs/02-modkit.md), console commands run without frames, QA instances run hidden in small windows at
  5-10 FPS, and several instances run side by side on one PC (tools/qa_win).
* **The unknowns (to test before deciding, phase 0)**:
  1. Does Spore keep simulating with its drawing cut or nearly cut (skip the scene draw, a 1 FPS Present, a tiny hidden
     window)? Its creature movement is animation-driven: animation may need the update but not the draw.
  2. Does the Creature stage run without the server's own player creature? The game is built around one avatar (the
     AI's "player", the camera, the area it simulates around it). A headless server likely needs a hidden stand-in
     creature, or must only arbitrate and leave all creature simulation to the players' games.
  3. What does Spore simulate far from its avatar? If it only runs the area around the camera, a single server cannot
     run a whole planet, and ownership by the nearest player (5EC) is required, not optional.
  4. Load, save and host with no one at the screen (menus, dialogs, the Steam / GOG launch, crashes and restarts).
  5. Cost: memory per instance (32-bit, under 4 GB), CPU per planet.
* **Design rule from now on**: code asks "am I the server?" or "am I a player?", never "am I player 0?", so the same
  build runs as a player-host (today) or a headless server (later) with no fork in the code.
* **One planet per server process**: Spore loads one planet at a time; an MMO is a server per busy planet (the
  ROADMAP's "per-planet sessions"), with the galaxy and the players' identities shared above them.

## Phase 0: what a host's game does when nobody watches it (2026-11-17, feat/lineages-alpha)
<a id="server-spike-watch"></a>

TITAN, QA host 9 and partner 8 (Samuth.spo, the QA frame cap of 10 FPS, 1280x960), the host measured 20-30 s per
condition: its CPU (share of one core), memory, frames, and how many of its creatures within 60-110 m of each player
moved more than 0.5 m (scratchpad tool spike0.py; to go into tools/qa_win when phase 0 ends).

| Host window | Frames | Host CPU | Creatures moving | Verdict |
|---|---|---|---|---|
| normal, on screen | 9.8 / s from Present | 22-24 % | 11/17 near each player | baseline |
| moved off-screen (-4000, 0) | 9.7-10.2 / s from Present | 22-24 % | 10-13 of 17-19 | works: nobody sees it, nothing changes |
| hidden (ShowWindow 0) | 446 / s from the game loop | 83 % | 0/20 | the game stops drawing and its world freezes; our frame runs uncapped |
| minimized | 0 | 2 % | 0/17 | everything stops, the mod too: the players would lose the host |

* ✔ Unknown 3 (what Spore simulates away from its own creature) is answered by what we already had: the game simulates
  only around its camera (the relevance pass: herds within 100 m enabled, creatures beyond 250 m back to the pool), and
  the mod adds every partner's position to that list (view_centres.h, BUG-0280). With the partner 411 m from the host's
  creature, 19 of the 22 creatures within 110 m of the partner moved, and the host's live creatures rose from 46 to 76
  (the second area loaded). So a server with no camera of its own can keep its players' areas alive through the same
  list; its cost grows with the number of separate player areas, not with the planet.
* ✔ A cutscene on the host freezes the world for everybody: teleporting the host's creature into deep water started
  the game's "Dangerous creatures lurk in the deep" scene; while it showed, 0 of 18 creatures moved (CPU 15 %). A server
  must never show cutscenes, tutorials or dialogs that wait for a click.
* ✔ The off-screen window is a "headless" host already today (no code), at the full cost of drawing.
* ~ Open then (answered in #server-spike-hidden below): why a hidden window freezes the world (Spore's own pause when it is not visible or not active)
  and whether that pause can be lifted, so the drawing can stop; a frame cap for loop frames (446 / s wastes a core);
  what the drawing costs (a 320x240 window against 1280x960); a host whose own creature is parked or missing; the
  memory over a long session (511 -> 589 MB in 15 min here, with a second area loaded).
* Seen on the way: the partner, left standing 411 m away, was killed by that area's creatures; its respawn waited
  for a click on its own screen (a test effect, not a bug).

## Phase 0: a hidden host keeps running (2026-11-17, feat/lineages-alpha)
<a id="server-spike-hidden"></a>

* ✔ Why a hidden window froze the world: Spore runs its whole frame, the world's update included, from WM_PAINT.
  App::Canvas::Tick (Steam 0x7382F0) only invalidates the window (an import called with (window, 0, 0)) and pumps its
  messages; a minimized window sleeps 200 ms there instead. A hidden window gets no WM_PAINT, so no frame ran: no update,
  no drawing, and no sleep for the frame limit (the loop spun).
* ✔ Proof from outside: WM_PAINT sent to the hidden host's window at 10 Hz (SendMessageTimeout from another process)
  brought it back: 7.7 frames a second, 10 of 17 creatures moving.
* ✔ Built (alpha, SporeKit): a hidden, not minimized game window is painted from Canvas::Tick at the frame limit, the
  loop sleeps 1 ms between (`kit.fix hiddenpaint`, `window.hidden on|off`). Measured on TITAN (QA cap 10 FPS,
  tools/qa_win/headless_check.py): shown 10.0 frames, 21 % of a core, 9/17 moving; hidden 9.8 frames, 22 %, 12/16
  moving; hidden with the switch off 425 frames from the loop, 77 %, 0/15 moving. With the host hidden, a partner's pack
  (5EA) killed its target in 6 s and followed its partner as with a shown host (1254 paints in 3 min).
* ✔ The drawing costs the graphics card next to nothing (1.2 % of TITAN's at 10 frames, 1280x960); the CPU of a frame is
  mostly the game's own frame work (its creature AI is 0.1 s a minute). Cutting the drawing out of the frame is an
  optimisation for later, not a blocker.
* ✔ Memory: about 850 of the 2047 MB address space free in play (the game is 32-bit without large addresses): the
  ceiling per server process, so one planet per process and a limit on players and creatures in it.
* Verdict of phase 0: yes, a Spore instance nobody watches can be the server today (hidden window, view centres for
  every player's area). Left for phase 5: the server's own creature (the game needs one: parked, protected, not shown
  to the players), no cutscenes or click-to-continue dialogs on the server, an unattended launch, load and save, and
  maybe cutting the drawing.

## Cheating: trust, but check
<a id="server-cheating"></a>

* What can be stopped: the server keeps the truth that matters (saves, DNA and progress, deaths, extinctions,
  ownership), and checks every request against what it knows: only the owner may send a thing's states (5EA already),
  speed and position jumps within the creature's limits, a bite only within reach of its target, damage within the
  ability's own values (today only a cap, kMaxHit), how often (rate limits), summons and spawns only through the server.
* What cannot be stopped: a modified game can lie inside those limits (a perfect aim, never missing a beat). No game that
  lets players' PCs simulate can prevent that; the big online games add logging, reports and bans.
* Kick and ban are built (BUG-9251, docs/04-netcode.md#net-kick-ban): the host's `/kick`, `/ban`, `/unban`, `/bans`.
* For co-op with friends: the checks above are plenty. For public servers later: logging of check failures, kick and
  ban tools for the server's owner, then more checks where players find holes.

## Per stage
<a id="server-stages"></a>

| Stage | Today | Moves to its player first | Later |
|---|---|---|---|
| Creature | host runs the world; own creature and (5EA) own pack per player | own Summon Flock (5FB); own nest family | wild creatures near each player (5EC) |
| Tribe | host runs all tribes | each player's own tribe members | wild creatures and rival tribes near a player |
| Civ | host runs the planet | each player's own cities and vehicles | rival cities near a player |
| Space | each player flies in their own copy of the host's galaxy | already mostly per player | own empire per player; the galaxy's changes kept by the galaxy server (#server-space) |
| Cell | host runs it | keep as is (short stage) | |

## Phases
<a id="server-phases"></a>

Each phase ships on its own and keeps today's co-op working.

| Phase | Work | Done when |
|---|---|---|
| 0 Spike | A QA Spore instance as a headless host on TITAN: drawing cut, its own creature hidden or absent, two players join, play 10 min. Measure CPU, memory, what still simulates | done 2026-11-17: yes (a hidden window, painted by SporeKit; #server-spike-hidden); the server's own creature and cutscenes left for phase 5 |
| 1 Server role | "server" and "player" separate in the code (the ~140 host checks sorted); the host's own player goes through the same paths as a guest's where it can | today's tests pass; a player-host behaves as before |
| 2 Ownership layer | 5EA generalised: any creature can be owned by a player (lease, states, animations, hits to the owner, hand-over, silence); the server's checks in one place | the pack runs on it; a creature can be handed from one player to another without a jump |
| 3 Own Summon Flock | each player's Summon Flock runs on its own game (5FB); the party limit above 1 becomes real | two players' flocks out at once; the limit enforced by the server |
| 4 Shared creatures near players | wild creatures, herds and nests near a player owned by that player's game, handed over as players move; the server simulates or keeps the rest (5EC) | two players far apart each see a living world; the server's CPU drops |
| 5 Headless server | only if phase 0 said yes: launch, host, save and restart with nobody at the screen; admin commands | a session with the server on its own PC and every player a client |
| 6 Per-planet servers | one server per busy planet, identities and parties across them | ROADMAP MMO questions decided |

Phases 1 and 2 are needed whatever phase 0 says; phase 0 only decides whether phase 5 is possible and how phase 4
looks. Phase 0 is small (one to two days) and goes first.

Status (2026-11-19 review): phase 0 is done. The work since followed player-hosted planets (5N A) and the galaxy
server rather than phases 1, 5 and 6 as listed (docs/ROADMAP.md#roadmap-alpha-now). Phases 3 (an own Summon Flock
per player) and 4 (the wild creatures near each player run by that player's game) are still to do.

## Risks
<a id="server-risks"></a>

* Spore may need its avatar and camera for most of its simulation (unknown 2 and 3): then the headless server
  arbitrates only and the players' games do all the simulating; still workable, the server just runs less.
* Hand-over is where desyncs hide (a creature mid-attack, mid-animation, carried, in a summon): the ownership layer
  needs its own tests for each.
* 140 host checks to sort: slow, careful work; done stage by stage, Creature first.
* A 32-bit game per planet: memory per instance limits players and creatures per server.

## The server's game copy, its memory and its CPU (2026-11-17)
<a id="server-copy"></a>

Owner, 2026-11-17: "couldn't we use any sort of launcher we want for the server? Meaning we can get 4GBs, or even more?
We don't need to worry about Steam for a server right? Would Spore consume less CPU if theres a host / server running
the instance?"

* 4 GB yes, more no: Spore is a 32-bit program. The Large Address Aware flag in the exe's header gives it 4 GB on 64-bit
  Windows instead of 2 (docs/INSTALL.md#install-laa, tools/laa.py; the QA copy has it off: 0x102). No flag or launcher
  takes a 32-bit program past 4 GB; more memory means more server processes (one per planet), which the design already
  has.
* No Steam needed: the GOG 2024 build is the same code as Steam's (docs/05-re-notes.md 5.1.1) and is not wrapped in
  Steam's copy protection, so a server copy of it runs without Steam and can carry the LAA flag safely. The Steam exe
  is encrypted on disk (its loader decrypts it) and needs Steam running. Each server still needs its own owned copy.
* CPU on the players' PCs: the world's AI is cheap (0.1 s a minute on the host); each player's frame is mostly its own
  drawing and animation of what it sees, which a server cannot take over. A server takes the extra work off whoever
  hosts today and keeps the world running when that player leaves; it does not make the players' games lighter.
* CPU on the server: a hidden server costs what a shown game costs (22 % of a core at 10 FPS on TITAN); cutting the
  drawing and running at a low tick rate would lower it (phase 5).

## Galaxy and planets for the MMO (2026-11-17)
<a id="server-galaxy"></a>

Owner, 2026-11-17: "We'd need to rework the galaxy menu too for MMO version. We'd need to have multiple planets working.
Player starts Spore & presses F9 / SporeCoop, connects to the Galaxy Server, Galaxy either becomes the server's loaded
worlds that you can select from or maybe a new menu to spawn the player somewhere? I'm unsure."

* Two kinds of program on one server (see #server-one-galaxy): a **galaxy server** that is not Spore at all (a small program: the list of planets and their
  servers, players' identities, parties, bans, which planet a player was on; it can run anywhere, a cheap Linux VPS
  too), and **planet servers** (hidden Spore instances, one per planet that has players, on a Windows PC; started when
  a player goes there, stopped some minutes after the last one leaves, the planet saved).
  ~ superseded (2026-11-19 review): planets are hosted by a player's game on the planet (5N A,
  #server-load-and-player-hosts) and the galaxy server stores them (#server-world-storage); a server-run Spore
  per planet stays a later option for busy planets.
* The player's way in, recommended: Spore, F9, "Online" connects to the galaxy server; a returning player goes back to
  the planet they were on; a new player gets "Find me a planet" (the galaxy server picks one with room and players of
  a similar stage) and the list of live planets (name, stage, players) to pick from. The planet server then sends its
  world, as a host does today.
* Spore's own galaxy screen comes later, if ever: it shows saves and planets of one save, and reworking it is UI work
  in the dark (docs/08-ui.md). The Co-op window's list does the job first.
* Space: flying to another planet becomes a hand-over from one planet server to another through the galaxy server
  (phase 6).

## One galaxy server (owner 2026-11-17)
<a id="server-one-galaxy"></a>

Owner, 2026-11-17: "We'd need more than 4GBs of RAM, and we'd need 1 server hosting the galaxy of Spore, including the
planets, not one instance per planet. Players should be allowed to choose a planet, progress through to space, fly to
another planet, and see other players in the Creature stage."

* What the players get is one server: one address, one galaxy, every planet in it, as asked.
* Inside that server it cannot be one Spore program: Spore holds one planet at a time (one noun manager, one terrain,
  the mod's "world"), and a 32-bit program stops at 4 GB. So the server machine runs the galaxy program and one hidden
  Spore per planet that has players in it, and their memory adds up on the machine (32 or 64 GB of RAM serve many
  planets). Spore's frame runs mostly on one core: separate planet programs also spread the work over the machine's
  cores, and one planet that crashes does not take the galaxy down.
* The players' journey on it: pick a planet (or "Find me a planet"), play Cell to Civ there with the others on that
  planet, reach Space, fly to another planet: the galaxy server hands the player's ship to that planet's server, which
  shows it the creature-stage players there (the planet server runs that planet's stage; the visitor's game shows them
  as the partners are shown today).
* Open research for that journey: a Space visitor on a planet whose server runs the Creature stage (two stages on one
  planet: today every player in a session plays the same stage); what a visitor may do there (ROADMAP MMO questions).
  Answered since: eras, one shared world per stage on each planet (#server-eras), seen from each other as visitors
  (#server-era-spike).

## Load per planet, and planets hosted by players (2026-11-17)
<a id="server-load-and-player-hosts"></a>

Owner, 2026-11-17: "I'm worried that the Server will quickly hit a limit with 32 players even, crashing or maxing out
quickly." And: "we let a player on the world be in control of that planet. The planet is detailed / saved on the server,
so when a player connects to it, the server lets them know the state of the world, and it lets them host the planet.
When they leave, it passes the control down. If no one's there, it's stored in the server for the next connection."

* The concern is valid for many players on one planet, not for 32 players spread over planets. Measured so far
  (#server-spike-watch): a planet program uses about 0.5-0.6 GB and 22 % of a core at 10 frames a second; one more
  player area far from the others loaded about 30 more creatures and about 80 MB. Spread over ten planets, 32 players
  are a few GB and a few cores. On one planet, every player standing in their own area multiplies the creatures the
  planet's authority runs and animates; somewhere past 8-16 separate areas one Spore program would likely run out of
  memory (4 GB) or of its one core (an estimate, not measured: a load test with added player areas measures it).
* The answer to that limit is the ownership model, whoever hosts: when each player's game runs the creatures around
  it (5EC, as 5EA does for packs), the planet's authority mostly keeps state and checks, and its load grows slowly with
  players.
* Player-hosted planets (the owner's alternative) work and fit what Lineages does today (a host is a player): the
  galaxy server stores each planet's world and hands it to the first player who arrives; that player's game is the
  planet's authority; the others join it; when it leaves, the authority passes to another player; an empty planet waits
  in storage. What it needs and where it hurts:
  1. Hand-over without a reload: today a guest's world is a mirror (proxies, the host's states); becoming the authority
     means turning the mirrors into the game's own creatures (BUG-0412's leave_locals does this for a guest left
     alone) and every other player switching to the new host. Simplest first version: the new host loads the latest
     saved world, the others rejoin it: a short loading screen for everybody at each hand-over.
  2. Saving to the server: the authority uploads the world regularly (a Creature save is a few MB); a host that
     crashes loses what happened since the last upload.
  3. Cheating: the authority decides everything on its planet, so a modified game hosting a planet can cheat for
     everyone there. The galaxy server can still keep each player's own progress (DNA, their creature) and check what
     hosts upload, but less than a server it runs itself.
  4. The host's PC and connection carry the whole planet: a weak PC or a slow line slows everybody, and many players on
     one planet hit that PC's limits first (the same limit as above, on weaker hardware).
  5. Reaching a player's PC: hosts behind home routers need the galaxy server to relay their traffic (bandwidth, not
     CPU, on the server).
* Recommended: make "planet authority" a role, held by a player's game or by a server's hidden Spore, with the same
  code (phase 1 makes "server" and "player" separate for this). Start with player-hosted planets and the galaxy server
  storing worlds (cheap, closest to today); give busy or always-on planets a server-hosted authority later; build the
  ownership model so neither hits the limit early.

## The galaxy server as the front door (2026-11-17)
<a id="server-front-door"></a>

Owner, 2026-11-17: "is there no way to host server from the galaxy server, but the connected player simulates it all? So
like, you still connect to the galaxy server IP, but you can join other player's instances on that ip. I'm trying to
spread the load to be more viable."

* Yes: the galaxy server is the one address every player connects to and runs no Spore. It lists the planets, stores
  their worlds, decides which player hosts each planet now (the first to arrive; the next when that one leaves) and
  relays the players' traffic to that host, so to every player the planet is "on the server's address".
* Its cost is bandwidth, not CPU: a partner in play gets about 20-30 KB/s from its host (ROADMAP 4P); relayed both ways
  for 32 players that is roughly 2-5 MB/s. Introducing players to each other for direct connections (where their
  routers allow) lowers it later.
* With the ownership model on top (each player's game runs its own creature, pack and the wild creatures around it),
  the planet's host carries only its own area plus the checks: the load is spread over every player's PC.
* Caveats as in #server-load-and-player-hosts: a hand-over means a short load for that planet's players, a host can
  cheat for its planet (the server still keeps each player's own progress), and the relay adds one hop of latency.

## Space and the galaxy server (2026-11-17)
<a id="server-space"></a>

Owner, 2026-11-17: "What about space stage? Since that's basically a woven net to each player's instance, wouldn't that
be the server's job?"

* Partly yes: the galaxy's shared state is the server's. Today every player flies in its own copy of the host's galaxy,
  linked by star and planet ids (ROADMAP MMO questions, Space briefing). Each copy starts from the same galaxy; what
  players change in it (colonies, who owns a planet, terraforming, empires' relations and wars, where each ship is)
  is world truth: the galaxy server keeps it as one list and sends it to every player's game, which applies it to its
  copy (as space_map and space_allies already do for a partner's colonies).
* The flying and fighting stay in the players' games: each player's game runs Space around its own ship, as in single
  player. A star system with several players in it is hosted like a planet: the first player there runs the system's
  enemies and events, the others join it through the server, the next takes over when it leaves (the 2026-11-12
  recommendation "whoever of the party is there, handed over when they leave").
* The net between players is the server's: it knows which system every ship is in, connects players who meet in the
  same system, and hands a ship over to a planet's host when it lands there.
* What the galaxy does on its own over time (empires expanding, raids on colonies while their owner is away) runs in
  Spore's code in a player's game today. The server can start by keeping what happens where players are; running
  the galaxy's own life with nobody near would mean either a server-hosted Spore in Space or rewriting those rules in
  the server (later, if wanted).
* Seeing each other (owner 2026-11-17: "would players be able to see eachother passing by in space?"): yes, as in co-op
  today (docs/07-modes.md 7.5, space_map.cpp): in the same star system or at the same planet a player's ship is a
  ship in the other's game (a puppet UFO moved by its owner's states); in the galaxy view, where Spore draws no ships,
  a name tag on the star or on the moving ship, an arrow at the screen edge, a click flies there. With many players the
  server sends each game only the ships in its system and, for the galaxy view, the players near it or in its party
  (hundreds of tags would cover the map).
* The hard part: keeping every player's galaxy copy in step with the server's list (a colony, a lost planet, a changed
  relation) without reloading, through the game's own functions.

## The galaxy server, first milestone (2026-11-17, feat/lineages-alpha)
<a id="server-galaxy-m1"></a>

Decision 5O A. Code on the alpha branch only: server/galaxy/main.cpp (the server, `tools/build_galaxy.sh`: Windows with
llvm-mingw, or Linux), net/galaxy.h (the protocol), mods/coop/galaxy_client.cpp (the game's side).

* The server runs no Spore. Every player says HELLO (protocol, identity, name, version) and gets a slot. ENTER <planet>
  makes the first player on a planet its host (ROLE host: the game hosts the world it has loaded) and every later one
  a guest of that host (ROLE guest + the host's slot: the game joins it). LIST gives the planets with their players.
  A player silent for 20 s is gone; a planet is forgotten when its last player leaves; HOSTGONE tells a planet's
  players their host left.
* Relay: a planet session's packets travel inside relay frames ('R', u16 slot, the packet). The game's RelayTransport
  (a net::Transport) gives the session one virtual address per slot (10.88.<slot>, port 47900), so net::Session hosts
  and joins as always and the whole mod above it is unchanged. The server forwards only within one planet and only
  between its host and one of its players.
* Console: `coop.galaxy connect <address>`, `list`, `enter <planet>`, `leave`, `disconnect`, `status`. The server:
  `galaxy_server [--local] [port] [galaxy name]`; `--local` listens on 127.0.0.1 only (a program that listens on the
  network gets the Windows firewall's prompt, which is the PC owner's to answer).
* ✔ Runtime (TITAN, QA host 9 and partner 8 through a local galaxy server): the partner listed the planets ("Samuth
  (1)"), entered Samuth, got the host's world (5.4 MB) through the relay and played: a partner's pack (5EA) killed its
  target in 9 s, its 36 animations played on the host. Relay load: about 185 KB/s while the world was sent, then
  10-16 KB/s for one partner in play. The host leaving: the partner's session ended within 5 s, both games left the
  planet and turned the relay off, the server forgot the empty planet.
* Found and fixed on the way: a host that left switched the relay off before its goodbye was sent (the partner's
  session hung on); the relay now stays until the session is closed, and a partner told HOSTGONE ends its own.
* ✔ Across PCs (5P A, 2026-11-17): the galaxy server on TITAN (its firewall rules already allowed it), TITAN hosting
  Samuth through it, the MSI joining over Tailscale: it got the world and played (a pack fight across the PCs, 9 s).
  The first relay doubled the round trip and lost up to 11 % (the server slept between polls: a 1 ms sleep lasts about
  16 ms on Windows, with default socket buffers); waiting in select() with 4 MB / 1 MB buffers made it 103-106 ms and
  0.1 % loss, as a direct connection (105-125 ms, up to 0.6 %). Relay load in play: 11-12 KB/s for one partner.
* Not yet: the server keeping each planet's world (the host's game hosts whatever it has loaded), the hand-over to the
  next player, a menu.
  Done since: #server-world-storage, #server-handover, #server-galaxy-menu-built.

## The galaxy menu as the server's planets (owner's idea, 2026-11-17)
<a id="server-galaxy-menu"></a>

Owner, 2026-11-17: "Is it possible to make it so when a player connects to the galaxy server, it swaps out their Galaxy
Menu saves with the server's and they can click a planet to begin almost like normal?"

* Possible, and the most natural way in. Spore's galaxy menu builds its planets from the saves it finds
  (cStarManager::ReadStarsFromSaveDB sends the message that rebuilds the menu's planet set: GGE_RebuildPlanets,
  docs/re/creature.md via BUG-0676), each save on its own star.
* The plan: while a player is connected, the menu reads an "online" folder of saves instead of the player's own: one
  save per server planet, downloaded from the galaxy server. The player's own saves are never touched; leaving the
  galaxy shows them again. A click on a planet is caught by the mod: it enters that planet on the server, then loads
  the fresh copy (it hosts) or joins the planet's host (the world arrives as today). "New game" makes a new planet on
  the server.
* Needs first: the server storing each planet's world (the host uploads it; the next first player downloads it).
* To find out: how the menu finds its saves and whether that folder can change while the game runs (or only at the
  next start of the menu); where each planet shows on the menu's galaxy map.

## Players at different stages on one planet: eras (2026-11-17)
<a id="server-eras"></a>

Owner, 2026-11-17, two ways to start on a hosted world from the galaxy menu: (1) the later stages are greyed out until
the world's creator unlocks them; the creator is away in Cell; a player who joins finishes Cell and starts Creature in
the same world while the creator is still in Cell. (2) Every stage is open from the start: a player starts at Tribe
while the host is in Cell, Creature or Civ.

* The limit: a Spore game runs one stage at a time, and one planet surface cannot be in two stages at once (Creature has
  nests and herds where Tribe has villages and Civ has cities; Cell is not on the surface at all). Today every player
  of a session plays the host's stage (docs/07-modes.md 7.6).
* The answer: one era per stage on each planet. Each era (Samuth Cell, Samuth Creature, Samuth Tribe, ...) is its own
  hosted session with its own saved world on the galaxy server, hosted by a player in it (the first to arrive). Players
  progress on their own: finishing a stage moves a player out of one era's session into the next era's (hosting it if
  nobody is there). Both of the owner's cases work this way: (1) the joiner hosts Samuth's Creature era while the
  creator idles in its Cell era and joins later; (2) "start at Tribe" enters Samuth's Tribe era while others play its
  other eras.
* Players see each other within an era; across eras they are parallel timelines of the planet (visitors across eras
  maybe later, like Space visitors on a Creature planet, never one shared simulation).
* Work it needs: a stage change today takes the whole session with the host; here each player changes era on its own
  (leave one session, enter the next through the galaxy server).
* Unlocks: tied to the world's creator, an away creator blocks everyone's later starts; Spore's own rule (a player starts
  any stage it has reached itself) does not. Moving on to the next stage by playing is always open.
* Status (2026-11-19): this is how a party moves on to the next stage (owner on 6Y and 7C: "they should be in the
  same stage with other players in their stages"). Not built yet: the galaxy server keeps one session per planet
  name (step 4, part 1, #server-party-progress-design).

## Seeing other eras: visitors (2026-11-17)
<a id="server-era-visitors"></a>

Owner, 2026-11-17: "How can we resolve The Catch. I feel like that is a high priority for something like this. Seeing
civilizations as a creature would be an amazing feat. Or seeing vehicles drive by you."

* Eras stay separate sessions (one stage per planet surface in a game), but show each other as visitors: each era's
  host sends a light stream of what moves in it (players, vehicles, ships, where cities and buildings stand) to the
  galaxy server, which passes it to the other eras of that planet; each game draws them as look-alikes.
* Space ships over any planet: as UFOs. Spore's Creature, Tribe and Civ stages fly UFOs (drama events), and the mod
  already mirrors them (K_UFO, BUG-0558; UfoGfx_SetModel takes any ship model key). Likely easy.
* Earlier eras in later ones: a Creature-era player as a creature in a Tribe or Civ game (those stages have wild
  creatures; the creature puppets already work there). Likely easy.
* Later eras in earlier ones, the hard part: vehicles and buildings in a Creature or Tribe game. Those stages have no
  vehicles or cities, but they draw models on simple objects (fruit, eggs, rocks, ornaments: cInteractiveOrnament);
  cVehicle::Load sets a vehicle's model through its locomotive's SetModel (vt+0x94) with the design's key. Unknown:
  whether such an object accepts a vehicle or building model in the Creature stage and can be moved each frame. A
  spike decides it.
* Look-only at first: no fighting across eras, creatures do not react to cities (interaction is its own design later,
  maybe for a few cases). Each era built its world on the same ground, so a city can stand where another era has a
  nest.

## Era visitors spike: vehicles and buildings in a Creature game (2026-11-17, feat/lineages-alpha)
<a id="server-era-spike"></a>

Decision 5S A. `coop.visit spawn <instance> <type> <group> [drive|stand] [lift m] [axes 0|1|2]` (alpha,
mods/coop/era_visitors.cpp): a ship made by the game's SpawnUFO (type 7, the Creature stage's drama ship) with the model
key of another era's creation, placed ahead of the avatar and moved every frame.

* ✔ The Creature stage draws them: a Civ vehicle (0654CCD8/24682294/40646200, one of the Civ test world Abber's NPC
  vehicles) and a city hall (067C4C2F/2399BE55/40636200) appeared in Samuth's Creature stage with their own models
  (creations::ensure_model found them in a Creature profile: they are the game's own designs), and a house driven in a
  12 m circle moved with it. Keys from a Civ world: `coop.civ bmodels` (buildings), `coop.civ` (vehicles).
* ✔ Orientation: with local Y forward and Z up (axes 0) or X forward (axes 2) the vehicle stands upright; the
  avatar's own rotation (axes 1) does not suit it.
* ~ Height not solved: they float about 6 m over the ground. The ship's update keeps its hover height over the terrain
  whatever the mod places (its read position stays 6 m up, mZoomAltitude +0x768 set to the ground did not change it),
  because the mod's frame runs after the game's frame is drawn and the ship's update runs again before the next one.
  Next ways: a carrier object that does not fly (an ornament or other static prop of the Creature stage with the model
  set on it, SetModel vt 0x94 as cVehicle::Load uses), or the hover clamp in the planet UFO controller
  (UFOController_Planet 0x1060E00), or placing from a hook that runs after the game's update and before drawing.
* ? The thin vertical bars under the city hall: its own columns or the abduction ship's beam (type 7); to check with
  another carrier.
* ✔ Height solved with a prop (decision 5T B, 2026-11-17, alpha commit "Era visitors: stand them on the ground as
  props"): the default carrier is now a plain ornament, noun 0x18C88E4 (the object of a city's decorations,
  docs/re/space.md#space-city-ornaments): `CreateInstance(0x18C88E4)`, AddRef, `cOrnament_SetModel` 0xC6F910 with the
  key, then the spatial object's vt 0x94 SetModelKey(key). The truck stood on its wheels and drove its 12 m circle,
  its position radius equal to the ground radius (505.3 / 506.0); the city hall stood on the ground. Removed with
  `DestroyInstance` and Release. `coop.visit spawn ... [prop|ufo]` keeps the UFO carrier for comparison.
* ✔ `cOrnament_SetModel` alone made an invisible prop (the object and its position were there, nothing drawn): the
  model only drew after SetModelKey (spatial vt 0x94), as cVehicle::Load sets a vehicle's model.
* ✔ A prop moves with SetPosition / SetOrientation (spatial vt 0x38 / vt 0x3C, as worldobj.cpp moves the city
  decorations); SetPositionAndOrientation (vt 0x44) moved it once and later calls were ignored.
* ✔ The bars under the city hall are its own columns: they stand on the ground under the prop as well.
* ~ Our props are left out of saves by a save-strip filter (era_visitors.cpp); not tested with a real save yet.
* ? Not checked: collision (creatures walked through the truck's spot in the screenshots, as expected of a prop),
* Status (2026-11-19): drawing is proven (a Civ truck driving and a city hall standing in a Creature game). Not built:
  the stream between the eras' hosts that would show the real players of the other eras (step 4, last part).
  how a prop looks at a distance (level of detail), and many props at once.

## The galaxy server keeps each planet's world (2026-11-17, feat/lineages-alpha)
<a id="server-world-storage"></a>

Decision 5Q A. Code on the alpha branch: server/galaxy/main.cpp, net/galaxy.h (protocol 2), mods/coop/galaxy_client.cpp,
world::galaxy_pack / galaxy_install (mods/coop/world.cpp).

* The server keeps one file per planet in its worlds folder (`--worlds <dir>`, "worlds" by default), read back when it
  starts: a planet with a stored world stays on the list with nobody on it ("Samuth (0, no host, saved)").
* Upload: the planet's host saves its game and sends the save folder with the designs, packed as a share packs it
  (world::galaxy_pack): 60 s after a new planet is hosted, then every 5 minutes (`coop.galaxy every <s>`), on
  `coop.galaxy save`, and before it leaves (`leave` and `disconnect` wait for it, 3 minutes at most). Only the planet's
  host may store its world.
* Download: the next first player on the planet gets the host role with the stored size, downloads the world
  (FETCH), writes it into a co-op save slot and loads it (world::galaxy_install, the player's own load, from the galaxy
  menu or from a stage), and hosts it once it plays there (READY). A player entering while the host is not hosting yet
  is told to enter again in a moment.
* Transfers: chunks of 1100 bytes, an ack each, 96 in flight, sent again after 1 s (XferOut / XferIn in net/galaxy.h,
  shared by the server and the game).
* ✔ Runtime (TITAN, local galaxy server, QA games 9 and 8): game 9 hosted Samuth (a new planet), its first upload
  stored 4025 KB in 8.2 s (4 s of it the save); `leave` saved again (4031 KB) and then left. Game 8 entered from the
  galaxy menu: downloaded the world in under a second, wrote it into a new save slot, loaded it and hosted it 4 s after
  the load; its creature stood 1 m from where game 9's creature was when it left. Game 9 joined game 8 as a guest
  through the relay and played (the party card, the partner 5 m away). Game 8's own 5-minute upload ran while it
  hosted. The server restarted with the world still listed; game 9 then downloaded it (0 chunks resent) and hosted it
  again.
* Found and fixed on the way: a send stamp up to 2 ms ahead of the clock made fresh chunks count as overdue (a third to a
  half of a download went twice); BUG-9252 (live): a guest leaving a stage for the galaxy menu waited forever for
  intro bars nobody showed.
* ~ A world hosted by another player changes hands: the new host plays its own creature in it (the world's player
  species becomes the new host's, BUG-0508), so the planet's creator, back as a guest, was treated as a new player there
  (no creature of its own: the first-creature editor). Each player keeping their own creature on a stored planet is
  open (decision 5U).
  Done since: #server-own-creatures (5U A).
* ~ Seen with BUG-9252, not fixed: the mod's "Bringing your nest" loading screen stayed over that first-creature editor
  until its 120 s limit, and the galaxy menu's splash text ("galactic adventures", the rating line) was drawn over it.
* Not yet: the hand-over while players stay (today everybody leaves when the host leaves, and the next to enter hosts
  the stored world), the galaxy menu showing the server's planets, eras.
  Done since: the hand-over (#server-handover, #server-warm-handover) and the menu (#server-galaxy-menu-built).
  Eras: not built (#server-eras).

### Each player keeps their own creature on a stored planet (2026-11-17, feat/lineages-alpha)
<a id="server-own-creatures"></a>

Decision 5U A. Code on alpha: mods/coop/creations.cpp (switch `creations.host_own_record`).

* Before: a guest already played its own creature per world (SporeCoop\worlds.ini of its profile, BUG-0345), but the
  host always played the world's player species. On a stored planet that is the last host's creature, and the host
  recorded nothing: the creator, back as a guest, was a new player there (the first-creature editor).
* Now: the host records the own creature it plays in a world (one of its own creations; also after evolving), and a
  host whose world has another player's creature plays its recorded own one (the existing avatar swap, as for a host's
  choice in the Co-op window).
* ✔ Runtime (TITAN, local galaxy server, QA games 9 and 8, Samuth): game 9 hosting with its record played its own
  creature 2920607A ("own avatar is now 2920607A"); game 8 took the stored planet over and played its own 2920807D, not
  game 9's; game 9 joined it as a guest with no editor and played 2920607A after the world loaded; game 9 took the
  planet back from storage and played 2920607A again. With the record cleared, the hosting game wrote it again by
  itself ("the host's own creature in world D296F25C is 2920607A (recorded, 5U A)").
* ~ Test setup: game 9's own Samuth save had another creature as its player species, left by earlier co-op tests, so
  its record was set by hand (`coop.creations worlds set creature 2920607A`) to start from "the creator has a record".
* ? Not checked yet: a host evolving in the editor while it plays a recorded creature (the record should follow the new
  creature); the host's nest, family and pack (they stay the world's species, the last host's creature); the record
  lives on each player's PC (worlds.ini), so another PC is a new player there until the server keeps the records.
* Alpha only: on the live branch the same rule would change which creature a host plays in a world it once received
  from another host; to be decided before it goes to live.

### The hand-over: the next player takes the planet over (2026-11-17, feat/lineages-alpha)
<a id="server-handover"></a>

Decision 5V A. Code on alpha: server/galaxy/main.cpp, mods/coop/galaxy_client.cpp, net/galaxy.h (protocol 3).

* When the planet's host leaves (it saves and uploads the world first) or times out (20 s silent) and players stay, the
  server makes the next player the host: it gets ROLE host with the stored world, downloads it, loads it and hosts it.
  The others get HOSTMOVE: their session ends and they wait on the planet ("another player takes it over, wait a
  moment"); once the new host is READY the server sends them ROLE guest and they join it (the world arrives as for any
  join). A new host without a stored world hosts the world it plays. A waiting player gives up after 6 minutes.
* ✔ Runtime (TITAN, local galaxy server, QA games 9, 8 and 7 on Samuth): game 9 hosted, 8 and 7 joined; game 9 left:
  its world was stored, game 8 hosted it 11 s later, game 7 joined game 8 at once and played there 25 s after that
  (its own creature, the world received and loaded); game 8 saw game 7 at 56 m with its party card. Game 8 killed: the
  server timed it out after 20 s, game 7 downloaded the stored world and hosted it 11 s later.
* ~ A host that crashes loses what happened on the planet since the last upload (5 minutes at most; the test lost
  about 2 minutes). Shorter uploads while players are on the planet would narrow it (each upload is a save, about 4 s).
* ~ Every hand-over is a reload for everybody on the planet (the new host loads the stored world, the others the new
  host's share): about 25-40 s on TITAN.

### The galaxy server in the server list (2026-11-17, feat/lineages-alpha, decision 5Y A)
<a id="server-list-galaxy"></a>

Owner: "I was thinking we just add it as a native server in the server list at the top. Maybe give it a larger size to
show it's a special server?" Code on alpha: mods/coop/coop_ui.cpp (the connect page), coop.cpp (galaxy_info).

* The server list's first row, two rows tall, pinned above the network games: "Galaxy Server / Online worlds - port
  47900"; connected: the galaxy's name and "N planets, M players online". A click fills in the saved galaxy address
  and port 47900; Join with port 47900 connects to the galaxy server instead of a game ([ui] galaxy_address remembers
  it). The galaxy menu then shows the server's planets (#server-galaxy-menu-built).
* ✔ Runtime (TITAN, QA game 8): the row in the window, the address typed, Join: "TITAN test galaxy, 1 planet, 0
  players online"; the galaxy menu showed Samuth; Play on it with nobody hosting downloaded the stored world and hosted
  it 5 s later (the gap left open in #server-galaxy-menu-built).
* ~ Seen: a host whose creature is dead keeps uploading, and the next player loads the planet into the death caption
  ("You were killed in battle"). Uploads could wait while the host is dead.
* Not yet: leaving the galaxy from the window (today: quitting the game, or `coop.galaxy disconnect`).
  Done since: the Co-op window's Join button reads "Leave Galaxy" while connected (coop_ui.cpp).

### The owner's first alpha test and its fixes (2026-11-17, feat/lineages-alpha, decisions 6A A, 6C A)
<a id="server-alpha-test-1"></a>

The owner played on TITAN in a separate alpha copy (its own profile) through the galaxy server, the MSI as the second
player. Their findings and what came of them (fixes on alpha, tested on TITAN QA games with the visible galaxy server):

* "I pressed join, but the co-op menu did not change ... I can't leave the server": a galaxy Join now closes the window
  once connected ("pick a planet in the galaxy menu"); while connected, Join reads "Leave Galaxy" and leaves. ✔ Tested.
  (The first click after the window opens only gives it the focus: a second click selected the galaxy row.)
* "Leaving to main menu should have stopped the host": leaving the stage for the menu as a planet's host uploads
  Spore's own quit-save and hands the planet over. ✔ Tested ("You left Samuth for the menu", stored, left).
* 6A A, "the world always saved if the host leaves, even using Alt+F4": Stop Hosting saves first (✔ stored, then the
  guest hosted it 10 s later); Alt+F4 and the window's X are held in the input hook until the planet is saved and left,
  then the window closes (✔ 8 s); saves every 2 minutes while others are on the planet (✔ 2 min 8 s apart), at a quiet
  moment, never with the host's creature dead. Not caught: a crash or a killed game (the last save counts), Spore's own
  Quit button from the pause menu (not checked).
* "make sure it doesn't freeze the game anymore": Spore's save pauses the game time, waits 1000 ms on its clock on
  purpose, writes (0.7-0.9 s) and shows the "game saved" box whose OK re-arms saving (docs/re/creature.md#creature-save-steps).
  The mod now moves that clock on: the save lands about 1.1 s after it starts instead of 1.8 s (✔ measured); the write
  itself stays. The box cannot be skipped (it would block every later save); the mod confirms it as it shows.
  ~ superseded (2026-11-17, owner: "seamless for uninterrupted gameplay"): the galaxy now uses Spore's silent save
  (message 0x689C9B9, docs/re/creature.md#creature-save-steps): no pause, no busy cursor, no wait, no box; only the
  write (one 0.7 s frame). A due save waits up to 90 s for a moment the player does not feel it (the game paused, or 8 s
  without input), then a quiet one. ✔ Tested: no box, the next host loaded the silently saved world. Left: the 0.7 s
  write itself (Spore's own save on its main thread); a world streamed to the server instead of Spore saves would
  remove it (the long-term model).
* "It didn't give me a creature creator for the first time connecting. It loaded some other creature": a new host
  with no creature of its own on a stored planet gets the creature editor once its creature is alive and the game idle;
  the creation it saves becomes its creature there. ✔ The editor opened after the respawn. Not checked: saving the
  creature (a fresh QA profile showed Spore's own "Register your game" sign-in screen over the editor, which the tests
  leave alone).
* "Can we get rid of the top right text": the green Pack line is off by default (live too; [coop] pack_line=1 shows it).
* "Editing my creature forced a migration / reconnect for MSI, then the creature creator again while fighting": the MSI
  had no creature of its own and played the host's; the host's evolution changed its body and its "migration" opened
  the host's editor for "final touches". On a galaxy planet each player evolves on its own: the three roles below.
* "it took a loading screen and leaving / returning" (the hand-over): decision 6B C, #server-warm-handover.
* Seen in the tests, not fixed: Lineages' campaign seats ("First gets no seat, the campaign's 4 seats are taken"): a
  stored planet keeps every player who ever played it in its 4 seats, so new players get none; the galaxy needs seats
  by who is on the planet now. A planet stored with its host dead loads with the death screen (fixed for new uploads).

### "Host" means three roles today (owner's note, 2026-11-17)
<a id="server-three-roles"></a>

Owner: "Once we allow people to leave a party, this will be more prevalent. Currently a lot of our mentions of a Host
actually mean the 'Party Leader' at the moment." (after a galaxy test where the host's evolution changed a partner's
body and the partner's migration opened the host's editor)

* Agreed. The code's "host" is three roles at once: the simulation host (whose game runs the world and decides hits
  and deaths), the party leader (the group's decisions: the evolution vote, migrating together, the stage change) and,
  on the galaxy, the planet's host. On a galaxy planet strangers share one simulation host but are in different
  parties (or none): each player evolves and migrates on their own, and a party's leader decides only for that party.
* Splitting them is a design step of its own (who leads a party, what a party decides, what stays the simulation
  host's); until then the galaxy should not apply "everybody follows the host's evolution / migration" rules to players
  who are not in the host's party.

### Seats: 16 players per planet, difficulty per party (owner, 2026-11-18)
<a id="server-seats-16"></a>

Owner: "Seats just mean how many players are allowed on a planet. We can set it to 16 per planet. Difficulty scales on
the individual party. Meaning they get their own relations with creatures, their own damage modifiers, their own goal
progression, etc. compared to other parties."

* Today "seat" is two things in the code: the player cap (the network's kMaxPlayers 4, the Co-op window's Player Slots
  1-4) and Lineages' founder seats, given once per player identity and kept with the planet (party.cpp: a planet's
  seats fill up with everyone who ever played it, so a stored planet refused a fifth new player), each seat owning its
  player's nest (Creature), village (Tribe) and nation (Civ); the founder files hold 8.
* For the galaxy: up to 16 players on a planet at once; a founder seat held by a player who is not on the planet can be
  given to a newcomer (the player gets one back when they return and one is free).
* Per-party difficulty (each party its own creature relations, damage modifiers and goal progression) is the
  three-roles split's party side (#server-three-roles): today these follow the session's host and its difficulty
  scaling.
* The host's cost grows with each partner (20-30 KB/s upload each): 16 players on one host's home connection is the
  risk to measure first (decision 6E).

### Any player count: one ceiling, the planet's count a setting (2026-11-18, feat/lineages-alpha, decision 6E B)
<a id="server-scalable-players"></a>

Owner: "It needs to be scalable too. Setting it to 17 shouldn't cause any issues."

* One ceiling in the code, net::kMaxPlayers 64 (protocol.h; ids 0..63, 0xFC and up stay markers), and
  founders::kSeatCeiling 64 (founders.h): every table, mask and loop sizes itself from these (the Civ seat masks are
  bitsets, the Tribe village count, the Cell loops over the players present, the view centres, the creature tables, the
  pass-through list, the guest packs). How many a session takes is a setting: all.max_players (2..64) for co-op, the
  galaxy server's `--planet-players <n>` (2..64, 16 by default) for its planets, sent in WELCOME (an older server: 4).
* A galaxy planet's host hosts with that count: a stored planet saved with fewer Lineages slots is raised to it ("This
  planet takes 16 players now (it had 5 slots)"); a full planet refuses the next player at the server ("Samuth is
  full (5 players): pick another planet or try again later").
* Difficulty stays one party's: the per-seat extras count 4 seats at most (CampaignRules::kScaledSeats), so 16 players
  on a planet play at a 4-seat party's difficulty until each party has its own (#server-seats-16).
* The Co-op window: Player Slots is a field with - and + (1..64, the four buttons before), the lobby's four player rows
  scroll (arrows at the Players heading), Apply takes up to 64; `coop.seats` and `coop.host` take any count.
* Fixed on the way: a galaxy guest that left sent its goodbye after LEAVE, the server had already ended its relay and
  the host kept the place for the 30 s timeout (a full planet refused the next player). The goodbye goes first now.
* Tests (TITAN, QA copy, a local test galaxy server, test worlds folder): with `--planet-players 5`: 5 players in the
  host's session (5/5, "Player slots: 5", takeover 115% as at 4), the sixth refused as full; a guest left and the sixth
  joined 4 s later in its place (before the fix: refused "session is full" for 30 s). With the default 16: 10 games on
  one planet (players=10/16, ids 1..9, past the old internal limits of 4 and 8), the sync check OK with all nine,
  every player's cell shown to every other, no crash. Unit tests: cell, civ seats, tribe seats, tribe late, party food,
  npc, space, the founders part of win_tests (a 17-seat raise keeps a 4-seat difficulty).
* Found, not fixed:
  * The host's upload. In the Cell stage the host sent 163 MB in the minute the 10 players settled: 128 MB of it the
    Cell snapshot (MSG_CELL_STATE, about 240 KB/s a guest), 28 MB world chunks. A home connection does not carry that
    for 15 partners: the snapshot has to go to each player for its own area only (or the galaxy server fans it out)
    before 16-player planets are practical. Measure Creature next (20-30 KB/s a partner before).
  * The party cards on the right of the screen stack past the top with 9 partners (8 cards at most, at 800x600 they
    go off the screen). On a galaxy planet they should show the own party only (#server-three-roles).
    Owner 2026-11-18: "The party cards are going to be limited to 4 because we'll limit parties to 4 members only.
    They wont overfill." A party is 4 players at most; the cards show the own party.
  * Player colours repeat after 8 (find_core.h: 8 colours, player 9 shares one).
  * Founder seats are still given once per identity and kept (party.cpp): 64 different players over a planet's life
    fill them; reusing an absent player's seat is still open (#server-seats-16).

### The host's upload with many players: what costs and what would cut it (2026-11-18, decision 6F)
<a id="server-host-upload"></a>

Measured in the 6E B test (#server-scalable-players), Cell stage, all players near each other, the QA games at 10 FPS:
the host's Cell snapshot (MSG_CELL_STATE) cost 100-240 KB/s a guest (4 guests: 25-53 MB a minute; 9 guests: 128 MB).
It is already per guest: each guest gets the cells within 120 m of its own centre, at most kSendCap 900, 10 times a
second (cellsync.cpp send_cycle, BUG-0295, BUG-0809). Each cell is a full 63-byte record every time: index, resource
instance, three model keys, level, size, position (3 floats), velocity (2), rotation (a quaternion, 4), hp, flags;
about 33 of those bytes never change while the cell lives. So "only what is near" is done; the cost is the record and
the rate. Theory for a 16-player planet (15 guests), not measured:

| | Each guest | Host upload, 15 guests | Galaxy server |
|---|---|---|---|
| Today | 100-240 KB/s | 1.5-3.6 MB/s (12-29 Mbit/s) | relays it |
| A: a compact snapshot (the fixed part of a cell once, then about 12 bytes: position and velocity as 16-bit offsets from the guest's centre, one angle, hp, flags; far cells less often) | about 20-40 KB/s (5-8x less) | 0.3-0.6 MB/s (2.4-4.8 Mbit/s) | relays it |
| B: the galaxy server copies one stream to the players of one area | today's size | one stream per group of players near each other: 100-240 KB/s when all are together, today's 3.6 MB/s when all are apart | sends 1.5-3.6 MB/s a busy planet |
| C: A, then B | about 20-40 KB/s | 20-40 KB/s per group | 0.3-0.6 MB/s a busy planet |

A helps co-op too (live, 4 players: 0.3-0.7 MB/s today to about 0.1 MB/s) and costs no server bandwidth; B only helps
players who stand together and moves the cost to the server; C is the MMO shape (the server carries the fan-out) on
top of A. Creature, Tribe and Civ are not measured at this size yet (Creature: 20-30 KB/s a partner at 4 players).

### The compact Cell state (2026-11-18, feat/lineages-alpha, decision 6F A)
<a id="server-compact-cell"></a>

Owner: "6FC" (A first, then the galaxy server copies the stream). Part A: MSG_CELL_STATE_C (mods/coop/cell_compact.h
has the format), sent by the host to every guest that announces net::CAP_COMPACT_CELL; an older guest gets the classic
MSG_CELL_STATE. Switch: `coop.fix cell.compact_state off` (the classic state to everyone); counters: `coop.pool compact`.

* A cell's fixed part (resource instance, model key, level) goes in a full record when the guest has not had it for
  2 s or it changed; otherwise position and velocity as 16-bit steps from the guest's centre, the rotation in 4 bytes,
  the size in 2 (log2), hp and flags: 22 bytes, 39 with the fixed part, 4 for "still there".
* Cells outside a 40 x 32 box about the guest (its view is about 15 x 11) are sent 2 times a second instead of 10, a
  "still there" in the other cycles (the guest goes on along the cell's velocity, as between any two states). A dying
  or dead cell is always sent.
* A chunk holds about 60 cells instead of 14 (fewer packets too).

Measured (TITAN, QA copy, a local test galaxy server, 6 games on Samuth in Cell, all players near each other, two
minutes each way in the same session):

| | Bytes a cell | Cells a guest a cycle | Each guest | Host, 5 guests |
|---|---|---|---|---|
| Classic | 63.6 | 281 | 179 KB/s | 894 KB/s |
| Compact | 15.5 | 393 | 61 KB/s | 305 KB/s |

Per cell 4.1 times smaller; at the compact window's 393 cells the classic would have cost about 250 KB/s a guest. The
players stood together, so 73 % of the cells were near (sent 10 times a second); spread out, more of them are far and it
costs less. A 16-player planet with everyone together: about 0.9 MB/s (7 Mbit/s) from the host instead of 3.7 MB/s.
Guests skipped 0 to 4 of about 550 000 records each (a lost full record, back within 2 s). The sync check: 112 checks,
0 mismatches (36 momentary differences, gone on the 3 s recheck). Unit tests: cell_tests (packing round trips, the far
cells' turns). Not tested: an older guest against a new host on alpha (the capability decides; live has no compact
state yet), Creature / Tribe / Civ (their own messages, not changed).

Next (6F part B): the galaxy server copies one stream to the players of one area.

### The galaxy server copies the Cell state to players together (2026-11-18, feat/lineages-alpha, decision 6F B)
<a id="server-fan-out"></a>

Owner: "Start part B" (6F C: A, then B). The host groups the guests whose centres are within 40 m of each other (a
chain: cell_compact::kGroupDist) and sends each group one compact Cell stream (the cells within 120 m of any member, at
most 900, the near box of any member at 10 a second); the galaxy server copies it to the group's players.

* net/galaxy.h kFan 'F': host -> server: u8 n, n x u16 slot, then u8 message type and body; server -> each player:
  u16 the host's slot, the type and body. The server takes it only from the planet's host and sends it only to the
  planet's players; the game takes it only from its planet's host, as a guest through the relay, and only
  MSG_CELL_STATE_C (coop.cpp gx.fan_in): it is then dispatched as the session's own message from the host.
* A guest gets the group stream only when it announces net::CAP_FAN_IN and CAP_COMPACT_CELL; alone, or an older game,
  or not through a galaxy: its own stream as before. A full record goes out when any member needs it (each member's
  "had it" time is kept). Copies refused by the galaxy client fall back to one stream each.
* Switch: `coop.fix cell.fan off` (one stream per guest); counters: `coop.pool compact` (groups), `coop.galaxy status`
  (copies sent / got / refused), the server's status line (copies in / out KB/s).

Measured (TITAN, 6 games together on Samuth in Cell, 2 minutes each way in the same session):

| | Host's Cell upload, 5 guests | Each guest gets | Galaxy server sends |
|---|---|---|---|
| One stream each (cell.fan off) | 192 KB/s | about 38 KB/s (its own area) | relays it |
| Group streams (cell.fan on) | 75 KB/s | about 76 KB/s (the group's area) | about 380 KB/s |

2.6 times less from the host; a guest downloads the group's area (about twice its own here). 4.8 guests a stream on
average (1218 guest updates in 252 streams). Sync check 72 checks, 0 mismatches; copies 0 dropped; the guests' "full
record not had yet" count did not grow during the run (0 to 3 new per guest); no crash. With A, against the classic
state at the same cells: about 7 times less host upload in all.

For a 16-player planet with everyone together: one stream from the host (about 75-110 KB/s, the same for 5 or 15
guests), the galaxy server sends it 15 times (about 1.1-1.7 MB/s a busy planet). Players apart: one stream per group.
Not done: Creature, Tribe and Civ (their own messages: measure next), a group's stream for the session's other
unreliable messages.

### Creature with many players: measured (2026-11-18, feat/lineages-alpha, decision 6G A)
<a id="server-creature-many"></a>

Owner: "6GA". TITAN, QA copy, a local test galaxy server (worlds folder out/galaxy/worlds_6ga), DuoQACreatureHost's
Samuth (Creature, `coop.load Game0 Samuth`) hosted on the galaxy, guests entering it.

* 10 Creature games on one PC do not run: the host fell to 3 FPS, guests to 1 FPS (the QA cap is 10), three guests
  timed out of the session (10 -> 7) and one out of the galaxy server. The numbers of that run are frame-bound and not
  used. Cell ran 10 games at 10 FPS (#server-scalable-players); Creature needs the MSI or fewer games per PC.
* Measured with 5 (host + 4 guests at 7-9 FPS, one steady minute, no world transfer):

| Message (host out) | KB a minute | Note |
|---|---|---|
| MSG_PLAYER_STATE (6) | 697 | every player's state to every other through the host: 4 x 4 streams, about 8 a second each |
| MSG_WOBJ (54) | 425 | world objects |
| MSG_LIBRARY (71) | 279 | creation pictures (once per new creation) |
| MSG_WSTATE (62) | 184 | world state rows |
| MSG_NEST (52) and the rest | 203 | |
| all | 1788 (30 KB/s, 7.5 KB/s a guest) | |

* Projection to 16 players (15 guests), same frame rate: everything but the player states grows with the guests
  (about 4.5 KB/s each: 68 KB/s); the player states grow with the square (each guest gets every other player's state
  through the host: 15 x 15 streams, about 160 KB/s at 8 states a second, more at a real frame rate). The player states
  are the Creature stage's wall, as the Cell snapshot was Cell's. Fix (6F B's mechanism): the host sends each player's
  state once and the galaxy server copies it to the others (linear: about 11 KB/s for 15 at 8 a second).
* A join costs the host the whole world again (MSG_WORLD_CHUNK, 3.2-4.2 MB a guest): 9 joins about 38 MB. The galaxy
  server already stores the planet (every 2 minutes and on leave): a joiner could load that copy from the server.
* Found on the way (not fixed):
  * A guest new to a Creature planet gets the first-creature editor (as designed, BUG-9208): a fresh profile first gets
    Spore's "Register your game" screen over it; the creature needs a mouth and a name before it saves (owner
    2026-11-18: "A creature needs a name / mouth to be saved"). The QA run skipped registration on the test profiles
    ("Don't prompt me again") and saved a mouth on the starting body with a dice name.
  * A guest with no saved game at all first starts a new Spore game (its world 'SaveGame', the new game's creature
    editor) instead of the host's world; quitting that editor drops it to the galaxy menu while it stays in the
    session.
  * Saving a new creature sends the host's world to that guest again and reloads it; with several new players the
    guests went in and out of editors for about 15 minutes.
  * A game the galaxy server timed out (1 FPS) kept "connected" and never said HELLO again: every ping answered "Not
    connected to the galaxy server: connect first". The client should connect again on that answer.
  * Sync check: 5 mismatches, all during that churn (guests loading: "missing on the guest", 2 NPCs apart); none in
    the steady 5-player minutes. No crash.
* Not measured: Tribe (TITAN cannot carry it at this size either: the MSI or 4-5 games).

### Player states copied by the galaxy server, and reconnecting (2026-11-18, feat/lineages-alpha, decision 6H A)
<a id="server-fan-states"></a>

Owner: "6HA. Also utilize MSI to ensure an even spread of computing power. In a real scenario, only one instance would
be running on someone's computer."

* The host sends each player's state (its own and each guest's it forwards) once for every guest with
  net::CAP_FAN_IN; the galaxy server copies it to them (6F B's kFan frame). Session::send_unreliable_fanned: two or
  more such guests and a galaxy host (Callbacks::fan_out, coop.cpp galaxy_fan_out), else one to each as before. A guest
  takes a copied state through Session::host_fan_in (the same checks as a state from the host: the roster, never its
  own id). Switch: `coop.fix net.fan_states off`.
* Reconnecting: a game the galaxy server does not know (timed out, the server restarted) gets REFUSED
  net::galaxy::kNotConnected on its next ping; it now leaves the planet and says HELLO again (at most every 5 s;
  `coop.galaxy status`: reconnects).
* Test spread over both PCs: TITAN the host and 3 guests, the MSI 3 guests (its own test games at 800 x 600, over
  Tailscale to the galaxy server on TITAN), Samuth in Creature, every game at its normal 9-10 FPS. Host out, a minute
  each way in the same session:

| | Player states from the host | Host upload in all |
|---|---|---|
| One to each (net.fan_states off) | 38-42 KB/s | 58-63 KB/s |
| Copied by the galaxy server | 9 KB/s (to the server) | 30-33 KB/s |

  4.3 times less for the states with 6 guests, and it now grows with the players instead of their square: about
  21 KB/s for 16 players at this rate instead of about 250. Each MSI game got about 28 000 copied states (2-5 refused
  while it joined). Sync check 107 checks, 0 differences, 0 mismatches; no crash on either PC. The galaxy server
  restarted under the 7 games: all 7 connected again by themselves within 25 s (each then off the planet, to enter
  again).
* Still open from #server-creature-many: a joiner's world from the galaxy server's copy (6H B), Tribe measured.
  The joiner's world from the server's copy: done since (#server-stored-join).

### Joining from the galaxy's copy; Tribe measured and shared (2026-11-18, feat/lineages-alpha, decision 6I A)
<a id="server-stored-join"></a>

Owner: "6IA. I want you to get as far as you can until you reach a decision that absolutely needs my attention."

**A joiner loads the planet from the galaxy server.** A guest's ROLE carries the stored world's size, crc and age
(net/galaxy.h; the server lets any player of the planet FETCH it now). When the stored copy is under 2 minutes old, the
guest downloads it from the server, loads it, then joins the host with the receipt stored_receipt(crc, world). The host
takes it (no world sent) only when that copy is its own last upload, nothing was saved since (world::
current_save_write, the same rule as BUG-0235's resume: the guest has the host's newest save) and it was packed under
150 s ago; else it sends its world as before and stores the planet again within 15 s. After any normal share the host
stores the planet within 20 s, so the next joiners find a fresh copy. Switch: g_fix_stored_join (galaxy_client.cpp).

* Test (TITAN host + 3, the MSI 3, Samuth in Creature): the first guest joined the usual way, the host stored the planet
  20 s later; the next 5 (2 on TITAN, 3 on the MSI) loaded the galaxy's copy: "accepted 5, refused 0", the host sent no
  world to any of them (about 20 MB of its upload saved); sync check OK with all 5 (0 mismatches). The MSI's downloads
  through Tailscale resent about 700 of 3800 chunks (a relayed link) and still took 20 s.

**Tribe measured** (7 players spread over TITAN and the MSI, every game at 9-10 FPS):

* DuoQAJiffHost's Jiff Peanutbutter is worn out: its tribe's hut gets destroyed while the test idles (the game-over
  caption "Your tribal hut has been destroyed!"); two guests then mismatched (15 extra members, 31 about 400 m apart).
  Not used. DuoQAClythHost's Clyth: sync check 51 checks, 0 mismatches.
* The host sent 163 KB/s for 6 guests (27 KB/s a guest): MSG_TRIBE_STATE 80 KB/s, MSG_NPC_ANIM 18, MSG_TRIBE_BUILDINGS
  16, MSG_TRIBE_TOOLS 10, the rest less. MSG_TRIBE_STATE, the Tribe NPC_ANIM batches, BUILDINGS, TOOLS, RELATIONSHIPS
  and EQUIPMENT are the same bytes for every guest: they now go through Session::send_unreliable_to_guests, which uses
  the galaxy's copies (6H A's path; the switch `coop.fix net.fan_states off` covers them too).
* With the copies, same session, a minute each way: host 168-200 KB/s (off) against about 67 KB/s (on: 39 KB/s in the
  session, 28 KB/s up to the galaxy server, which sent out 154 KB/s). Sync check 78 checks, 0 mismatches; no crash on
  either PC. At 16 players: about 125 KB/s from the host instead of about 450.
* Still per guest (by its area): the creature NPC states (MSG_NPC_STATE_C), npc.cpp's NPC_ANIM, MSG_WOBJ (about 31 KB/s
  of the 39 for 6 guests). Grouping players who stand together (as the Cell state, 6F B) would cut them.
* Seen: a join's MSG_CREATION burst (each player's creations sent on to everyone, 3.4 MB in the minute 5 players
  joined).

### Parties of 4: the owner's rules (2026-11-18, decision 6J)
<a id="server-parties"></a>

Owner, answering 6J:

1. "A party is formed by opening the connection menu and right clicking a name to 'view creature, invite to party,
   whisper, add friend'." The menu "should be similar to all native menus"; "allow a player to rightclick another
   player's creature / nametag directly".
2. The leader: "The person who invited or the person you joined. The leader can then right click in the connection
   menu (F9) or the party panel to set another player as a leader."
3. "When the party leader leaves, the longest standing member receives the Leader status. Our normal continuation plan
   proceeds (You continue from the progress you gained, the original leader will catch up to the new progress.)"
4. Across stages: "We can deny cross stage invites for now, and we can think about a non-linked party where everyone
   can help eachother, but they each have unique progression like single player."

Earlier (2026-11-18): a party is 4 players at most; its cards show the own party only; difficulty, creature relations,
damage modifiers and goals are each party's own (#server-seats-16).

### Parties of 4: built (2026-11-18, feat/lineages-alpha, decision 6J)
<a id="server-parties-built"></a>

The owner's rules (#server-parties), on alpha:

* **The book** (mods/coop/parties.h, tests/party_tests.cpp): every player in one party, 4 at most; an invite (any
  member may invite; the inviter's party keeps its leader, an inviter alone leads); the invited player accepts or
  declines within 60 s; a yes leaves the old party; the leader hands the lead to a member; a leader who leaves: the
  longest standing member leads; no invite across stages (a menu, an editor or a load counts as no stage). A plain
  co-op join puts the player in the host's party ("the person you joined"); on a galaxy planet a player starts alone.
* **The messages** (parties.cpp, parties_net.h): the host keeps the book and sends it (MSG_PARTY kTable, reliable) on
  every change; guests ask (invite, answer, leave, lead), the host offers the invite to the invited player and answers
  the asking one. MSG_WHISPER: a line only the other player sees, through the host. Console: `coop.party [status |
  invite | yes | no | leave | lead]`, `coop.whisper <id> <text>`, "/w <name> <text>" in the chat line.
* **The right-click menu** (coop_ui.cpp show_player_menu, coop.cpp player_menu_input): on a lobby row of the Co-op
  window (F9), a Party card, a partner's name tag or plate, or near its creature on the screen. Items: View Creature
  (the game's Sporepedia card), Invite to Party / Make Leader (your party, when you lead), Whisper (the chat line gets
  "/w <name> "), Add / Remove Friend; on your own row: Leave Party. An invite shows the same panel with Join Party /
  Decline. The panel is in the Co-op window's look (its panel colour, a 1 px edge, the name in gold, blue item rows).
  The presses are the mouse hook's (mouse::set_player_menu_input, before the UI scale remap): the menu works under
  the game's input lock (a caption, the death screen) too.
* **Friends**: this profile's list in <coop dir>\friends.txt (identity key and name); a friend's name is light blue in
  the lobby.
* **Party cards**: the own party's only (a galaxy planet's other players have none).
* Found while building it: Spore hit-tests the Co-op window before a sibling window drawn over it (the menu sits
  inside the Co-op window while it is open); buttons made outside the Co-op window drew in the disabled look (the menu
  items are filled rows instead); a hot reload (`mod.reload coop`) leaves the old build's windows on the screen (tests
  restart the game).
* Tested (TITAN, 3 games on a galaxy planet, Creature): a right-click on a lobby row opened QA7's menu; Invite reached
  QA7 as the Join Party / Decline panel (QA7 on the death screen); Join Party made QA8 + QA7 a party led by QA8 (the
  host's book the same); a right-click on QA7's name plate in the world opened its menu; Add Friend wrote the list and
  the lobby showed QA7 in light blue; Whisper filled the chat line, the line reached QA7 only; the host handed the lead
  to QA8, QA8 left the session, the host led again (the longest standing); Decline told the inviter. Not checked by
  eye: the Party cards with two in a party (the deaths hid them); View Creature from the menu (the same call as the
  card's portrait click).
* Not yet: per-party difficulty, creature relations and goals (#server-seats-16); the parties survive a hand-over
  (a new host starts everyone alone); the Co-op window's "Players who can join" setting still reads 4 on a galaxy
  planet (the planet takes the galaxy's count).
  Done since: the galaxy server keeps the parties across hand-overs (#server-parties-galaxy); difficulty and relations
  per party in every stage (#server-party-difficulty, #server-party-relations, 2026-11-19). The goals and progress per
  party are step 4 (#server-party-progress-design).

### Parties kept by the galaxy server, kick, the friends list's place (2026-11-18, feat/lineages-alpha, decision 6K)
<a id="server-parties-galaxy"></a>

Owner (with "6KA"): "Let's also allow kicking from party. We need a location for a friends list to allow faster
partying and whispering. (Make a mockup of where it'd be) We need to allow partying in the galaxy menu. Parties
shouldn't be reset/disbanded if the host changes. Ensure it's clean, well spaced, and polished."

* **The galaxy server keeps the parties** (server/galaxy/main.cpp, parties::BasicBook<uint16_t> by player slot;
  net/galaxy.h PARTY, PARTYVIEW, PARTYNOTE, WHO, PLAYERS). A player is in a party from the moment it connects, in the
  galaxy menu too, and stays in it across planets and hand-overs. Requests name the other player by its identity
  (identity::known on any machine); each member gets its party's view (slot, identity, name, planet, stage) and an
  invite in it. A game that drops and comes back (a new slot, the same identity) goes back into its party for 5
  minutes. The stage each plays comes with its requests (the cross-stage rule). Without a galaxy (plain co-op) the
  session's host keeps them as in 6J.
* **Kick**: the leader's "Remove from Party" (menu, `coop.party kick`, `gkick <name>` in the galaxy menu): the player
  plays alone, and hears "<leader> removed you from the party".
* **The galaxy menu** (until its panel is built, decision 6L): `coop.party online`, `ginvite <name>`, `gkick <name>`;
  the invite panel (Join Party / Decline) shows over the galaxy menu too.
* Tested over TITAN (the host, QA8) and the MSI (one game): QA8 invited the MSI game in the galaxy menu, the MSI's
  Join Party made them a party there; both entered Samuth (the view showed both on it; QA8's menu on the MSI player
  offered Make Leader and Remove from Party); the host left, QA8 took the planet over and the party stayed whole;
  QA8 removed the MSI player (alone, leading its own party). The server logged each step.
* **The friends list's place**: mockups images/mockup_friends_A_galaxy.png, mockup_friends_A_world.png (a "Friends &
  Party" panel at the right edge, F8 anywhere, a Friends button under Co-op in the galaxy menu: the party, friends
  online with where they play, Invite and Whisper on each row, the galaxy's players) and mockup_friends_B_coop.png (a
  Friends tab in the Co-op window's lobby). Decision 6L.

### Each party its own progression: the plan (2026-11-18, decision 6K A)
<a id="server-party-progression"></a>

Research (read of the code and docs/re, 6K A). Today one game simulates the planet and every player shares its
relations, difficulty and goals:

* **Relations**: one table per game (cSpeciesRelationshipManager, the map at mgr+0x20: relation, impressed, killed per
  species, docs/re/creature.md); GetRelationship 0xBA40E0 is always called with a creature pair (the player's creature
  and the NPC), so the host can answer per player; BUG-0462 gives every player the host's answer today. Events
  (ApplyEvent 0xBA49E0: impress, kill, attacked) name the victim, not the actor. The host sends its table to the guests
  (MSG_SPECIES, social.cpp) and already can send one to a single player.
* **Difficulty**: CampaignRules by the campaign's seats (4 at most since 6E B); enemy health is one factor per NPC,
  damage is scaled per hit in the host's damage filter where the hit player is known (npc.cpp, tribe.cpp, civ.cpp,
  cellsync.cpp). Space already counts the players in one system (squadron::party_rules): the same idea per party.
* **Goals**: the progress bar, the DNA pace (gains divided by all players in the world), the progress debt, the goal
  rewards (once per session), extinction, nests, collected parts and the stage end are one per world on the host;
  spendable DNA, the own creature and editor, health and rescue are already per player.

Plan, in steps (each one a build and a test on TITAN and the MSI):

1. **Difficulty per party**: the seats that scale a hit are the hit (or hitting) player's party size; health factors
   become damage factors per party (one NPC, many parties: the attacker's damage is divided by its party's health
   factor instead of the NPC's health changing). DNA pace and progress debt counted per party.
2. **Relations per party**: the host keeps one table per party (the game's own table stands for the host's party);
   GetRelationship answers for the party of the player creature in the pair; impress / kill / attack events are
   credited to the acting player's party (the social win and the hit already know the player); each party's guests
   get their party's table (MSG_SPECIES per party). Goal rewards paid once per party.
3. **Extinction and nests per party** and **NPCs that pick targets by relation** (the target picker is distance only
   today): needs decision 6M.
4. **Stage progress per party** (the bar, the stage end, leaving for the next stage) with the three roles (party
   leader, planet host, simulation host): needs decision 6N.

Owner's answers (2026-11-18): **6M B** extinction stays one per planet (step 3 keeps the game's own extinction; only
NPC targeting by relation is left in it); **6N A** a party that ends a stage moves on to its own next-stage world while
the others stay (step 4: the stage end becomes the party's, the planet's host party included).

**The menu's buttons** (owner 2026-11-18: "The buttons look so bland. Is that what Spore looks like? Why aren't we using
our own button layout and appropriate colors?"): the items are our own button art now (data/ui/lineages_button_<colour>
_normal / _hover, the Co-op window's buttons), drawn as images (a real button outside the Co-op window showed its
disabled image): gold joins (Invite to Party, Join Party), red leaves or removes (Remove from Party, Leave Party,
Decline), blue the rest, grey Remove Friend; the hover image under the pointer; the panel 224 px wide, 31 px rows.

Hardest parts: crediting events to the right party (the game reports the victim), one NPC seen by several parties
(its AI keeps targets between ticks), extinction being a world object, and the host's single progress bar.
Feasible soon: steps 1 and 2.

### Hand-over without a reload: research (2026-11-17, decision 6B C)
<a id="server-warm-handover"></a>

Owner, after the first test's hand-over: "it took a loading screen and leaving / returning. This would interrupt a lot of
people if there's 10 people on a single planet." Decision 6B C: research the in-place promotion (the new host keeps
its loaded world and becomes the host; the others reconnect to it without a world transfer). Findings below.

* ✔ Experiment (TITAN, 3 QA games, the galaxy server's `--warm-handover`: the new host keeps the world it plays):
  game 9 hosted Samuth, 8 and 7 joined; game 9 left; game 8 was hosting 3 s later with no loading screen and played on
  (no stage change); game 7 rejoined it and still reloaded (a full world share, as for any join). So the new host's half
  works mechanically today; the others' half needs a Creature "resume" path.
* What the promoted game carries over (a code inventory, file:line in the report of 2026-11-17; most host-only work is
  decided per frame from the role and starts by itself once the role is Host):
  1. Its creatures: as a guest it destroyed every native NPC it updated (npc.cpp update filter) and showed the old
     host's NPCs as proxies (no AI, often in mod-made herds, health overridden). The session end destroys the proxies;
     `away_locals` rebuilds the living ones in nest herds as natives only when its snapshot is under 2 s old, bodies and
     own-herd proxies are lost, and the nests refill at their respawn time. Seen: 45 proxies and 56 natives removed as a
     guest, 2 tracked NPCs right after the promotion (the herds themselves intact).
  2. Species keys: a guest plays the host's creations under mod keys of its own (0x7D aliases, its own creature under
     0x7C) and drops the host's packages at the session end: creatures already built keep keys that exist only on that
     PC, and they would go into the new host's saves.
  3. Host-only state the guest never had: every player's pack, the creation bundles for late joiners, the progress-debt
     ledger, the knock-out / revive coordination, per-player nests (seat 0 is the host), the session settings (the
     promoted game builds its own from its coop.ini).
  4. "The host is player id 0" everywhere (net/session.cpp): the promoted game becomes 0 and the others get new ids;
     everything keyed by the old host's object ids (NPC ids, egg serials, world-object serials) is invalid.
  5. Joining without a world transfer exists only in Cell (a receipt the host minted for that world, epoch, file and
     food total); a Creature version needs a lineage id every guest keeps (the planet and its host's generation, from
     the galaxy server) and a Creature fingerprint (save name, stage epoch, no reload or save since). The proxies,
     world objects, wsync and egg mirrors already reconcile a guest's loaded copy with a host's state.
* Plan (the warm hand-over, galaxy only, behind a switch):
  1. Promotion: when a guest is told to take a planet over, its session ends the "promotion" way: the living proxies
     become natives at once (away_locals without the 2 s limit, bodies kept as bodies), pack proxies stay real pack
     members, the creation packages and aliases stay registered, mod-only species keys are mapped back before its first
     save; then it hosts in place (3 s).
  2. Reconnect without reload: the galaxy server gives every hand-over a generation id; a guest that keeps the same
     planet loaded rejoins the new host with it, and the new host skips the world share for it (states re-sync as for a
     late joiner).
  3. Per-player state that only the old host had (packs, ledgers, party seats, settings): kept by the galaxy server
     per player and per planet; part of the three-roles split (#server-three-roles).
* ✔ Steps 1 and 2 built (2026-11-18, alpha, protocol 5): the server gives each hand-over a token (ROLE, HOSTMOVE);
  the new host, still playing the planet's world, keeps it: every living proxy goes on as its own NPC
  (`npc::promote_now`, through away_locals), its guest session ends and it hosts in place; the others, still in that
  world, rejoin with the token as their resume receipt and get no world (`galaxy::accept_warm` in the join). A player
  who left the world (a menu, a load) gets the stored world and the normal share. Tested on TITAN, three QA games:
  the host left; the next player turned 32 of 34 proxies into NPCs and hosted 3 s later with no stage change; the
  third rejoined it with "no world sent" and showed its 24 NPCs, no loading screen; a second hand-over the same way
  (34 of 36); a fresh player then loaded the world the twice-promoted host had saved: no creature or model warnings,
  53 NPCs (the original world about 54).
* ~ Not covered yet: step 3 (packs, the progress-debt ledger, revive tracking, party seats stay with the old host);
  the 2 proxies per hand-over whose species has no nest herd in the world copy (they are dropped); the mod-only species
  keys (no warnings were seen on a fresh load, not proven for every creature); stages other than Creature (Tribe and
  Civ have their own guest mirrors).

### Owner's idea: deleting one's creature on a planet from the online galaxy menu (2026-11-17)

Owner: "when a player is on the Online Galaxy Menu, we should allow them to delete their creature on the world still.
Deleting their creature will let them rejoin that planet with a new one."

* Good, and cheap with 5U A: the player's creature on a planet is the record in its worlds.ini; deleting it clears the
  record (today `coop.creations worlds set creature 0` in that world), and the next join of that planet opens the
  first-creature editor (BUG-9208).
* Also to remove with it: the player's own nest and family in the stored world (BUG-9210 made one per player); left
  there, they would stay as a nest of nobody. The planet's host would remove them (the player asks through the galaxy
  server).
* Goes with the galaxy menu work (#server-galaxy-menu): a "Delete my creature here" button on each server planet.

### Owner's idea: refreshing nests on a persistent planet (2026-11-17)
<a id="server-nest-refresh"></a>

Owner: "We'll need to rotate the nests to refresh them so they're never stale and all extinct or something ridiculous.
We'll have like a 6 hour rotation or something. Maybe every 3 hours they refresh."

* Agreed that a persistent planet needs it: with many players hunting, species go extinct and the planet empties.
* Recommended shape: not one clock for the whole planet, but each extinct species coming back a set time after it went
  extinct (3 hours, a server setting). Hunting a species out still counts (Spore's own "make extinct" goals and the
  predator path keep working), and no planet stays empty. Done by the planet's host (the nests respawn the game's way:
  `coop.nest respawn` already drives it), timed by the server's clock so it holds across hand-overs.
* Players' own nests (BUG-9210) are another rule: removed after their player has been away for days, not hours (the
  player may come back), or when the player deletes their creature there (#server-handover, the owner's delete idea).
* Not built; a decision when the persistent planets get their rules.
* Owner 2026-11-19 (6Z): when a party moves on to the next stage its nests stay on the planet as wild animals of its
  species, and the refresh rotates them out after a while ("keep the old nests around as wild animals, then they
  rotate after time has passed"). Still not built: part of step 4 (#server-party-progress-design).

### The galaxy menu: what decides its planets (2026-11-17, feat/lineages-alpha, decision 5W A)
<a id="server-galaxy-menu-spike"></a>

Owner's choice 5W A: the galaxy menu showing the server's planets. A first look on TITAN (QA game 9, the menu, memory
read through SporeKit; no Ghidra on this PC) before building it:

* ✔ A save slot (Games\GameN) is one galaxy: its stars.db, planetRecords.pkp and GGEUserData.dat (a DBPF with one
  compressed record that names the slot's saved games, "Samuth" in it) and one .spo per saved planet (Samuth and
  Diplomavil share Game0 of the QA profile).
* ✔ The menu's planet set (GGE_PlanetSet_ptr, 0xBC-byte entries) had 8 entries: 2 with a save (+0xAC = 1: Diplomavil,
  and Samuth with its player species 2920607A at +0x3C) and 6 empty stars to start a new game; +0x80 the position.
* ✔ A planet of another galaxy copied into the slot (Abber.spo of the Civ test profile, into a copy of Game0, shown with
  `coop.menuslot`) did not appear: the menu lists the saves its galaxy's files name, not the .spo files in the folder.
* So "the server's planets in the menu" needs one of: (1) one galaxy on the server (every planet made in the server's
  galaxy, the server keeping its stars.db, planet records and menu list, merged from the hosts' uploads), or (2) the
  mod filling the menu's entries itself (names, creatures, positions) and catching the click on one (enter it on the
  server, then load). Both need the menu list's format and the entry panel's fields: reverse engineering that goes
  fastest with Ghidra (not on this PC; the Ghidra projects are on the Linux PC, docs/05-re-notes.md 5.2b).
* Meanwhile a simple "Online" page in the Co-op window (the server's planets, who is on each, Enter, Delete my
  creature) would make the galaxy server playable without console commands.

### The galaxy menu shows the server's planets (2026-11-17, feat/lineages-alpha, decision 5X B)
<a id="server-galaxy-menu-built"></a>

Owner's choice 5X B: Ghidra on TITAN first (docs/05-re-notes.md#re-notes-ghidra-windows), then the menu. The reverse
engineering: docs/re/space.md#space-menu-entries. Code on alpha: mods/coop/galaxy_menu.cpp (`coop.gmenu`).

* While the player is connected to a galaxy server, the mod rewrites the menu's entries just before their panels are
  made: each server planet gets an entry (its name, its stage icon, its creature when this game has the creature), the
  player's own saves turn into empty stars while online, and Play on a server planet enters it on the server (the
  galaxy client joins its host, or downloads the stored world and hosts it). The menu refreshes when the server's list
  changes (polled every 10 s in the menu). Protocol 4: an upload carries the planet's stage and its host's creature.
* ✔ Runtime (TITAN, local galaxy server): game 9 hosted Samuth; game 8, on the galaxy menu, connected: one star became
  "Samuth, Creature Stage" in Spore's own planet panel (Play, the bin, the date); Play joined game 9 and loaded game 8
  into Samuth.
* Found on the way: (1) Play checks "<name>.spo" before it loads ("Bad or Missing Saved Game"): the mod takes the Play
  step, not the load; (2) the step comes again every frame until the menu is left: entering the planet again made the
  client leave and join in a loop (fixed: never while already on that planet).
* ? Not checked yet: Play on a planet nobody hosts (download and host from the menu), a planet whose creature this game
  lacks shows "?" (the creature's design is not sent with the list), more server planets than the menu's entries (6-8
  here), the bin on the panel (it could become "Delete my creature here"), the stage card and creature row that
  flashed under the joining screen.

### Owner's question: Civ and Creature eras talking to each other (2026-11-17)

Owner: "Once we load Civ stage while someone's in creature stage, it shouldn't be too crazy to make them communicate I
assume."

* Seeing each other: not too crazy, agreed. Both eras are sessions on the same planet through the same galaxy server;
  each era's host sends a light stream (players, vehicles, ships, where buildings stand, a few times a second) that the
  server passes to the planet's other eras, and each game draws look-alikes (the 5S spike and 5T B: vehicles and
  buildings stand and drive on the ground in a Creature game). The ground is the same planet, so positions carry over.
* Interacting is the hard part: a tank shooting a creature, a creature attacking a city. The two eras are separate
  simulations with different contents (nests and herds where the other era has cities and roads), so every interaction
  needs its own rule written by us (who decides the hit, what the other era's game does with it). Look-only first;
  then a few chosen interactions.
* Chat between eras is easy (the server relays it).

## Questions for the owner
<a id="server-questions"></a>

Asked in docs/ROADMAP.md as 5H (what goes first) and 5I (how strict the checks are). Who runs the parts of a planet
nobody is near waits for phase 0's answer.


## The Social Menu built (2026-11-18, feat/lineages-alpha)
<a id="server-social-menu"></a>

6L A (owner 2026-11-18: "I like A, can we also add a button next to the Party panel for this menu? We'll name it the
Social Menu and it'll have the two person silhouette icon. Don't make the gold buttons have black text. Keep it white."
and "Make this look more native."). Built and seen working on TITAN + MSI (QA 8 joining the MSI's Samuth):

* The Social Menu: F8 anywhere, the Social button (two person silhouette: the Co-op item's own `data/ui/coop_icon.png`, owner 2026-11-18: one look for both) left of
  the Party tab in a stage, and a Social item under Co-op in the galaxy menu. Sections: the party (n / 4), friends
  online and offline (friends.txt in the coop folder), the galaxy's other players online (up to 5). Rows: the lobby's
  row box, a green / grey swatch, the name (gold: you, light blue: a friend) and where they play; Invite and Whisper
  buttons (white text on our gold / blue art), Leave on your own row in a party. A text that does not fit ends in
  "..." ("Samuth" alone when "Samuth  -  Creature" is too long).
* The look is the Co-op window's own frame (glow, hex top band, panel colour, close button) drawn at the panel's size
  (docs/08-ui.md#ui-native-frame-copy). The partner name tags and locator arrows are not drawn under the panel or the
  player menu (`coop_ui::ui_covers`).
* Whispers reach any player of the galaxy through the galaxy server (control WHISPER = 24, by identity).
* 6O, the Party panel smaller: the cards and tab are 72% of their old size (`[ui] party_scale`), the small font for
  the card name and title; the Social button follows the tab's height.
  Ported to live Lineages (feat/lineages-47, decision 6Q A, 2026-11-18): the same `party_compact()`, nothing else.
* Seen on the way (not fixed): the joiner loaded the MSI's stored world while the MSI's creature had just died, and got
  the "killed in battle" screen too; joining a planet still pushes the host and the other guests into the creature
  editor (they press the check), the known forced editor.

## Steam to avoid port forwarding (research, 2026-11-18, feat/lineages-alpha)
<a id="server-steam-research"></a>

Owner 2026-11-18: "Let's also investigate the idea of connecting with Steam to avoid port-forwarding."

* What the game has: Spore's `steam_api_orig.dll` is an old Steamworks (SteamNetworking001 only: no
  ISteamNetworkingSockets / Messages, the relay network Steam games use now). The old P2P interface works through
  Steam's relays too, but it needs the game launched by Steam under AppID 24720 (not ours) and a logged-in Steam on both
  PCs; GOG players cannot use it at all.
* A newer steam_api.dll loaded beside the game's: a crash risk (two Steam clients in one process), a licence grey area
  (Valve's SDK terms for a mod of a game we do not own), still AppID 24720 and Steam only.
* Without Steam: most home routers let two PCs talk directly once both have sent a packet to each other at the same
  time (UDP hole punching; RFC 5128 measured 80-90% of pairs; Tailscale reports the same). The galaxy server already
  sees both players' public addresses: it can hand them to each other, the two games punch through, and the pairs that
  fail keep the galaxy server's relay (it already relays today). No port forwarding for anyone; works for Steam and GOG.
* Recommendation: hole punching through the galaxy server with the relay as the fallback (ROADMAP 6P A); a one-day
  Steam spike only if many pairs still fail.

## A migration takes the party, not the planet (6R A, 2026-11-18, feat/lineages-alpha)
<a id="server-migrate-party"></a>

* ✔ The "editor on join" seen in the alpha tests was BUG-9163's rule from the small co-op ("a migration started by any
  player: everyone edits and migrates together"): on a galaxy planet one stranger's migration sent every player of the
  planet into the creature editor, and the host's editor then held a joiner's world. Now (BUG-9254) only the migrating
  player's party follows it; a direct session is still one group. Checked with fix on and off on TITAN + the MSI.
* Still open: a host in its own editor (its own choice) holds a joiner's world until it is back in the stage; the
  joiner could take the galaxy server's stored copy meanwhile (6I A's path).
* Name plates: a hidden -NoShare- tag left its space ("Pandize " drew off centre, "Pandize  2"); the spaces go with
  the tag now (live too). The Party card is 80% of its old size (72% read cramped) and its name is measured in the
  card's own font (it sat off to the right).

## A planet's new host makes its first creature in the start editor (BUG-9265, 2026-11-19, feat/lineages-alpha)
<a id="server-host-start-editor"></a>

* Owner 2026-11-19: "We can't let them make a level 5 endgame creature upon joining any player." BUG-9263 (live) gave
  joining guests the Creature stage's start editor; a galaxy planet's new host without a creature there still got the
  Creator with every part. It gets the start editor too now (`coop.fix creature.host_start_editor`).
* ✔ The start editor (0xE46C381E) works inside a running Creature world when its request does not name the galaxy
  menu at +0x1C: the start palette, the red cross and the check both go back to the world, and the saved creation is
  adopted (creations::host_first_tick). Its budget there is the running game's DNA (570 on Samuth), not the 145 it
  has from the galaxy menu.
* ✔ The Creator (0x465C50BA) with no creation key and no parts list (+0x94) is what unlocks every part; with the
  evolution tag and the collected parts it is the game's own limited evolution editor.

## Difficulty per party, step 1: Creature (BUG-9266, 2026-11-19, feat/lineages-alpha)
<a id="server-party-difficulty"></a>

* Step 1 of #server-party-progression, Creature on a galaxy planet: a hit, an NPC's bite, hunger and the Creature award
  use the rules of the party involved, its players on the planet as the seats (alone: the game's own difficulty; 4 and
  more: a 4-seat party's). `coop.fix creature.party_difficulty`; direct co-op is unchanged.
* The host learns each guest's galaxy party from the guest (MSG_PARTY kGxParty, parties_net.h): the galaxy server
  sends each game its own party only. A guest could claim no party (easier fights for itself): part of 6S.
* ✔ An NPC keeps the game's own health; a player's hit is divided by its party's health factor (one NPC, several
  parties). Measured: alone 1.00, a party of 2 0.87 (1/1.15); fix off: the planet's x1.45 health for any party.
* `coop.party status` prints "party sizes here: <id>=<size>" (on the host with each guest's reported party).
* Next: Cell, Tribe and Civ on galaxy planets (their extras still read the planet's rules), the progress debt and the
  goal rewards per party, then step 2 (relations per party).

## Difficulty per party in every stage, relations per party (6V A and B, 2026-11-19, feat/lineages-alpha)
<a id="server-party-relations"></a>

* Owner 2026-11-19: "Cell, Tribe, and Civ being planet wide difficulty makes no sense for a party based system";
  6V A and B.
* B (BUG-9266): Cell bites and meals, Tribe village fights and Civ rival fights and takeovers use the party of the
  player involved; what belongs to nobody (Cell predator groups, the rival tribes, the NPC nations' armies) follows
  the largest party on the planet. Built; seen at runtime in Creature only (the test galaxy has no Cell or Civ planet).
* A (BUG-9267): each party keeps its own species relations. The host's game table is its own party's; another
  party's records are kept by the host, and an event of that party's player (a hit, a kill, a social win) runs the
  game's own ApplyEvent on that party's record. Extinction stays one per planet (6M B). Each guest gets its party's
  table. ✔ Kills: a stranger's kill counted only in its party's record and its own game. Not yet: the records are not
  saved (a hand-over keeps the new host's party's only), and NPCs still pick targets by distance (step 3).
  Done since: saved (BUG-9270, #server-party-relations-saved), NPCs by party (BUG-9269, #server-party-ai).
* Still one per world: the shared DNA pool, the Cell food total, the progress debt and the stage end (step 4, 6N A).

## NPCs treat each party by its own relations (6W B, 2026-11-19, feat/lineages-alpha)
<a id="server-party-ai"></a>

* BUG-9269, the 6K plan's step 3: an NPC near a partner ticks with that partner as the player (BUG-0016); for a partner
  of another party the NPC's species record and its herd's ally flag are that party's for the tick. ✔ A species the
  guest's party made aggressive (an eaten egg) attacked the guest (19 hits in 30 s) while the host's party's record
  stayed neutral; with `coop.fix social.party_ai off` the same NPC left the guest alone.
* Relation 5 is "aggressive", not "extinct" (CORRECTION in docs/re/creature.md 8.1): a party's 5 stays its own.
* BUG-9268 (same day): a galaxy host whose game reloads its planet (the Tribe seat rebuild: every newcomer on a stored
  Tribe or Civ planet) handed the planet over; it stays the host now (`coop.fix galaxy.reload_stays`).
* Cell difficulty per party seen in game on a new Cell test planet (Cellfour): predators keep the game's health, the
  host's bites scaled only in a party (BUG-9266).

## Each party's relations saved with the planet (6W C, 2026-11-19, feat/lineages-alpha)
<a id="server-party-relations-saved"></a>

* BUG-9270: `<save>.spo.duo-relations` next to the planet's save keeps each player identity's party's records and who
  owned the game's own table. ✔ A stranger's party got its records back in a new session, and when a player of that
  party hosted the planet later its game played by that party's records while the old host's party got its own.
* A party that splits keeps the records in each member; a new party takes the first member's it finds (no merge).
* Left for the 6K plan's step 4 (decision 6N A): the shared DNA pool, the progress bar, the Cell food total and the
  stage end are still one per world.

## Security pass: identity and address (6S, 2026-11-19, feat/lineages-alpha)
<a id="server-security-pass"></a>

* BUG-9273, protocol 6: an identity is its first key's (the server keeps it in <worlds>/identities.txt: nobody else
  can be you, a second copy of a profile is refused), and a new address proves itself with the server's cookie before
  it gets a slot. tools/tests/galaxy_server_check.py covers both.
* Left for the owner (gameplay): who may host an empty planet and store over its world (S6; today anyone who enters
  first). Also open, not gameplay: encryption (the key travels in clear UDP: someone on the path could read it).
* ~ superseded (2026-11-19): S6 is not an owner's choice (owner: "No one is replacing the persistent planets"). A
  planet's upload is its running save on the server, the same planet going on. What is left is protection against
  a broken or cheating host's save: step 4, part 6.

## Step 4, each party its own progress: the design (2026-11-19, feat/lineages-alpha)
<a id="server-party-progress-design"></a>

Decision 6N A (owner 2026-11-18): a party that ends a stage moves on to its own next-stage world while the others stay.
What exists (research, 2026-11-19):

* Spendable DNA is each player's already (2F B, BUG-9162, docs/re/creature.md#creature-personal-dna). What is shared on
  a planet is the earned progress: cPlayer +0x10F0, the bar, its brain levels and the stage end. The host's game owns
  it; a guest's gain moves the host's bar (dna_sync_raw) and MSG_DNA gives every guest the host's earned total.
* The brain level also sets the NPC herds' level: the host's level for every herd (docs/re/creature.md, the brain
  level notes): the world has one level, as it had one health factor before BUG-9266.
  ~ superseded (2026-11-19): CORRECTION, badly put: the brain level changes no creature. It decides which herds are
  awake: Relevance_Update disables a herd whose brain range (+0x120, +0x124) does not hold the player's level
  (docs/re/creature.md, the relevance notes); today the host's level decides for everyone.
* The stage end is one game's: the bar full, the game offers the next stage and loads the next-stage save of the same
  planet. On a galaxy planet that takes the host (and its world) away from everybody.

Plan (built in this order once the decisions below are answered):
1. The bar per party: the host keeps each party's earned total (as it keeps their relations, BUG-9270, saved next to
   the planet); a guest's gain goes to its party's total; MSG_DNA gives each guest its party's; brain levels follow.
   NPC herds: decision 7A.
2. The stage end per party: when a party's bar is full its leader is offered the next stage; (CORRECTION 2026-11-19,
   owner on 6Y: no new planet per party: the party joins the next stage's shared world, decision 7C; private planets
   only on request in the galaxy menu. Superseded:) accepting copies the planet as the party left it into a new galaxy
   planet (decision 6Y), the party's games load it in the next stage and
   the party hosts it; the old planet goes on for the others, with that party's traces as decision 6Z says.
3. The planet's owner (S6, decision 7B): who may store over a planet's world on the galaxy server.

Why not before the decisions: the bar per party alone lets a party that is not the host's fill its bar with no way to
move on (step 2 is what happens then), and both steps write the galaxy's planet list.

CORRECTION (2026-11-19, feat/lineages-alpha): the plan above missed the eras design of 2026-11-17 (#server-eras) and
the era spike that proved a Creature game can show a Civ truck and a city hall (#server-era-spike). The owner, on 6Y,
7C and 6Z: no new planet per party; a party that ends a stage joins the other players of that stage on the same planet;
a private planet only when a player asks for one in the galaxy menu; old nests stay as wild animals and rotate after a
while. 7A and 7B were withdrawn (badly worded, not gameplay choices). The plan as it stands:

1. **Eras on the galaxy server**: a planet holds one session per stage (Samuth Creature, Samuth Tribe, ...), each with
   its own stored world and its own host (the first player in it). The galaxy menu shows the planet once, with the
   stages that have players. Built on the existing planet sessions (one more key: the stage).
2. **The bar per party**: the host keeps each party's earned total (saved next to the planet, as the relations are,
   BUG-9270); a guest's gain goes to its party's total; MSG_DNA gives each guest its party's; brain levels follow. The
   herds near each player follow that player's own brain level (the game's rule, run per player as the view centres
   already are, npc.view_centres); no creature's level is changed.
   CORRECTION (2026-11-20, feat/lineages-alpha): nothing to build: every wild herd's brain range is [0,5], so no herd
   wakes or sleeps by the brain level (BUG-9284, docs/re/creature.md#creature-herd-brain-ranges).
3. **The stage end per party**: a party's bar full, its leader gets the game's own next-stage offer; the party leaves
   the Creature era together (1X A) and enters the planet's Tribe era. The first party there makes it from its own
   save through the game's own stage change; a later party joins it, and each player's village is founded at its own
   nest's spot through the existing founding (ROADMAP decision 6, tribe.multi_founders, the late-joiner village of
   BUG-9150), as Spore turns a nest into a village. To check in game: that a later party's nest positions carry over
   into a Tribe world made from another party's save.
4. **What stays behind** (6Z): the party's nests and family stay in the Creature era as wild animals of its species;
   the nest refresh (#server-nest-refresh, a server clock) brings extinct species back and rotates such nests out
   after a set time (a server setting).
5. **Visitors across eras** (5S, 5T): each era's host sends a light stream (players, vehicles, where buildings stand)
   that the server passes to the planet's other eras; the props of the spike draw them. Look-only first.
6. **Planet saves stay safe** (was S6 / 7B): the server keeps the last few uploads of each planet and refuses one
   that is not that planet's world (another save's id, a much smaller file), so a broken or cheating host cannot
   wipe a planet.

Order: 1, 2 and 3 together make the stage end work; 4 with 3; 5 and 6 after. No gameplay decision is open for it.

## Step 4 built: eras on the server, each party's own bar, safe planet saves (2026-11-19, feat/lineages-alpha)
<a id="server-eras-built"></a>

Parts 1, 2 and 6 of #server-party-progress-design.

* Eras (BUG-9276, protocol 7): a planet session is keyed "<planet>|<Era>" (Samuth|Creature, Samuth|Tribe; net/galaxy.h
  era_key and friends). Everything that worked per planet (hosting, storage, hand-overs, joins, the relay) works per
  era. An upload names its era and the server refuses a world of another stage, so a host whose game moved on cannot
  turn the planet's Creature era into a Tribe world. `coop.galaxy enter Samuth` takes the era of the stage the game
  plays, or in the menu the planet's only era (several: it names them); messages and the galaxy menu say
  "Samuth (Creature)". Worlds stored before eras move to their stage's era when the server starts.
* Safe saves (part 6): the server keeps the 3 saves before the current one beside a planet's world (<file>.1 .. .3; to
  undo a bad save, copy one over the world while the server is stopped) and refuses a save of another planet over a
  stored one.
* Each party's bar (BUG-9277, switch `creature.party_bar`): the host keeps every other party's evolution bar; a
  guest's gain and social win fill its own party's bar, each guest gets its party's bar; saved next to the planet
  (<save>.spo.duo-progress). `coop.dna bar` shows the party bars.
* ✔ Runtime (TITAN, QA 8 host, QA 9 another party, the test galaxy): the stored worlds became eras; "Samuth" from the
  menu entered Samuth (Creature); the guest's bar went to its party's 0, its gain of 60 filled only its bar (the
  host's stayed 1078); with the switch off the same gain moved the host's bar. Two uploads left .world.1 and .2.
* Not yet: the herds near a player waking by that player's own brain level (part 2's last piece: the game's
  Relevance_Update uses the host's level for every herd), the stage end per party (part 3), the nest refresh (part 4),
  visitors across eras (part 5).

## Step 4 part 3, the stage end per party: how it works (2026-11-19, feat/lineages-alpha)
<a id="server-party-stage-end"></a>

The pieces it reuses: the warm hand-over (6B C: a game keeps its loaded world and hosts it in place, the others rejoin
with the token and get no world), the co-op stage end inside one session (the vote, the editor, the Tribe start with
a village per founder seat from its own nest, tribe.multi_founders), and the late join into a Tribe world (a joiner
keeps its species and gets its village, decisions 4, 5 and 14).

1. **The split.** A party's leader asks for the stage end (the game's own stage-end editor request, with its party's
   bar full). Its game holds the request and sends MOVEON to the galaxy server. The server takes the party's members
   on that era out of it into a crossing session of their own (key "<planet>~<party id>|<Era>": never stored, never
   listed, only that party may enter): the leader hosts it with the world it has loaded (warm), the members get MOVED
   with the token and rejoin the leader with no world sent. The players who stay keep the era: if its host was in
   the moving party, the next of them takes it over warm (the hand-over the server does when a host leaves).
2. **The stage end.** Once the members are back with the leader (or 60 s passed), the leader's game sends the held
   request again: the co-op stage end runs as in a party's own game (vote, editor, the next stage's start, a village
   per member from its own nest).
3. **Arriving.** In the next stage the leader sends ARRIVE: if the planet's next era has no host, the crossing
   becomes that era (the server renames it; the members stay in the session, nothing reloads) and the leader's next
   upload stores it. If the era has a host, every member gets ROLE guest of that host: they join it as late joiners
   (one load; each keeps its species and gets its home through the late-join path).
4. **What stays behind** (part 4): the party's nests stay in the old era as wild animals; the nest refresh rotates
   them later.
* Built (BUG-9278, protocol 8, switch `galaxy.party_stage_end`): the server's MOVEON / MOVED / ARRIVE
  (tools/tests/galaxy_server_check.py: 11 checks), the game's side (galaxy_client.cpp: MOVED, no uploads from a
  crossing, ARRIVE 10 s after the next stage is up; coop.cpp: the Creature stage-end request on a galaxy era goes
  through the party's leader and is held until the crossing is ready, then runs again through the vote).
* ✔ Runtime (TITAN, QA 8 hosting Samuth (Creature), QA 9 a stranger's party of one as its guest): QA 9's stage-end
  request sent MOVEON; QA 9 went into "Samuth~2|Creature", closed its guest session and hosted the crossing 3 s
  later with no reload; QA 8 kept hosting Samuth (Creature) alone; the held request went on in the crossing.
* ? Not seen yet: the native Creature -> Tribe start inside a crossing and the ARRIVE that follows. The test request
  (`coop.vote stageend`) only opened an evolution editor, and on the QA copy `coop.dna add 3000` filled the bar
  without the brain upgrades that make the game offer the Tribe stage (no caption, History's check closed History).
  Next: a Creature test save at the last brain level (the campaign fixture's way) to run the whole crossing.
* Only the Creature stage's end is routed so far; Cell -> Creature and Tribe -> Civ ends keep today's session-wide
  behaviour on a galaxy planet.
* ✔ Runtime, the host's party (TITAN 2026-11-19, QA 8 hosting Samuth (Creature) with brain level 4 and a full bar, QA 9
  another party as its guest): QA 8's stage-end request (`coop.vote mate`) moved it into "Samuth~1|Creature" (hosted
  3 s later, no reload); QA 9 took Samuth (Creature) over in place (32 of its copies of the creatures went on as its
  own, no reload) and swapped in its own party's relations (BUG-9270) and bar (BUG-9277: "this planet's bar was
  another party's (60): this host's party plays its own (60)"), the first live check of that swap.
* ~ Still not seen: the Tribe start inside a crossing. `coop.vote mate` runs the egg scene's message (0x4471C60, the
  mating evolution): after "done" the game ran an evolution (the baby game, a hatchling), not the Tribe start, and no
  migration followed; History's check only closed History (its Creature bar half full while the HUD's was full). The
  game's "ready to form a tribe" state is still to be reached in a test game (docs/re/creature.md#creature-tribe-ready-test).
* ~ Seen once: the crossing's host logged "sending the world to Pandize 2" and "player 1 left" 40 s after the split
  (the old session's partner still in its list of remotes when the editor returned); the partner never joined it.
* ✔ The whole crossing in game (TITAN 2026-11-19 22:15-22:24, QA 8 hosting Samuth (Creature), QA 9 another party):
  QA 8's stage end (`coop.vote tribe`, the game's own stage-end request) split its party into its crossing, QA 9 took
  Samuth (Creature) over in place; after the editor QA 8's game played the Tribal stage offer and loaded Tribe with its
  village at its old nest; 10 s later ARRIVE made the crossing Samuth (Tribe) ("arrived from its crossing and hosts
  it", no reload) and its first upload stored Samuth (Tribe) (5454 KB) beside Samuth (Creature), which QA 9 kept
  hosting and saving. Pictures: images/stage4_crossing_tribal_offer.png, images/stage4_crossing_tribe_intro.png.
  Not run in game yet: a party of two or more crossing together, a guest's party crossing, arriving where the next era
  already has a host (each is covered by the server checks only).
* ✔ Joining the players already there (TITAN 2026-11-19 22:47): QA 8 hosted Samuth (Tribe) from the server, QA 9
  hosted Samuth (Creature) and ended its stage; its crossing ARRIVEd while Samuth (Tribe) had a host, so the server
  sent it to join QA 8 (kMovedJoin); QA 9 downloaded nothing new of its own, joined QA 8's Tribe world as a late joiner
  and got its own village and chief there (images/stage4_join_tribe_host_view.png, _guest_view.png). Found and fixed on
  the way: the end of the crossing's session made the game send LEAVE right after its ENTER (it ended alone in a Tribe
  world of its own). Open: its village was placed next to the host's, not at its own nest (BUG-9280); the Tribe food
  share runs between the two parties (BUG-9281).

## Owner's idea: other parties' players in their colour instead of name tags (2026-11-19, feat/lineages-alpha)
<a id="server-party-outline-idea"></a>

Owner, after the join test's pictures (name tags over every tribe member, "-NoShare-" names, no Party panel): "I think
it's because we didn't develop a system for a group of a player. That's why we went with tags over every tribe member /
marine piece. Would it be possible to outline other party member's in their chosen color? In Tribe stage, that would
mean outlining all tribe members. For civ, it'd be all units, etc."

* Today: `all.name_tags` (coop.cpp on_overlay) writes a player's name over its creature, its tribe members, its cities and
  vehicles; the styled plate with the distance (decision 3E C) is only the partner's own creature's marker. "-NoShare-"
  is the QA games' name rule (their creations never reach the owner's Cache), not what players see. No Party panel in the
  test: QA 8 and QA 9 were two parties of one, and the Party panel shows the own party's members only (6J).
* The idea fits the game: Spore itself marks units by drawing on the models (the hover highlight on creatures, the
  selection on tribe members and vehicles). What is known: OutlineFormat_SetStrength (0x830230) is the UI's text
  outline, not a model's. Not known yet: the model highlight path (the rollover glow of the Creature and Tribe stages,
  UI::SimulatorRollover), whether it takes a colour, and its cost with many units. A spike decides it: one tribe member
  drawn with a coloured outline or rim from the mod, then every member of a player's tribe, then Civ vehicles.
* Each player's colour exists already (the F2 colour, pfind, BUG-0949); the party's members could share their leader's.
* (23:17 rerun) BUG-9281 fixed and seen: the two parties in Samuth (Tribe) show no shared "Village food" line any more.
  BUG-9280 corrected (its ticket): the village by the host's came from an old partner seat in the QA save, made at the
  crossing's Tribe start; the real gaps are the Tribe start making villages for seats outside the crossing party, and a
  stranger's Creature nest being placed by the host's like a partner's (decision 7E).
* 7D A and 7E A built (2026-11-19 late, BUG-9282, BUG-9283): a partner's plate edge and arrow in that player's colour
  (images/7DA_plate_colour_off_on.png); another party's player gets its nest on a spot of its own (seen: 295 m from the
  host's, 250 m from every player's nest). With 7E A a stranger's nest is far from the host's, so a party's Tribe start
  that turns other species' nests into rival tribes (the game's own rule) makes a stranger's tribe at its own nest:
  BUG-9280's "the nest becomes the village" for a party that arrives later. Not seen yet in game.

## Step 4 part 4 built: extinct species come back (the nest refresh) (2026-11-20, feat/lineages-alpha)
<a id="server-nest-refresh-built"></a>

The first half of #server-nest-refresh (BUG-9285, switch `creature.nest_refresh`; code mods/coop/nest_refresh.cpp).

* The galaxy server has the refresh time (`--refresh-minutes`, 180 by default, 0 never) and sends it in WELCOME; its
  PONG carries its clock (Unix seconds), so every host of a planet times the refresh the same way.
* The planet's host (Creature, a galaxy planet) looks at the wild herds every 10 s: a species whose herds are extinct
  (+0x190) is noted with the server's time in `<save>.spo.duo-extinct` next to the planet (it is in the world the
  server stores, so a hand-over keeps it). When the refresh time has passed, the host undoes the extinction: the herds'
  +0x190 (cHerd_SetExtinction(false)), the nests' mark (+0x120), and the species' relation record back to "not set"
  with no kills (the archetype decides again; its goal cards come back). Players' own species are left alone.
  A species found extinct when the planet is first seen with the refresh (an older world) starts its clock then.
* Guests follow through the worldobj herd kind (a herd the host revived is revived in the guest's game), for the 48
  nests nearest each guest, as extinction itself is synced: a farther one catches up when the guest comes near, and the
  next host loads the revived world from the server.
* ✔ Runtime (TITAN, QA 8 host, QA 9 another party, the test galaxy): the server's 10800 s and clock arrived; the 5
  species extinct on Samuth were noted and the file written; with the clock moved 3 h on (`coop.refresh age 10900`)
  and the switch off all 5 stayed extinct, on the host revived all 5; the guest revived the 2 within its nearest nests
  at once (the others 424-637 m away); at one revived nest the host found 7 animals and the game's own "Impress 6
  Fatroch to ally" goal (images/stage4_nest_refresh_revived.png). `tools/tests/galaxy_server_check.py` checks the
  WELCOME field and the PONG clock.
* Not built yet (the second half of 6Z): the nests a party leaves behind when it moves on, rotated out after a while,
  and the players' own nests after their player has been away for days.

## Step 4 part 4 built: a player's left nest gets a new species (the nest rotation) (2026-11-20, feat/lineages-alpha)
<a id="server-nest-rotation-built"></a>

The second half of #server-nest-refresh (decision 7F, BUG-9286, switches `creature.nest_rotate` on the host and
`wsync.nest_rotate` on guests; code mods/coop/nest_refresh.cpp, wsync.cpp).

* Owner's answer to 7F: "when it rotates out, it's a normal spore picking including the cache like normal".
* Which nests: a nest herd with no slot in the planet's pick table (herd +0x8C 0, docs/re/creature.md#creature-species-pick):
  the game picks every wild nest's species through a slot, a player's nest has none (the game's own nests of a player's
  species and home_nests' nests). Players here now (the host's species, each player's nest herd) are left alone.
* When: the planet's host notes when no player of such a species is on the planet (the server's clock, as the refresh)
  in `<save>.spo.duo-left` next to the planet (it travels with the world). The server's `--rotate-hours` (72 by default,
  sent in WELCOME after the refresh time; 0 never) later, every nest of that species rotates. A player of it coming
  back before that clears the note. One rule for a party that moved on to the next stage and for a player away for days.
* How: the game's own pick (SporeCoop::PlanetPickSpecies 0xB9A8A0) with the archetype and generation of the nearest
  wild nest and no slot, so a fresh pick from the creations the game knows, Sporepedia downloads included; then what the
  game's own evolution step writes (generation, species, cHerd_ApplyProfile, the members' +0x1004). The nest gets a
  slot of its own (0x7E000000 | the new species' instance: a wild nest from then on), is no longer the party's best
  friends and does not evolve by itself (+0x85 0). Its animals turn into the new species within a minute.
* Guests: the existing nest sync (wsync row herd_evo, BUG-0668) carries the new species; for a nest with no evolution
  chain the guest takes the species by its key, a species the guest does not have (a download only the host had) it
  asks the host for (creations::want_species, BUG-0724), and the row now also carries +0x191 (best friends).
* ✔ Runtime (TITAN, QA 9 hosting Samuth (Creature), QA 8 a stranger's party, the test galaxy): the host noted 15
  players' species and dropped QA 8's when it joined; with the clock moved 73 h on and `creature.nest_rotate` off, 0
  nests changed; switched on, 25 nests of 14 species got new species (14 different picks, among them Sporepedia
  downloads; another run picked others); their animals were the new species within a minute and attacked the host at
  what had been a party's friendly nest (images/stage4_nest_rotation_new_species.png). Guest: before the guest part, 13
  of 126 nests kept the old species (species it did not have); with it 0 of 125 differ (it asked the host for 14
  species). QA 8 left: its nest was noted at once and rotated 72 h (test clock) later. After a save, an upload and a
  reload all 26 rotated nests kept species and slot, and none was noted again. `tools/tests/galaxy_server_check.py`
  checks the WELCOME field.
* Not seen yet: a rotated nest when its new species goes extinct (the game's own replacement pick runs with the
  nest's slot; the refresh brings it back first), and the MSI (it needs the new builds).

## Step 4 part 5 built: visitors across eras (2026-11-20, feat/lineages-alpha)
<a id="server-era-visitors-built"></a>

Decisions 5S A / 5T B (#server-era-visitors, #server-era-spike); BUG-9287, switch `galaxy.era_visitors`; code
mods/coop/era_visitors.cpp, galaxy_crt.cpp, server/galaxy/main.cpp.

* The stream: each player in a stage sends where its creature is (VISIT: its era, its species key, its position;
  twice a second; in Tribe the chieftain, the game has no avatar there). The galaxy server passes it to the players of
  the same planet in its other worlds (another era, a party's crossing: `crossing_planet` equal, the world not the
  same), nobody else; one sooner than 0.375 s after the last is dropped. `--no-visitors` turns it off on a server.
* What a game shows: each visitor as a creature of its species with no AI, a puppet as a partner's (own herd, no AI,
  kept out of saves: the save with a visitor stripped it and its herd), walking where its player walks (the game's
  own walk, a MoveTo when the target moved 2 m; a teleport beyond 30 m). Shown in the Creature and Tribe stages;
  removed 6 s after its last VISIT.
* ✘ A creature drawn on the spike's ground prop (ornament + SetModelKey with a creature key) shows nothing; vehicles
  and buildings draw that way, creatures do not. So visitors from the creature stages are creatures; a Civ era's
  vehicles and buildings (props) are the next piece, with a Civ era in the test galaxy.
* The creation store (net/galaxy.h CRTPUT .. CRTPART): a game rarely has another era's player's creature (each is a
  creation of its own player's profile or Sporepedia). Each player puts its own creature in the galaxy server's store
  (the mod's species bundle: its records and creaturedata, 58-88 KB; one file per creation in <worlds>/creations, kept
  across restarts, a key kept once); a game that has to show a creature it does not have asks for it and installs it
  as a host's own species is installed on a guest. Generalises: any game of the galaxy can get any player's creature.
* ✔ Runtime (TITAN, the test galaxy): QA 9 on Samuth (Creature), QA 8 on Samuth (Tribe). Each game put its creature
  in the store (88300 and 58841 bytes) and got the other's (one ask each); each showed the other era's player
  (images/stage4_era_visitors.png: the Tribe player's small green species in the Creature stage, the Creature
  player's purple one among the Tribe village). The Creature player walked 30 m: its copy in the Tribe game walked the
  same way, up to 11 m behind while moving (10 FPS QA), 2 m from it 2 s after. Switch off on one game: both games
  dropped their visitor; on: both showed it again. A save with a visitor: kept out (savestrip last_ours 2), the
  upload stored. `tools/tests/galaxy_server_check.py`: VISIT reaches the other era only, not another planet or the
  sender, too soon and wrong size dropped; the store keeps a whole creation only, serves it back whole, answers 0
  for an unknown one.
* Not yet: Civ / Space eras (vehicles and buildings as props, ships as UFOs); a name over a visitor; a player can
  still hit a visitor in its own game (nothing reaches the other era); the lag while walking (a target ahead on the
  path, as partners' puppets do, BUG-0360); many visitors at once (the server sends every VISIT to each player of the
  planet's other worlds: an interest radius for an MMO-sized planet); the MSI (it needs the new builds).

## Visitors: radius, name tags, the store by owner, Civ objects (2026-11-20, feat/lineages-alpha)
<a id="server-era-visitors-more"></a>

* BUG-9288: the server passes a VISIT only to players whose creature is within 300 m (`--visit-radius`, 0 everyone);
  a player that sent none yet gets them all. Server checks: 400 m away none, 100 m away one; FAIL with the radius off.
* BUG-9289: a grey name tag over each visitor within 120 m (its name and "<Era> era"), at its head as a partner's tag
  (switch `galaxy.visitor_tags`). The walking gap (5-6 m on average at 2 VISITs a second and 10 FPS) did not change
  when the walk aimed 0.5 s ahead (5.2 m against 5.7 m over 4 walks each): taken out.
* BUG-9290: the creation store keeps a creation as {its owner's identity, instance, type, group}: creation ids count up
  per profile, so two players' creations can share a key (a vehicle design above all). Any creation type; installed
  under a key of ours from its content. VISITOR carries the sender's identity.
* BUG-9290: the Civ era's host sends its city buildings and vehicles near the other eras' players (VISITOBJ: id, model
  key, position, orientation, velocity, kind; vehicles twice a second, buildings every 4 s, at most 8 messages a
  second) and puts their designs in the store. A Creature or Tribe game makes a ground prop per object (installed design,
  else its own key), a vehicle moved on along its velocity. ✔ Runtime: 441 objects in 129 messages, 6 designs fetched,
  9 props at the city's places. Before a prop the game bakes every level of detail of its model: a Creature profile had
  level 0 only of a vehicle and a building (IBakeManager vt 0x1C(key, level): mask 1; a spawned creature F); asked with
  vt 0x4C(key, &level), all four were there 20 s later.
* ✘ Open (BUG-9291): those props mostly do not show. One truck showed (a fresh game at 1280x960, near the nest, as in
  the spike); not after the avatar moved, not at 800x600, not after a hot reload, also offline and without the mounted
  package, also on a UFO carrier. Ruled out: the bake, the session, the package, the carrier.
* CORRECTION (2026-11-20, feat/lineages-alpha): the "✘ Open (BUG-9291)" above was mostly the tests: the spike's
  "25 m ahead" point and `coop.nest look` leave the prop off the screen (the status now prints its screen position).
  What is real: an ornament prop draws only near the place it had in its first frames; moved more than its size later
  it is no longer drawn. Streamed objects are made at their place, a moving vehicle is made again beyond 6 m
  (`galaxy.prop_remake`). ✔ The Civ city of Abber drawn from the Creature stage (images/stage4_civ_visitors.png); a
  driving vehicle made again 5 times along its way. The buildings stand on their own pillars, high where the other
  world's ground is lower (this test put Abber's city on Samuth's ground; a planet's eras share their ground).

## Step 4 part 3 for the Cell and Tribe stages (2026-11-20, feat/lineages-alpha)
<a id="server-party-stage-end-more"></a>

BUG-9292, switch `galaxy.party_stage_end_more`. The Cell stage's switch (History's check, h_CellToCreature) and the
Tribe stage's native graduation request (h_Submit) go through the party's crossing as the Creature stage's end does
(#server-party-stage-end): the leader's party moves into its own crossing, the switch / request runs again there, and on
arrival the crossing becomes the next era (or joins its host).
* ✔ Runtime (TITAN, QA 9 hosting Cellfour (Cell) from the test galaxy, `coop.vote creature`): the party moved into
  "Cellfour~1|Cell" (5 s), the switch ran there, the game played Creature, the crossing became "Cellfour|Creature"
  (15 s) and the first creature's editor opened. With the switch off: the game played Creature on "Cellfour|Cell".
* ? Tribe -> Civ: built, not run (a Tribe save at its graduation takes ~40 minutes of scripted raids).
* ✔ Cross-PC (2026-11-20, TITAN's galaxy server, the MSI over Tailscale with today's build): TITAN QA 9 on Samuth
  (Creature), the MSI on Samuth (Tribe): each game showed the other's player (1.3 m and 1.7 m from where it was sent)
  and got the other's creature from the creation store (one ask each).

## Era visitors are solid, can be hidden, one menu row per planet (2026-11-20, feat/lineages-alpha)
<a id="server-era-visitors-solid"></a>

Decisions 7H A and 7I A (docs/ROADMAP.md), the owner's menu point of 7H.
* BUG-9293, switch `galaxy.visitor_solid`: this game's own creatures (Creature: the avatar; Tribe: the player tribe's
  members, on the simulating game) are pushed out of another era's buildings and vehicles (each prop's box, turned with
  it) once a frame, and out of a visitor's "stretch": from its creature to where its player last said it was (up to 30 m),
  the owner's speed-scaled hitbox. Visitor creatures themselves are creatures of this game (Havok). ✔ In game: the avatar
  held at a building's wall (switch off: it walked to the middle); put on a 4.2 m stretch of a walking Tribe chief it was
  pushed 1.27 m off (off: 0.00 m). ? Not run: Tribe members against a prop.
* BUG-9294 (7I A): cross-stage visibility is the player's: coop.ini [galaxy] other_eras (on), chat `/eras on|off`; the
  server stops that player's visits both ways (net/galaxy.h ERAS). ✔ galaxy_server_check (FAIL on the old server), ✔ in
  game both ways. Not built: a button in the co-op window.
* BUG-9295: the galaxy menu shows one star per planet (its eras' players added, the latest era's icon); Play enters the
  era this player last played there, else the earliest. ✔ In game (images/stage4_menu_one_row.png). Open: the 5R stage
  choice for a player new to a planet (Spore's own "Choose A Stage" of an empty star is a candidate).
* BUG-9296 (found on the way): a Tribe game showing a Civ era's objects crashed in the model bake (asked in the frame;
  asked outside it, the bake's end crashed in Editors::cEditor::HandleMessage). Bakes are now asked only in the Creature
  stage, outside the frame, once per model. ✔ No crash in 30+ minutes of Tribe hosting.

## A button for players in other stages (2026-11-22, feat/lineages-alpha)
<a id="server-eras-button"></a>

BUG-9334, switch `galaxy.eras_button`. On a galaxy server the Social Menu (F8) ends with "Players in other stages" and
a button showing the setting of BUG-9294 (#server-era-visitors-solid): gold "Shown", grey "Hidden"; a press does what
chat `/eras on|off` does. Off a galaxy server there is no row.
* ✔ In game (TITAN's test galaxy, QA 9 on Samuth, Creature): a real click switched it both ways; switch off: no row.
* ✔ UI pitfall (again): a window given a new drawable keeps drawing the old one until it changes; hiding and showing it
  draws the new one (set_look, now so_chip_look too).

## The stage choice for a player new to a planet (5R A, 2026-11-22, feat/lineages-alpha)
<a id="server-stage-choice"></a>

BUG-9335, switch `galaxy.stage_choice`. Play on a planet of several eras where this player has played none opens
"Choose a stage": one button per era (stage order, its players); a press enters that era and remembers it (BUG-9295's
[galaxy_eras]). Stages not reached in the player's own Spore are grey and refused with a notice (Spore's rule, 5R A);
the reader is the game's own stage chooser check (docs/re/space.md#space-stage-unlocks). One era or one
remembered: entered at once, as before.
* ✔ In game (TITAN, test galaxy, Samuth Creature + Tribe): the choice showed both, a click on Tribe loaded Samuth's
  Tribe era; the next Play went straight to Tribe; switch off: the earliest era (Creature) at once.
* ✔ Grey stages (a profile with Creature and Space reached): Tribe grey, a click refused with a notice, Creature entered.
* Not built: starting a stage the planet has no era of yet (a new era from Spore's own Choose A Stage wizard).
* Built later (BUG-9336, #server-stage-start): the choice offers "Tribe (new)" on a planet of a stored Creature era and
  no Tribe era.

## Starting a stage the planet has no era of yet (5R A, 2026-11-22, feat/lineages-alpha)
<a id="server-stage-start"></a>

BUG-9336, switch `galaxy.stage_start`, protocol 9. Spore's own Choose A Stage makes a new planet; a galaxy planet's new
era has to grow on that planet's ground, so the start reuses the party crossing of #server-party-stage-end with one
player and the stored world of the stage below.
* The stage choice (#server-stage-choice) adds "Tribe (new)" when the planet has a stored Creature era and no Tribe era
  (players or stored); grey when the player has not reached Tribe in its own Spore. A planet of one era opens the choice
  only when that new stage is open to the player.
* A press: STARTERA (the era to start from, the era to start). The server makes the player a crossing of its own,
  "<planet>~<slot>|Creature", whose FETCH sends the Creature era's stored world, and answers ROLE host. Refused: two
  stages up, a stage that has players or a stored world (enter it instead), no stored world to start from, a start
  already on its way.
* The game hosts the crossing as any stored planet (a player new there makes its first creature first, BUG-9265); once
  it plays (5 s settled, no editor, no cinematic) it asks the game's own Creature stage end (the brain level raised to 4,
  then the request History's check sends: docs/re/creature.md#creature-tribe-ready-test). The player gets Spore's own
  stage-end editor, "Congratulations! You are about to enter the Tribal stage" and the Tribe start with its village at
  its nest; 10 s into Tribe the crossing ARRIVEs: it becomes the planet's Tribe era (stored at its first upload), or
  the player joins the Tribe era's host if another player made it in the meantime.
* ✔ In game (TITAN, Samuth with only its Creature era): the choice (images/stage_start_choice.png), a click on "Tribe
  (new)", the editor, the Tribal offer (images/stage_start_tribal_offer.png), the Tribe intro with the player's creature
  (images/stage_start_tribe_intro.png); the server: "Samuth|Tribe: arrived from its crossing and hosts it", then
  Samuth (Tribe) stored (5706 KB) beside the untouched Creature world. Switch off: no choice, Samuth (Creature) at once.
* ✔ galaxy_server_check: 7 checks (FAIL on the server before).
* Not built: a Civ start from a Tribe era (the Tribe stage's end has no request to ask yet), a Creature start on a
  planet that has only later eras (nothing to grow it from), a party starting together (the start is the player's own;
  its party can join the new era afterwards).
* Seen once, not followed: the nest log line after the editor read "follows its evolution 00000001 -> 7CD60050"
  (the previous species id 1); the Tribe start was normal.

## The stage choice in Spore's own Choose A Stage screen (8L A, 2026-11-22, feat/lineages-alpha)
<a id="server-native-stage-choice"></a>

BUG-9337, switch `galaxy.native_stage_choice`. Decision 8L A (the owner): the stage choice of #server-stage-choice and
#server-stage-start is Spore's own Choose A Stage screen (the paw, fire, globe and rocket around the planet), not our
box. The galaxy menu itself was Spore's already (its entries rewritten, #server-galaxy-menu-spike); only the box was
ours.
* Play on a server planet that wants a choice switches the menu to its new-game state with that planet's entry (as an
  empty star's panel does), not to its Play state. Spore opens its screen for the planet; the mod enables the stages
  the planet offers (its eras, the new Tribe stage) that the player reached in its own Spore, the others grey.
* A press closes the screen as its Back does and enters the era or starts the new stage, as the box did. Never a new
  game. The X or Esc: back to the planets (the entries rebuilt).
* Lost against the box: the players per stage. The box stays as the fallback (switch off, or the screen's symbols
  missing).
* ✔ In game (TITAN): Abber (Creature + Civ, Tribe new) through the real star, Play, X, Play, globe: the Civ stage
  loaded (images/native_stage_choice.png, images/native_stage_choice_civ.png); Samuth: the fire started the new Tribe
  stage. Switch off: our box (images/native_stage_choice_off.png).
* Found on the way, open: BUG-9338, the box (fallback only now) opens on top of Spore's stage loading card after a real
  Play.
* How the screen works: docs/re/space.md#space-native-stage-choice.

## The planet details panel: a mockup (8M, 2026-11-22, feat/lineages-alpha)
<a id="server-planet-details-panel"></a>

Owner, 2026-11-22, on the players per stage lost with #server-native-stage-choice: "Why can't we add a little panel
(similar to party player name panel) off to the opposite side of the planet menu that shows the details of the
planet? Can you create a mockup?"
* Yes: Spore's screen has no room of its own for it, but the mod already draws its own panels over Spore's menus (the
  Social Menu, the Party panel, #server-social-menu). A panel beside the screen is the same work.
* Mockup: images/mockup_planet_details.png, on a real shot of the screen (Abber, QA 9) with the Social Menu's own frame.
  Right of the planet: the planet's name, "Online world", players here; one row per stage in the buttons' order
  (Cell, Creature, Tribe, Civilization, Space) with the stage's own icon: players and host, "New here: start it",
  "Not on this planet" or "Not reached yet" in grey; the hovered stage's row lit gold with its players' names; friends
  on the planet; when it was last saved. Sample names only.
* What the data needs: players per era, host and saved come with the planet list already (galaxy_client.cpp, PLANETS);
  the players' names per era and friends from the Social Menu's player list (WHO, PLAYERS); the save's age from ROLE.
  The hovered button: Spore's flow window under the pointer (not looked at yet).
* The connect flow as it is today, shot on TITAN (QA 9, out/shots/connect, not in git): Spore's own "Network play
  disabled" box at start (an offline copy), the Co-op window (F9 or Co-op), the Galaxy Server row, Join ("Galaxy
  server: connected", the row shows the galaxy's name), the window closes to the galaxy menu with the server's planets
  as stars; a star, Spore's planet panel, Play, the Choose A Stage screen. Seen on the way: the planet list message
  runs off the right edge (BUG-9339, open); the galaxy row says "0 planets, 0 players online" for a moment after Join.
* Built (8M A, BUG-9340, switch `galaxy.planet_panel`): as in the mockup, with Spore's own stage icons (the screen's
  buttons' drawables) and the stage under the pointer lit with its players' names; "Saved on the galaxy server" for the
  mockup's save time (the planet list has none). With the Social Menu open it stands under it. ✔ In game (TITAN, QA 9 by
  real clicks, QA 8 hosting Abber's Creature era as a friend): images/planet_panel.png, images/planet_panel_empty.png;
  switch off: images/planet_panel_off.png. Esc, Back and a stage press close it with the screen.

## Starting the Civ stage from a planet's Tribe era (5R A, 2026-11-22, feat/lineages-alpha)
<a id="server-civ-start"></a>

BUG-9343, switch `galaxy.civ_start`. The next step after #server-stage-start (Creature -> Tribe): a planet whose latest
era is Tribe offers its Civ stage as new to a player who reached Civ in their own Spore (Spore's Choose A Stage globe,
"New here: start it" in the planet details panel).
* The press makes the player a start crossing from the Tribe era's stored world (the server needed no change). Hosted, the
  game fills the Tribe goal bar and presses Spore's advance button itself; from there the player goes through Spore's own
  end of Tribe: History ("Congratulations! Your Tribe reigns supreme!") and its check, the cinematic, the City Hall
  editor, the first vehicle's editor, Civ. The crossing arrives as the planet's Civ era and is stored.
* Like the Creature start, it skips the planet's own progress (the five victories): 5R A's rule is the player's own Spore.
* ✔ In game (TITAN, Clyth): images/bug9343_civ_new.png, images/bug9343_history.png, images/bug9343_civ.png; switch off:
  images/bug9343_off.png. Not run: a party of several, the MSI. Civ -> Space is not offered (the next step).
* How the Tribe end works: docs/re/tribe.md#tribe-advance-button.

## The galaxy server's creation ban list (8D A, 2026-11-23, feat/lineages-alpha)
<a id="server-ban-list"></a>

BUG-9345, switch `galaxy.server_bans`. Decision 8D A (owner 2026-11-23): the server sends a creation ban list, so a new
ban reaches the galaxy's players without a mod release.
* The server's operator writes `banned.txt` in the server's worlds folder, with the lines of the mod's
  data/lineages/banned.txt: `source <identity>`, `creation <sha256>`, `keep <sha256>`. The server reads it at start
  and again within 5 s of a change; connected players get the new list at once.
* `player <galaxy identity, 16 hex>` lines are the server's own (not sent): that player's creations are not kept by
  the server's creation store (#server-era-visitors-built) and none already kept are handed out (the files stay).
* The mod keeps the last galaxy server's list (LineagesCache\galaxy_banned.txt) and applies it at once and at each
  game start beside the shipped list: the same move into LineagesCache\banned (never deleted), the player's own
  creations never touched. Live Lineages without a galaxy server keeps only the shipped list.
* A ban the server takes back stays applied until the next start (a moved creation is never moved back).
* Protocol: net/galaxy.h BANS (player: the crc of its kept list) and BANSPART (server: the list in pieces until the
  player's BANS names its crc). An older mod never asks and gets nothing; an older server ignores BANS.
* ✔ In game (TITAN): switch off nothing moved; on, the banned creation moved at once and the kept copy applied at the
  next start; a banned player's creature was not stored. Not run: a refused get, the MSI.

## Starting the Space stage from a planet's Civ era (5R A, 2026-11-23, feat/lineages-alpha)
<a id="server-space-start"></a>

BUG-9346, switch `galaxy.space_start`. After #server-civ-start: a planet whose latest era is Civ offers its Space stage
as new to a player who reached Space in their own Spore.
* The press makes the player a start crossing from the Civ era's stored world. Hosted, the game ends Civ as a nation's
  surrender would: the planet's cities still to come are founded, then every city of another nation joins the player
  civ (docs/re/civ.md#civ-stage-end, the instant path). Spore then shows "Global domination is yours!", the Advance to
  Space button, History, the spacecraft editor and the Space stage; the crossing arrives as the planet's Space era.
* ✔ In game (TITAN, Abber): images/bug9346_space_new.png, bug9346_domination.png, bug9346_history.png,
  bug9346_ship_editor.png, bug9346_space.png, bug9346_space_ship.png; switch off: images/bug9346_off.png.
* Open: the Space era is stored only once the ship is in a solar system (Spore saves Space only there): BUG-9348. Not
  run: a party, the MSI.
* ? Design question for later (decisions 2H-2K): what a planet's "Space era" is once its players fly off; today it is
  the world of the players who started Space there.

Social Menu, 2026-11-23 (BUG-9347, switch `ui.menu_beside_social`): a player's right-click menu from the Social Menu
opens beside the panel (it opened over or under it). Pictures of the Social Menu for sharing: images/social_share/.

## Space ships over a planet's other eras (2026-11-23, feat/lineages-alpha)
<a id="server-space-ships"></a>

BUG-9349, switch `galaxy.space_ships`. The "Space ships over any planet: as UFOs" idea of #server-space, for a planet's
own Space era: while a Space player's ship is at the planet, the planet's Creature, Tribe and Civ games show it as a
UFO with that player's ship design, where the ship is (the same VISIT stream as era visitors, the design through the
creation store). It goes when the ship leaves the planet. Look-only, kept out of saves.
* ✔ In game (TITAN): Abber's Space ship over Abber's Civ city (images/bug9349_ship_in_civ.png); switch off: no ship
  (images/bug9349_off_on.png). Not run: the ship moving, Creature or Tribe games, a name tag, the MSI.
* BUG-9348: the Space era is stored once the ship is in the solar system (Spore saves Space only there); seen.
* The ship flies at most 60 m over the ground in the other eras (its player's zoom altitude, 775 m from the centre at
  Abber, put it out of a Civ camera's view) and wears a name tag with "Space era" (images/bug9349_tag.png).

