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

## Spore ModAPI

The function names and addresses in `data/symbols/march2017_dc04aee5.sym` come from the March 2017
address tables of [Spore ModAPI](https://github.com/Spore-Community/Spore-ModAPI) (Spore Community),
used as they are for the GOG 2024 build (earlier releases also carried a port of them to the GOG 2016
build). ModAPI's code itself is not included. Thanks to its authors for
years of reverse-engineering work on Spore.

## Fonts and images

The co-op window uses the game's own fonts. The menu and notification icons in `data/ui/` are
drawn by `tools/make_icons.py` (part of this project).
