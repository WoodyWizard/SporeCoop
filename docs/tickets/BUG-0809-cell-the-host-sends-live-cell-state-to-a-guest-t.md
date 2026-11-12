---
id: BUG-0809
title: Cell: the host sends live Cell state to a guest that is not in its Cell world yet (bandwidth during the world transfer), suspected
status: fixed
kind: bug
stage: Cell
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 BUG-9098 (lineages30-dev, still open there)
role: host guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-own-world
commits: dda0afe5, 1843ae9a
tests: own_cell
signature: world transfer to a joining guest crawls on a lossy / tunnelled link while MSG_CELL_STATE goes out
docs: docs/re/cell.md#cell-own-world-pool, docs/re/host-centric.md#host-centric-own-world, docs/06-testing.md#testing-own-world
related: BUG-0405
---

## Report
Fork finding on a constrained UDP path (separate networks, tunnel): a 3.4 MB world that arrives in 6.5 s on a
good link delivered 40 KB in 54 s. The host also sent live Cell snapshots to the guest still in the Galaxy
menu; they are useless there. With the snapshots held back until the guest was in the host's Cell world, the
host's send rate dropped from ~270 to ~22 datagrams/s and the transfer went about twice as fast.

## Root cause
Measured on our build: the host sends every guest its live Cell state every 0.1 s (host_tick send_state) with no check of where the guest is. A guest in a Cell world of its own (and so a guest in the menu, a load or the world download) still got ~206 KB/s (lab 20261112-030026, the switch off), the same as a guest in the host's world (239 KB/s), and dropped all of it at dispatch. The effect on a world transfer over a lossy link was not measured (the fork's numbers).

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

## Fix
host_tick skips the per-guest MSG_CELL_STATE for a guest whose latest PlayerState is not the host's Cell world (own_world::peer_here: same mode, same world id, the host's world known). The 2 s food total (MSG_CELL_FOOD, reliable) still goes. `coop.pool` on the host: `cell_send: held_not_here=N`. Switch `coop.fix cell.send_here off`. The fork's paced transfer is not taken.

## Verification
Test `own_cell` step 3: the host's bytes to the guest (`coop.status` `net to 1: out_bytes`) over 15 s with the guest in its own Cell world against 15 s in the host's world.
* FAIL: out/lab/20261112-030026 (`SPORE_FIX_OFF=cell.send_here`, lane 4): 206 219 B/s (239 KB/s in the host's world).
* PASS: out/lab/20261112-030818, out/lab/20261112-031057 (lane 4): about 2.6 KB/s (238-241 KB/s in the host's world); also 20261112-025009 and 030305 (step 3 passed there).

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (code audit, not reproduced by us).
- 2026-11-12 (fix/w48-own-world, O1 of 0.7.13): fixed with cell.send_here (own_world::peer_here); FAIL 20261112-030026, PASS 20261112-030818 / 031057.
