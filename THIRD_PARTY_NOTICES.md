# Third-party notices

## Spore

Spore and Spore Galactic Adventures are trademarks of Electronic Arts Inc. SporeCoop is an
unofficial fan-made mod. It is not affiliated with, endorsed or supported by Electronic Arts or
Maxis. The repository contains no game files: you need your own copy of the game.

## MinHook

`third_party/minhook/`: MinHook, the minimalistic API hooking library for x64/x86,
Copyright (C) 2009-2017 Tsuda Kageyu, BSD 2-Clause license (see
[`third_party/minhook/LICENSE.txt`](third_party/minhook/LICENSE.txt)). It is compiled into
`sporekit.dll`. The vendored copy keeps thread freezing off (`MH_SetFreezeThreads(FALSE)`) because
it deadlocks under Wine.

## Monocypher

`third_party/monocypher/`: Monocypher 4.0.2, Copyright (c) 2017-2023 Loup Vaillant, Michael Savage
and Fabio Scotoni, dual-licensed BSD 2-Clause or CC0 1.0 (see
[`third_party/monocypher/LICENCE.md`](third_party/monocypher/LICENCE.md)); the release archive from
monocypher.org, its SHA-512 checked against the published one (2026-11-23). It is compiled into
`coop.dll` and the galaxy server (`mods/coop/net/galaxy_crypto.cpp`): the galaxy traffic's encryption.

## Spore ModAPI

The function names and addresses in `data/symbols/gog2016_922af7c7.sym` were ported to the GOG
2016 build of the game from the March 2017 address tables of
[Spore ModAPI](https://github.com/Spore-Community/Spore-ModAPI) (Spore Community) by
`tools/addrport/port_modapi.py`. ModAPI's code itself is not included. Thanks to its authors for
years of reverse-engineering work on Spore.

## Fonts and images

The co-op window uses the game's own fonts. The menu and notification icons in `data/ui/` are
drawn by `tools/make_icons.py` (part of this project).
