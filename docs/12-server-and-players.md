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

## Cheating: trust, but check
<a id="server-cheating"></a>

* What can be stopped: the server keeps the truth that matters (saves, DNA and progress, deaths, extinctions,
  ownership), and checks every request against what it knows: only the owner may send a thing's states (5EA already),
  speed and position jumps within the creature's limits, a bite only within reach of its target, damage within the
  ability's own values (today only a cap, kMaxHit), how often (rate limits), summons and spawns only through the server.
* What cannot be stopped: a modified game can lie inside those limits (a perfect aim, never missing a beat). No game that
  lets players' PCs simulate can prevent that; the big online games add logging, reports and bans.
* For co-op with friends: the checks above are plenty. For public servers later: logging of check failures, kick and
  ban tools for the server's owner, then more checks where players find holes.

## Per stage
<a id="server-stages"></a>

| Stage | Today | Moves to its player first | Later |
|---|---|---|---|
| Creature | host runs the world; own creature and (5EA) own pack per player | own Summon Flock (5FB); own nest family | wild creatures near each player (5EC) |
| Tribe | host runs all tribes | each player's own tribe members | wild creatures and rival tribes near a player |
| Civ | host runs the planet | each player's own cities and vehicles | rival cities near a player |
| Space | each player flies in their own copy of the host's galaxy | already mostly per player | own empire per player (ROADMAP MMO questions) |
| Cell | host runs it | keep as is (short stage) | |

## Phases
<a id="server-phases"></a>

Each phase ships on its own and keeps today's co-op working.

| Phase | Work | Done when |
|---|---|---|
| 0 Spike | A QA Spore instance as a headless host on TITAN: drawing cut, its own creature hidden or absent, two players join, play 10 min. Measure CPU, memory, what still simulates | a yes / no / "only with a stand-in creature" on the headless server, with numbers |
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

## Questions for the owner
<a id="server-questions"></a>

Asked in docs/ROADMAP.md as 5H (what goes first) and 5I (how strict the checks are). Who runs the parts of a planet
nobody is near waits for phase 0's answer.
