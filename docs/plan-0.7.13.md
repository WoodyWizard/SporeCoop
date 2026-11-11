# SporeCoop 0.7.13 plan (draft 2026-11-10): bugs nobody had found yet

0.7.13 is a bug release built from an intake and an audit done on 2026-11-10 (main e49e1e23 = 0.7.11.1, plus the
unreleased 0.7.12 code): the open GitHub issues and Discord reports, the bugs three outside forks found in code we
share, and a read-only hunt through our own code for unfixed siblings of bugs that already hit players. It starts
after 0.7.12 (docs/plan-0.7.12.md, paused 2026-11-08). The rules of docs/plan-0.7.11.md apply: every ticket of this
plan closes in this release, fixed with a test that FAILs with its switch off and PASSes with it, or closed with
evidence after a serious attempt; "not reached" is not an outcome.

**Status: draft; the owner's decisions of 2026-11-10 are at the end.** Nothing here is implemented. Every ticket below is open and was written by
the agent that found it: Report, Root cause (file:line, observed or inferred, confidence), Fix idea, How to test.
Almost all findings come from reading code, not from a run: the first step of each package is the FAIL test.

## Sources

| Source | What it gave |
|---|---|
| GitHub issues #199, #200, #201 (#197 answered) | Civ vehicles "like flash" (0.7.10.1, with a report); antivirus detections on the setup and the loader |
| Discord bug-reports / help / suggestions, 2026-11-07..10 | "Screen tearing" (host, since multiplayer is on); a captured settlement on one game only; crash on launch with RivaTuner / MSI Afterburner; Space badges lost on a rejoin |
| Fork PandizeLive/SporeCoop-Lineages (PR #196 is its 2026-11-06 snapshot; the fork is at lineages43-dev, based on our 0.7.3.6) | 17 bugs in code we share (crashes on freed objects, the save watcher, the world share) and 11 features |
| Fork jwhans0n/SporeCoopGA (Galactic Adventures co-op) | 8 fixes and tooling ideas, co-op adventures and a shared adventure editor |
| Fork nvstlex/SporeCoopNVST (rivals foundation) | a Cell placement fix, a data model for Rivals |
| Code audit: core / net / SporeKit / updater and the new 0.7.12 code | 8 findings |
| Code audit: Cell and Creature | 24 findings |
| Code audit: Tribe, Civilization, Space | 23 findings |
| Review of closed tickets: 73 not reproduced / wontfix / duplicate / game-bug, and 215 fixed tickets with weak evidence | no new bug behind the unresolved ones (6 old tickets now linked to the audit tickets that explain them, 6 untried tests noted in them); 16 findings behind fixed tickets (BUG-0880..0895); 69 fixes that look right but were never seen failing (listed below) |
| The owner, watching a player's recording (no logs) | an egg a guest ate or hit turned into a body with hunt DNA (BUG-0896); a non-Latin name shown as "Player" (BUG-0897) |

Tickets that come from a fork carry the fork's solution in their "Fix idea" section, marked "NOT FINAL: taken from
the fork, verify on our code and against our rules before using". All three forks keep our MIT licence, so code can
be taken with attribution; describe and re-implement rather than copy. New reverse-engineering facts from the forks
are in docs/re (creature, tribe, civ, space, cell, creations and the new adventure.md), each marked "from a fork,
not verified by us".

## Before 0.7.13

| What | Tickets | When |
|---|---|---|
| **H0 Hotfix of 0.7.11.1** (the owner: release 0.7.11.2 now) | BUG-0850: `destroy_proxy` in npc.cpp destroys an NPC proxy the game already removed (an `else` without braces since BUG-0710): a second DestroyInstance on a guest, in Creature and Tribe | as soon as decided |
| **0.7.12 blockers** (bugs in the unreleased 0.7.12 code) | BUG-0770 (session report and settings survive a leave), BUG-0780 (the session zip written in one call on the main thread), BUG-0783 (a guest that is sending ignores the next Ask) | before 0.7.12 ships; added to plan-0.7.12 B2 |

## Work packages (one agent, one branch, one lane pair each)

| Package | Mechanism | Tickets |
|---|---|---|
| **R1 Player reports: Civ speed** | The city alarm's +50 % speed boost reads an already boosted speed as the base (race at the boost's end), is not put back when the vehicle leaves the host's table, and is also given to NPC vehicles; the guest's mirror stalls and teleports once the host's vehicle is over twice as fast | BUG-0771, 0773, 0876, 0787 (GitHub #199) |
| **R2 Player reports: captures on one game only** | Civ: the first capture deals every city out round-robin, a capture is credited by the nearest vehicle, a guest's city purchase is never made, a capture in the host's absence shows "won" but is not made. Tribe: the outcome of a conquest or alliance is not sent to the guest, and a guest's conquest in the host's absence is undone | BUG-0875, 0775, 0877, 0878, 0777, 0879 |
| **R3 Player reports: the rest** | A guest's Space badges and rank replaced by the host's at every (re)join (the same class as the cargo hold); the picture tearing with the mod active (need the report: Present timing statistics first); the crash on launch with RivaTuner (a warning, the INSTALL advice, the report naming it) ; a hatchling's body and hunt DNA at the place of an egg a guest ate or hit (need the recording); a guest with a name that is not in Latin letters shown as "Player" (no logs: a test of the name path) | BUG-0786, 0779, 0782, 0896, 0897 |
| **A1 Trust and install** | Antivirus false positives (version resources, attestation of the build, vendor submissions, an INSTALL / SECURITY section; signing later); the uninstaller's weak points; a Large Address Aware tool; an installer test race | BUG-0784, 0851, 0862, 0853 (GitHub #200, #201) |
| **L1 Freed objects** (children of the BUG-0426 umbrella) | A pointer of the game kept and used after the game freed the object: NPC proxies between the once-a-second list checks, a cached nest herd, a Tribe mirror citizen, the partner's puppet in effect and animation handlers, proxies in damage and social handlers, a carried rock on a destroyed puppet, a deferred destroy at a reused address, the Cell puppet lookup, the allies mirror after a world reload; newer detours missing from the swap ledger (the sphere vector, the player-species override) | BUG-0842, 0843, 0844, 0795, 0796, 0805, 0814, 0825, 0823, 0838, 0882 |
| **W1 World share and saves** | The save watcher takes any box of window 0x06318E78 (the specialty question, pause-menu questions) for "game saved" and presses Enter on it; the share drops unreadable files; the staged move fails on a brief lock (Windows); a share save between the City Hall and vehicle editors; the cursor held 5 s after the saved box; a saved box with its OK off screen; saves allowed in a standalone adventure; the guest's UFO mirror in its saves | BUG-0846, 0847, 0848, 0849, 0804, 0806, 0854, 0817 |
| **O1 A guest in its own world** (children of BUG-0427) | Host state and guest rules applied while the guest plays another world: the Cell pool emptied, NPC nests never evolving, the co-op cargo hold and the host's stage traits written into the guest's own Space save, the species alias, live Cell state sent to a guest that is still loading, votes waiting 60 s for a guest elsewhere | BUG-0797, 0803, 0816, 0818, 0819, 0809, 0801 |
| **C1 Cell** | A downed cell pinned at its pre-rescale spot, a knocked-out cell's poison still killing, the rename of an imported cell crashing the host, the kill ledger recording a refused kill, a whole swallow without ACT_GONE, a mirrored poison cloud "eaten", 3-4 guests stacked on one spot; the cell puppet overshooting a stop (drive.puppet_fall is Creature only) | BUG-0798, 0799, 0802, 0821, 0827, 0835, 0792, 0887 |
| **C2 Creature** | The guest's Summon Flock event never forgotten, the social meter left on screen, own-editor flags after a refused Submit, the mate out of the reply radius, DNA counted twice after the host's absence, a departed guest's social game, nest egg mirror references, the catch-up speed reset by MoveTo, the attacker pointer of non-creature attackers; the guest picking its own next nest during the host's migration, unchecked rows of the player-species caller table, raw velocities without a NaN guard | BUG-0800, 0808, 0811, 0815, 0829, 0831, 0833, 0788, 0790, 0881, 0893, 0894 |
| **T1 Tribe and Civ** | A bought child given to the wrong player, a member that changed tribes with owner 0xFE, the world epoch not counting Tribe worlds, graduation with a null animal pen, baby mirrors not growing, dead window buttons under a native dialog, a rejected guest building not refunded, the spacecraft editor held by the launch pause; the fire-dance music playing on when its stop arrives outside Tribe | BUG-0824, 0841, 0839, 0856, 0857, 0858, 0807, 0855, 0890 |
| **S1 Space** | Kept colonies lost at a crash rejoin with the same id, the rivals budget reset at every return to Space, the galactic core waiting forever, colony spice collected twice, a failed hold restore made permanent, a refund matched to an older gift, the survey contract failed by a late member, a sale unpaid at the session end, the 48-colony cap, invincibility left on a new ship, the allies' mirror colonies in the spice sync; terraforming dropped while no partner is on the planet, planner buildings not reaching the other guests, the effects relay off by default in Space | BUG-0810, 0813, 0820, 0822, 0826, 0828, 0830, 0832, 0834, 0836, 0840, 0883, 0884, 0895 |
| **N1 Net and session** | A leaver's ready flag inherited by the next player of its id; names from the network not sanitized (log lines, the slot marker); LAN discovery answering the whole internet; 16 half-open peer slots; DOS device names in received paths; a task destructor under the task lock; host() / join() inside a delivery callback; a late chunk of an older realign cycle restarting the cycle | BUG-0772, 0774, 0776, 0778, 0785, 0860, 0861, 0886 |
| **G1 Adventures: safety** (no adventure co-op yet) | The host's time of day overriding an adventure's lighting, the partner's puppet in the scenario cast crashing the host at an act change | BUG-0789, 0845 |
| **P1 Performance, tools, small things** | `readable` calling VirtualQuery on every check (measure first), the wheel without modifier flags in tests, a console command casting a number to a window, the paint reader skipping run-compressed models, one function under two names in the .sym; the watchdog and stack dump calling VirtualQuery while the target thread is suspended (a hang under Wine); tests that cannot fail or assert a mod counter (guest bite, Tribe members eating, the social console, Cell poison damage); the census without a stage-change guard | BUG-0791, 0859, 0812, 0852, 0837, 0880, 0888, 0889, 0891, 0892, 0885 |

