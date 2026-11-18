# SporeCoop Lineages: private Windows preview

Requires Spore with Galactic Adventures on supported Steam or GOG 2024 builds.
Every player must use the same Lineages version, gameplay mods and Creepy & Cute
content configuration. Start with two players and a separate test campaign.
Keep a pre-mod save backup for vanilla play.

## Install

1. Close Spore and open the standalone Lineages setup executable. If you
   received the ZIP instead, extract it and open SporeCoopSetup.exe.
2. Select the Galactic Adventures installation. Choose a frame rate cap from
   1 to 360 or leave it blank for Spore's default. Enable Borderless Window
   if you use Spore in windowed mode.
3. Click Install. If Windows denies access, run setup as administrator.
4. Launch Galactic Adventures normally. Select Co-op in the galaxy menu or F9.

You can change the cap and borderless mode in Spore's Graphic Settings and
apply both with its green check mark. Turn off Full Screen Mode before enabling
borderless. Spore may ask for a restart if you also change its screen size.

## Join friends

One player selects Start Server, then loads or starts the test campaign. Friends
can select a nearby server or enter its reachable address and port, then click
Join. The default game port is UDP 47810. Successful joins appear at the top
of the server list, newest first. Nearby hosts have a LAN label. Use the mouse
wheel over the list or drag its scrollbar. Save and Delete manage addresses in
the list. Passwords are optional and are kept in memory for the session.
Password protection controls admission. Co-op traffic itself is unencrypted.
For your existing Tailscale or Hamachi setup, use the host's VPN address and
ensure the PCs can reach each other. The VPN is configured separately.
Allow the game through Windows Firewall when prompted; do not disable it.

If a guest connects but the world download stalls, switch **Transfer** from
**Fast** to **Optimal** on the host's Co-op connection screen before hosting.
Optimal uses smaller packets and spaces out retries. It can take several minutes
for a full world, so keep **Fast** for links that already transfer reliably.
The setting is saved on the host and guests can use the same join steps.

In the Co-op lobby, **Copy Host Address** copies the host's detected addresses with
the current UDP port. Share the address reachable from your friends' network.
**I'm Ready** appears during a shared vote. **Send World** remains available
to the host when there is a loaded world and a guest.

While hosting, Lineages queues saves after campaign milestones. If ten minutes
pass without a confirmed checkpoint, it queues a fallback save. Both wait for
a safe gameplay state and do not send a world by themselves. Set `autosave=0`
under `[coop]` in `mods/coop.ini` to disable this behavior.

In Creature and Adventure third person, hold **Alt** and scroll down to widen
the camera by up to 18 degrees. Hold Alt and scroll up to return to vanilla.
Ordinary scroll still controls zoom. This is local on Steam and GOG 2024.

## Uninstall

Close Spore, open the same setup and click Uninstall. The installer restores
the original Steam API loader and removes the Co-op and graphics settings and
Lineages report archives. It preserves saves and the native creation library.
Use your pre-mod backup for vanilla play, since custom-founder
campaigns can depend on the mod.

## Lineages Cache

Cache-enabled builds automatically exchange locally authored creations with
connected friends in the background. In Sporepedia, open **Lineages Cache >
Shared Creations**. Creatures, buildings, vehicles and ships retain their embedded
names and authors. The co-op display name is recorded separately as the sharer.
The host relays guests' creations to other connected guests. Large libraries take
time to finish; imports wait while an editor or loading cinematic is active.

Received creations persist in that Spore profile's native library across solo
and co-op games, without an EA login. They are available to the game's normal
selection system; the cache does not force them to spawn or alter existing worlds.
Use matching installed content/mods. Arbitrary missing-part combinations and
Adventure/flora libraries have not been qualified.

Uninstall removes the mod's category and the cache in the current user's
standard Spore profile. Imported native creations stay in the Sporepedia.

## Preview qualification

This package includes selected upstream 0.7.9 stability fixes and the current
Lineages campaign code. Targeted two-instance Tribal and Creature checks passed.
Separate-PC Tailscale checks covered Cell world transfer, unchanged Cell
reconnect, and automatic loading of a prepared Creature world after reconnect.
An uninterrupted Cell-to-Space run, broad third-party mod compatibility and
three/four-player campaigns are not fully qualified.
Adventures are incomplete: shared combat, partner bodies and final rewards need
further work. Use this as a development preview, not a finished release.
