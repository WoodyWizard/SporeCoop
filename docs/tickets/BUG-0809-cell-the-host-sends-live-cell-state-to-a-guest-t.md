---
id: BUG-0809
title: Cell: the host sends live Cell state to a guest that is not in its Cell world yet (bandwidth during the world transfer), suspected
status: open
kind: bug
stage: Cell
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 BUG-9098 (lineages30-dev, still open there)
role: host guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature: world transfer to a joining guest crawls on a lossy / tunnelled link while MSG_CELL_STATE goes out
docs: docs/re/cell.md
related: BUG-0405
---

## Report
Fork finding on a constrained UDP path (separate networks, tunnel): a 3.4 MB world that arrives in 6.5 s on a
good link delivered 40 KB in 54 s. The host also sent live Cell snapshots to the guest still in the Galaxy
menu; they are useless there. With the snapshots held back until the guest was in the host's Cell world, the
host's send rate dropped from ~270 to ~22 datagrams/s and the transfer went about twice as fast.

## Root cause
* Our code (observed by reading, effect suspected, confidence medium): `mods/coop/cellsync.cpp:3197-3230` host_tick
  sends `send_state(pl.id, ...)` (MSG_CELL_STATE, unreliable, chunks of 14 cells, up to kSendCap cells) to every
  player id != 0 each cycle. The only per-guest filter is `views` (120 m around the guest's view centre when it
  is known); a guest that has not loaded the Cell world yet (menu, loading, downloading the world) still gets the
  full list around the host. On a lossy link this competes with the reliable world chunks (BUG-0405 area).
* Not measured on our build.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages BUG-9098, commit b3d24b5); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* The host skips live Cell snapshots for a guest until the guest reports being in the same Cell world (its last
  state: mode Cell and world id == the host's current world).
* Separately (opt-in "Paced" transfer, `Session::paced_world_transfer`): smaller world chunks
  (`net::kPacedWorldChunkBytes` instead of 1000 bytes), a smaller in-flight window (`net::kPacedWorldWindow`
  instead of 160 / 600) and spaced original sends and retries in `Connection::build_packet(now, pkt, paced)`;
  the host advertises it in the LAN reply settings bit 0. Normal stays the default. Their test: a synthetic
  probe on the bad path (80 small datagrams all arrive, 27 of 80 game-sized spaced 40 ms, 4 of 80 in a burst).
* For us: first measure with our `net` lab loss / delay options whether the snapshots matter; the gate alone is
  small.

## How to test
Lab, 2 instances, the host in Cell, the guest joins over the lab link with loss and a bandwidth cap. Count the
host's MSG_CELL_STATE datagrams to that guest before it entered the Cell world. FAIL before: snapshots go out
during the whole transfer, transfer time X. PASS after: zero snapshots until the guest is in the host's Cell
world, transfer time shorter; partner cells appear within 1 s after the guest enters.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (code audit, not reproduced by us).
