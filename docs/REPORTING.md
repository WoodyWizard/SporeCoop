# Reporting a problem

Thanks for helping! One file from each player is usually all we need.

## 1. Get the report file

* **After a crash or a freeze:** SporeCoop makes the file by itself:
  `Spore\SporebinEP1\mods\logs\SporeCoop-report-<date-time>.zip`. The next time you start the game
  it tells you the file's name.
* **Anything else** (players out of sync, something does not work): press **F9** in the game, then
  **Report a problem**. It makes the same file and opens the folder with it selected.
* If both of you were playing, send the files of **both** players: the host's and the guest's.

The file holds the game build, Windows or Wine, memory and graphics card, the co-op session, the
logs of the last runs and the mod settings. Your player name is in it; the address you joined is
not.

## 2. Spore's own crash files (optional)

When Spore itself catches a crash it writes two files into your Windows user folder (or the game
folder): `Exception Report <PC> <date> exception.txt` and `... minidump.mdmp`. They help too, but
they contain your Windows user name and PC name; send them only if you are fine with that.

## 3. Tell us what happened

Open an [issue](https://github.com/WoodyWizard/SporeCoop/issues/new/choose) ("Bug report or
crash") and write:

* the SporeCoop version (the bottom line of the F9 window or the main menu) of both players;
* the game (GOG, Steam, EA app) and the system (Windows, Linux);
* who had the problem (host or guest) and in which stage;
* what you did just before ("the host finished migrating to a new nest, then the guest's game
  closed");
* how often it happens.

**Mark the moment:** when something goes wrong while you play, type `/bug <what you see>` into the
co-op window's chat (F9), for example `/bug only one of us can eat`. The chat says "marked for the
report" and the log of **every** player in the session gets that line with your name and the
moment, so the right minute of a two-hour game is found at once in all the logs.

Screenshots help a lot for "players see different things": one from each player at the same
moment.

No GitHub account? Post the same in the [SporeCoop Discord](https://discord.gg/7Yre7u4b8x) (the files can be attached there
too).

Please check [known issues](known-issues.md) first, and update to the
[latest version](https://github.com/WoodyWizard/SporeCoop/releases/latest): many problems are
already fixed there.
