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
* ~ Seen with BUG-9252, not fixed: the mod's "Bringing your nest" loading screen stayed over that first-creature editor
  until its 120 s limit, and the galaxy menu's splash text ("galactic adventures", the rating line) was drawn over it.
* Not yet: the hand-over while players stay (today everybody leaves when the host leaves, and the next to enter hosts
  the stored world), the galaxy menu showing the server's planets, eras.

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