## Fixes never seen failing (from the closed-ticket review)

The review found these 69 fixed tickets right in the code, but no run ever showed their test failing without the
fix. They are not reopened; the G-style test pass of a later release can pick them up (write the FAIL twin):
BUG-0022, 0028, 0095, 0096, 0098, 0152, 0169, 0175, 0193, 0231, 0232, 0233, 0249, 0250, 0259, 0262, 0263, 0264, 0265,
0266, 0267, 0268, 0269, 0275, 0277, 0287, 0289, 0292, 0294, 0296, 0297, 0302, 0311, 0312, 0317, 0325, 0331, 0346, 0353,
0356, 0389, 0418, 0419, 0420, 0423, 0424, 0428, 0429, 0430, 0431, 0432, 0454, 0473, 0489, 0491, 0492, 0493, 0498, 0500,
0504, 0509, 0526, 0538, 0558, 0575, 0583, 0588, 0612, 0615, 0623, 0631, 0644, 0657, 0698, 0701, 0719, 0743.

Test ideas nobody tried, written into the old tickets by the review: the 4-player Creature seed with the host's
animals churned and the BUG-0842 / 0843 switches off (BUG-0170); campaign_tribe_share with creature.puppet_alive off
(BUG-0359); a native destroy of the partner's puppet plus an effect in the same frame (BUG-0520 / 0795); a guest
army taking a city while a host vehicle stands nearer (BUG-0648); exit_editor_held with the spacecraft editor
(BUG-0578 / 0855); the content check on a model with a run across its coat layers (BUG-0727 / 0852). BUG-0842's
fix and test must cover Tribe too (the list check runs only in Creature).

