#!/usr/bin/env python3
"""Start a new game at any stage from the galaxy menu via the stage chooser (cheat levels -unlock).

Coordinates are for the 1440x1080 windowed test profile. Usage:
  new_stage_game.py <cell|creature|tribe|civ|space> [-i N] [--slot X,Y | --slot N] [--npc-cities N] [--name NAME]
The planet slot is found in the galaxy menu by itself: every slot ring has a 48x48 UI window
with id 0x02E5A4A8 (coop.ui tree); --slot N takes the N-th of them, --slot X,Y a screen point.
Use a profile without saves (an empty Games folder): a slot with a save would load that save.
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from sporectl import Console  # noqa: E402
import lane  # noqa: E402
import subprocess  # noqa: E402

STAGE_ICON = {"cell": (720, 392), "creature": (800, 428), "tribe": (848, 496), "civ": (848, 578),
              "space": (798, 646)}
MODE = {"cell": "Cell", "creature": "Creature", "tribe": "Tribe", "civ": "Civ", "space": "Space"}
DICE_CARD, DICE_NAME, DICE_STUFF = (930, 486), (1102, 474), (1092, 495)
PLANET_NAME = (1010, 540)  # the text field of the last wizard step "Name Your Planet"
NEXT = (1086, 618)

# wizard steps after choosing the stage: ("click", x, y) / ("wait", frames)
STEPS = {
    "space": [DICE_CARD, NEXT, DICE_NAME, NEXT, NEXT, NEXT, NEXT, DICE_STUFF, NEXT, (1090, 608)],
    "civ": [DICE_CARD, NEXT, NEXT, NEXT, NEXT, DICE_STUFF, NEXT, (1090, 608)],  # card, theme, difficulty, specialty, city stuff, planet
    # diet card, sporepedia creature + confirm, theme, difficulty, planet name
    "tribe": [(852, 536), NEXT, (428, 220), (1400, 1058), NEXT, NEXT, (1090, 608)],
    # like tribe (GOG 2024, 2026-10-24): a diet card opens the Sporepedia, a creature, confirm, theme, difficulty
    "creature": [(852, 536), NEXT, (428, 220), (1400, 1058), NEXT, NEXT, (1090, 608)],
    "cell": [(900, 537), NEXT, NEXT, (1090, 608)],  # diet card, difficulty, planet name
}

# the two diet cards of the Cell wizard (GOG 2024, 1440x1080): wiz_cell_start.png of `--shots`
CELL_DIET = {"herb": (900, 537), "carn": (995, 537)}  # leaf, meat


SLOT_WINDOW = "0x02E5A4A8"  # the hotspot over a planet slot ring in the galaxy menu


def slot_centers(c):
    """screen centers of the planet slots in the galaxy menu (placed ones only)"""
    out = []
    for line in c.cmd("coop.ui tree 3").splitlines():
        m = re.search(r"id=" + SLOT_WINDOW + r" .*?\[(-?\d+) (-?\d+) (-?\d+) (-?\d+)\]", line)
        if m:
            x0, y0, x1, y1 = map(int, m.groups())
            if x0 > 0 and y0 > 0:  # one hotspot waits unplaced at [0 0 48 48]
                out.append(((x0 + x1) // 2, (y0 + y1) // 2))
    return out


def find_slot(c, index):
    """the index-th slot once the galaxy stopped moving (two equal reads)"""
    last = None
    for _ in range(60):
        cur = slot_centers(c)
        if cur and cur == last and len(cur) > index:
            return cur[index]
        last = cur
        c.cmd("wait.frames 30 20000")
    raise SystemExit(f"no planet slot in the galaxy menu (got {last})")


def chooser_open(c):
    """the "Choose A Stage" ring is shown: its caption window AND every parent are visible (flag
    bit 0). The caption exists, visible itself, while the chooser is closed: its parents then have
    fl=0x2, so a check for the caption alone passed after a click that did nothing."""
    out = c.cmd('coop.ui find "Choose A Stage"')
    flags = [int(m, 16) for m in re.findall(r"fl=0x([0-9A-Fa-f]+)", out)]
    return len(flags) >= 3 and all(f & 1 for f in flags[:3])


def city_count(c):
    return sum(1 for line in c.cmd("coop.civ").splitlines() if line.startswith("city "))


def intro_up(c):
    """the civ intro text box is shown (UI flag bit 0 = visible)"""
    m = re.search(r"fl=0x([0-9A-Fa-f]+)", c.cmd('coop.ui find "age of technology"'))
    return bool(m and int(m.group(1), 16) & 1)


def npc_cities(c, n):
    """A new Civilization game has only the player city; the others appear one by one through the
    cCitySimulator once the player claimed spice and has money (docs/re/civ.md "NPC cities").
    AppearNextCity(instant) skips that wait; each city comes with the next 1 s simulator tick."""
    # the intro text ("... soon other cities will arise ..."): its OK button unpauses the game
    for _ in range(60):
        if intro_up(c):
            c.cmd("click 1410 1044")
            c.cmd("wait.frames 60 20000")
            if not intro_up(c):
                break
        c.cmd("wait.frames 30 20000")
    m = re.search(r"eax=(0x[0-9A-Fa-f]+)", c.cmd("call Simulator::cCivExpansion::Get cdecl"))
    if not m or int(m.group(1), 16) == 0:
        print("FAILED: no city simulator")
        return 1
    cs = m.group(1)
    before = city_count(c)
    for _ in range(n):
        print("  appear:", c.cmd(f"call Simulator::cCivExpansion::SpawnNextCity thiscall {cs} 1 0").strip())
        c.cmd("wait.frames 90 30000")
    c.cmd("wait.frames 150 30000")
    after = city_count(c)
    print(f"cities: {before} -> {after}")
    return 0 if after >= before + n else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", choices=sorted(STAGE_ICON))
    ap.add_argument("-i", type=int, default=lane.HOST)
    ap.add_argument("--slot", default="0", help="planet slot: its index in the galaxy menu or a point X,Y")
    ap.add_argument("--npc-cities", type=int, default=0,
                    help="civ: let N cities of other nations appear right away (the game waits for spice and money)")
    ap.add_argument("--shots", action="store_true", help="screenshot every wizard step to out/shots")
    ap.add_argument("--diet", help="cell: the diet card, herb | carn | X,Y (default: the wizard's first card)")
    ap.add_argument("--no-settle", action="store_true", help="return as soon as the stage mode is up (its intro still runs)")
    ap.add_argument("--name", help="type this planet name (any letters, e.g. Cyrillic) instead of the game's")
    a = ap.parse_args()
    for _ in range(120):  # the game is up (its console answers)
        try:
            if Console(a.i, timeout=5).call("ping")["ok"]:
                break
        except OSError:
            time.sleep(1)
    c = Console(a.i)
    c.cmd = lambda line, timeout=None: c.call(line).get("out", "")
    for _ in range(180):  # the galaxy menu, settled (the intro may still run)
        if "GalaxyGameEntry" in c.cmd("game.mode"):
            break
        time.sleep(1)
    c.cmd("wait.frames 600 120000")  # a fresh profile flies into the galaxy first (~15 s)
    c.cmd("cheat levels -unlock")
    if "," in a.slot:
        sx, sy = map(int, a.slot.split(","))
    else:
        sx, sy = find_slot(c, int(a.slot))
    print("planet slot at", sx, sy)
    for _ in range(5):  # hover first: a click on a slot that was not hovered may do nothing
        c.cmd(f"move {sx} {sy}")
        c.cmd("wait.frames 15 10000")
        c.cmd(f"click {sx} {sy}")
        c.cmd("wait.frames 150 60000")
        if chooser_open(c):
            break
        if "," not in a.slot:
            sx, sy = find_slot(c, int(a.slot))
    else:
        print("FAILED: the stage chooser did not open")
        return 1
    x, y = STAGE_ICON[a.stage]
    c.cmd(f"click {x} {y}")
    c.cmd("wait.frames 150 60000")
    if a.shots:  # the first wizard page (the diet cards of the cell wizard)
        subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "..", "sporectl.py"), "-i", str(a.i),
                        "shot", os.path.join(lane.out_dir("shots"), f"wiz_{a.stage}_start.png"), "--scale", "0.4"],
                       stdout=subprocess.DEVNULL)
    steps = list(STEPS[a.stage])
    if a.stage == "cell" and a.diet:
        steps[0] = CELL_DIET[a.diet] if a.diet in CELL_DIET else tuple(map(int, a.diet.split(",")))
    for n, (x, y) in enumerate(steps):
        if a.name and n == len(steps) - 1:  # the last step names the planet: type over the game's name
            c.cmd(f"move {PLANET_NAME[0]} {PLANET_NAME[1]}")
            c.cmd(f"click {PLANET_NAME[0]} {PLANET_NAME[1]}")
            c.cmd("wait.frames 20 10000")
            c.cmd("key end")
            for _ in range(30):
                c.cmd("key back")
            c.cmd(f'text "{a.name}"')
            c.cmd("wait.frames 20 10000")
        c.cmd(f"move {x} {y}")
        c.cmd("wait.frames 10 10000")
        c.cmd(f"click {x} {y}")
        c.cmd("wait.frames 80 60000")
        if a.shots:
            path = os.path.join(lane.out_dir("shots"), f"wiz_{a.stage}_{n}.png")
            subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "..", "sporectl.py"), "-i",
                            str(a.i), "shot", path, "--scale", "0.4"],
                           stdout=subprocess.DEVNULL)
    t0 = time.time()
    while time.time() - t0 < 300:
        mode = c.cmd("game.mode")
        if MODE[a.stage].lower() in mode.lower():
            print("in", mode.strip())
            if a.no_settle:
                return 0
            c.cmd("wait.frames 600 120000")
            if a.stage == "civ" and a.npc_cities:
                return npc_cities(c, a.npc_cities)
            return 0
        time.sleep(2)
    print("FAILED: still", c.cmd("game.mode").strip())
    return 1


if __name__ == "__main__":
    sys.exit(main())
