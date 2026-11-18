---
id: BUG-9227
title: Guests see a 'Paused by host' cover while the host's game is paused
status: fixed
kind: feature
stage: all
reported: 2026-11-15
source: owner play test 2026-11-15
role: guest
versions: 0.7.14.1-lineages58-dev
fixed_in: 0.7.14.1-lineages59-dev
branch: feat/lineages-47
commits: 95378ac3
tests: none yet (built; the owner's session was running)
signature:
docs:
related:
---

## Report
Owner play test 2026-11-15 (host Pandize on TITAN's Steam Spore, guest BigTexas over the internet, 58-dev): "When host pauses, it should add the loading overlay without the loading icon saying paused by host."

## Root cause
Not a bug: no general signal of the host's pause existed (upstream's BUG-1086 note is Tribe only).

## Fix
- The host sets PF_PAUSED (512) in its player state while its game time has stood a second or more in a stage (pause menu, a caption; not a cinematic). A guest in the same stage of the same world draws the loading screen's blurred cover without its ring, "Paused by host" and "<host> paused the game" (ring_timer::loading with radius 0). Switch ui.paused_cover.

## Verification
no-fail-run: built only; to be checked on TITAN + the MSI after the owner's play session

## History
- 2026-11-15: opened.
