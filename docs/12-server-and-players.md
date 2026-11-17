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
* Not yet: the server keeping each planet's world (the host's game hosts whatever it has loaded), the hand-over to the
  next player, a menu in the Co-op window, a test across PCs (it needs the firewall allowance on the server's PC).

## Questions for the owner
<a id="server-questions"></a>

Asked in docs/ROADMAP.md as 5H (what goes first) and 5I (how strict the checks are). Who runs the parts of a planet
nobody is near waits for phase 0's answer.
