---
id: BUG-9071
title: Qualify each founder's own spacecraft, captain and Space tutorial
status: open
kind: bug
stage: Space
reported: 2026-11-06
source: Personal launch requirement and Vantiegan shared-victory lab
role: host guest
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests: tools/tests/duo_civ_launch_editor.py --both-ships
signature: same empire spacecraft key after independent native first-ship editors
docs: docs/re/space.md
related: BUG-9070 BUG-9056
---

The old handoff lets the host run the introduction and gives the guest
the first orbital checkpoint. Each founder now needs their own launch.
own-ships-editors.log shows both connected players can enter a blank native
spacecraft editor using History on each machine. Each saved a design and
entered Space. The two empire ship resource keys read back equal despite
different hull choices. The profiles came from cloned saves; equal local keys
alone do not establish identical model contents. Compare actual hull parts,
captain resources and independent Mission Control choices. Qualify rejoin
and interrupted setup before exposing the workflow to players.

own-ships-b-play.log: both local games opened separate ship editors, saved,
entered Space, accepted separate captain names and offered the native
Mission Control flight tutorial. Neither player had to reach orbit for the
other to reach this choice. Both games then read the same active empire UFO
key 29179BD8 and parts AB10C9B6, 33541AB2 and 33541AB4. The guest's captain
key was empty and the host's was populated. The different-hull regression
remains open. Both processes exited cleanly with zero recorded faults.

The guest's cached multiplayer bundle contained a different model record
from the host's. The package registered under the original empire key on
the guest matched the host's bytes. own-ships-c-play.log verifies that
protecting the personal model keeps hull AB10C9B8 on the guest and AB10C9B6
on the host, while the host's multiplayer copy still uses its separate key.
The same test identified a second issue: accepting training did not protect
the guest against a queued orbital world share. The host and guest both
ended in native context 1 despite the guest choosing training.

With the received save staged until the guest departs its own homeworld,
own-ships-d-choices.log and own-ships-d-exit.log show actual contexts 1
(host in orbit) and 0 (guest still on the planet), the two distinct hulls
and clean exits. The first captain naming handler E1DC60 reads one source
key at E1DCC9, copies it into the native clone request, and assigns the
result through C31100 at E1DD8C. Supplying the guest's owned founder there
created Stabbella (1).png and a nonzero native captain key in run E, whereas
the control left the guest key empty. The first immediate body readback
failed. Verify committed model data, rejoin and the actual outfitter.

own-ships-f-play.log verifies the two native captain models against the
part IDs of their own founder creations. A native EditorSaves flush makes
the generated BEM visible before model transfer or a checkpoint. The real
guest captain screen displayed Recruit GuestCaptain and the orange guest
species. Its own outfitter accepted placement of a hat, then refused Save.
own-ships-f-validation.log captured finish state 2 and only blocking bit
0x20. The scoped inherited-part exception now requires the captain
outfitter request and the locally bound personal captain. Other error bits
and unrelated editors keep their native validation.

personal-orbit-h-outfit.log verifies a committed changed guest BEM digest
with unchanged host captain and both ships. personal-orbit-i-rejoin.log
repeats the model checks across a cold load and rejoin, checks the actual
remote UFO model on each peer twice and verifies the shared balance. Both
processes exited with zero native faults. Guest onboarding without the
host's original launch vote still needs explicit coverage.

late-space-b-joined.log covers a guest who missed the host's launch vote.
The guest joined from its saved ten-city victory, kept its Civilization
History screen while the orbital save waited, designed a ship and named a
captain whose parts matched Stabbella. Mission Control offered the guest
its own tutorial. The host stayed in Space. Both reported zero faults.
A join from the galaxy menu or an earlier Civilization save remains open.

retain-civ-a captured the confirmed ten-city native campaign outside Games.
menu-space-a started the host in orbit and the guest at the galaxy menu.
The guest recovered the native victory and passed its own ship, captain and
first Mission Control choices. Both games exited cleanly. The older
two-city guest also recovered all ten cities, but its first replay stalled
between captain naming and Mission Control. Check the visible native prompt
and compare the next run before counting that case as passed.

early-launch-b-start.log observed one city per nation before joining.
The regression checked the recovered native city ownership (five each),
ship creation, captain body, Mission Control and both fault counters.
The repeat passed. The first naming stall's cause remains undetermined.
tutorial-link-a-rejoin.log covers a network disconnect at the native
tutorial choice. Rejoining preserved the open dialogue, planetary context,
ship parts and captain model digest while the host stayed in orbit.
The full native flight lesson and a game restart while it is unfinished
remain open.

Cold-restart policy: keep the original spacecraft and captain and restart
the unfinished lesson. Cold-lesson-a exposed an incorrect Creature avatar
check in Civilization. In cold-lesson-b direct replay of the ship setter
and stage-transition presentation stalled on the Space loading screen.
Use the History editor's existing-creation path for the next replay.

cold-lesson-c-before-close.log shows the native mission at six remaining
after the guest popped an orb. cold-lesson-e-rejoin.log reopens a copy of
that profile and checks the original ship key and hull parts, original
captain key and BEM digest, a saved-ship confirmation in the real editor,
no new naming prompt, and seven remaining after accepting the lesson.
Both peers exited with zero faults. The earlier D screenshot shows seven
in the visible HUD too; the textual "Visit 7" lives in a tooltip, so a
visible-parent assertion on that string had incorrectly failed the run.