## Feature ideas from the forks (moved to the Rivals plan)

Not part of the bug work. Decided by the owner on 2026-11-10: they are planned with the Rivals release (0.8), see
docs/11-rivals.md#rivals-fork-feature-ideas. Each ticket says how the fork built it.

| Idea | Ticket | Size | Relation to our plans |
|---|---|---|---|
| Host campaign autosaves after milestones and every ten minutes | BUG-0864 | small | needs BUG-0804 first; a session setting (F3) |
| Frame rate cap 1-360 and borderless window in Graphic Settings | BUG-0865 | medium | new; may also help BUG-0779 |
| Optional session password, recent servers list | BUG-0867 | small-medium | the co-op window (F1) |
| Tooltips on the co-op window's buttons | BUG-0874 | small | F1 |
| Personal Cell edits without a Ready vote | BUG-0868 | small | a session setting; F5 keeps the vote |
| Wider third-person field of view (Ctrl+Scroll) | BUG-0866 | medium | new |
| Warm reconnect reusing an unchanged Cell world | BUG-0869 | medium | new |
| Difficulty scaling by party size | BUG-0870 | medium | plan-0.7.12 F3 excluded difficulty changes: an owner decision; Rivals 0.8 |
| The players' cells as native NPC species in Cell | BUG-0872 | medium-large | new |
| A depth handle for parts in the creature editor | BUG-0871 | medium | new, not co-op |
| A shared creations library with its own Sporepedia tab | BUG-0873 | large | new |
| Co-op Galactic Adventures | BUG-0793 | very large (13.6k lines in the fork) | README roadmap "co-op adventures"; realistic with the fork's author |
| Shared Adventure Editor | BUG-0863 | very large | after BUG-0793, led by the fork's author |
| Per-stage Together / Rivals table and relation table | BUG-0794 | data model | docs/11-rivals.md (0.8) |

## Order

1. H0 and the 0.7.12 blockers as soon as the owner decides (they do not wait for 0.7.13).
2. Wave 1 (at most 3 lab agents): R1, R2, L1: the player reports with a known cause and the crashes.
3. Wave 2: W1, O1, S1, then R3, C1, C2, T1.
4. Wave 3: N1, G1, A1, P1 (A1 needs no lab: build files, docs, vendor submissions by the owner).
5. Each package starts with the tests that reproduce its findings; a finding whose test cannot be made to fail after
   a serious attempt is closed as not reproduced with the runs as evidence.
6. Integration run after every two merges; final check as in plan-0.7.11 plus the visual checks of 0.7.12 (the key-frame review, ui_golden, the same view on both games) (every new test, play_* of all stages,
   campaign_*, detect_*, the long sessions), then the release.
7. After the release: answer GitHub #199, #200, #201 and the Discord posts that were fixed (with the owner's yes on
   each Discord text).

## Owner decisions (2026-11-10)

1. H0: release 0.7.11.2 with the BUG-0850 fix now (in work: fix/w47-proxy-destroy).
2. The feature ideas go to the Rivals plan (0.8): docs/11-rivals.md#rivals-fork-feature-ideas.
3. The owner contacts the fork authors himself.
4. The owner downloaded the "Screen tearing" report archive; it is analysed in BUG-0779.
