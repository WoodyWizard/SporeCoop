# Plan for 0.7.99 (pre-0.8 release)

Branch: `rel/0.7.99` (branched from `main` / `v0.7.17.1`, `c9d0661b`).
Goal: Final 0.7 polish release before `0.8` (Rivals) shipping co-op Galactic Adventures gameplay (shared runs and host-authoritative enemies), remaining gameplay features, graphics/camera/creations/editor QoL, and all remaining open bugs and desyncs.

## Work Packages

| WP | Scope | Tickets | Families / Wire |
|---|---|---|---|
| WP-0 | Reservations, plan, superseded cleanup (`0.7.99` dummy in `triage_symbols_check.py` -> `0.9.99`, close duplicate `BUG-0868`/`0870`/`0872`) | `BUG-0868`, `BUG-0870`, `BUG-0872` | `creature 1.4` (`MSG 79 ADVENTURE`), `cell 1.3`, `tribe 1.2`, `civ 1.3`, `space 1.5`, `creations 1.3` |
| WP-A | Co-op Galactic Adventures: E-A shared run (goals, acts, win/loss, late join, `adventure.shared`), E-B host-authoritative enemies (`adventure.host_actors`, `adventure.hit_forward`, `adventure.guest_extras`, `adventure.body_side`), per-captain puppet scale | `BUG-1474`, `BUG-0793` (core run, without `BUG-0863` shared editor) | `creature 1.4`, `MSG 79 ADVENTURE` |
| WP-G | Gameplay features: guest pack recruit in Creature (`BUG-1256`), friendly fire in Cell/Tribe/Civ (`BUG-0899`), Civ standing city citizen animations/gestures (`BUG-1231`) | `BUG-1256`, `BUG-0899`, `BUG-1231` | `creature 1.4`, `cell 1.3`, `tribe 1.2`, `civ 1.3` |
| WP-Q1 | Graphics & camera QoL: frame rate cap 1-360 + borderless window (`BUG-0865`), wider third-person FOV via Ctrl+Scroll in Creature/Adventure (`BUG-0866`) | `BUG-0865`, `BUG-0866` | local only (no wire) |
| WP-Q2 | Creations & editor QoL: shared creations library kept between sessions (`BUG-0873`), Creature editor depth handle (`BUG-0871`) | `BUG-0873`, `BUG-0871` | `creations 1.3` (`BUG-0873`); local only (`BUG-0871`) |
| WP-F1 | Open bugs & desyncs (Cell / Creature / Tribe): Cell >1000 m rescale centre (`BUG-1394`), Creature post-editor nest proxies (`BUG-1388`), Tribe chief staff hold & facing (`BUG-1347`, `BUG-1458`), Tribe campaign test (`BUG-1535`) | `BUG-1394`, `BUG-1388`, `BUG-1347`, `BUG-1458`, `BUG-1535` | `cell 1.3`, `creature 1.4`, `tribe 1.2` |
| WP-F2 | Open bugs & desyncs (Civ / Space): Civ city decorations sync (`BUG-1456`), Civ vehicle burst error (`BUG-0438`), Space colony citizen lead (`BUG-1404`), Civ -> Space guest ship editor verification (`BUG-1531`) | `BUG-1456`, `BUG-0438`, `BUG-1404`, `BUG-1531` | `civ 1.3`, `space 1.5` |

## Rules
- Every fix / feature gets its own `COOP_FIX` switch (when applicable), unit tests, real Wine lab test, and FAIL OFF-twin registered in `tools/tests/suite.py`.
- Any WP that modifies a locked wire family re-locks with `tools/msg_manifest.py lock --rebase <family> "<WP>: <what>; unreleased <family> 1.x of 0.7.99"` and updates `docs/04-netcode.md`.
- Ship `v0.7.99` after `make test`, `make release-dry`, `fixcheck`, `tickets.py check`, targeted + smoke + regression lab passes on 4 lanes (`2,3,4,5`), merge to `main`, green CI, and GitHub Release verification.
