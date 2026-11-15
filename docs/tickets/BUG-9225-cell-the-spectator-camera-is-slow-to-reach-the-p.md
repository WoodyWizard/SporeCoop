---
id: BUG-9225
title: Cell: the spectator camera is slow to reach the partner
status: fixed
kind: bug
stage: cell
reported: 2026-11-15
source: owner play test 2026-11-15
role: all
versions: 0.7.14.1-lineages58-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 95378ac3
tests: none yet (built; the owner's session was running)
signature:
docs:
related:
---

## Report
Owner play test 2026-11-15 (host Pandize on TITAN's Steam Spore, guest BigTexas over the internet, 58-dev): "Spectator cam on Cell is slow to reach the other player."

## Root cause
Inferred (docs/re/cell.md#cell-camera, BUG-0343): the camera eases its drawn point +0x48 with factors from the avatar's speed; a knocked-out cell stands still, so the point crawled to the watched partner.

## Fix
- When the watch starts or switches, and whenever the camera trails the partner by more than 8 units, the camera point jumps to the partner (camera_snap). Switch cell.spectate_snap.

## Verification
no-fail-run: built only; to be checked on TITAN + the MSI after the owner's play session

## History
- 2026-11-15: opened.
