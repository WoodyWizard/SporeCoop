#!/usr/bin/env python3
"""BUG-0469 test fixtures: a model the game has, with a part this game does not have.

The guest lane of the BUG-0469 tests has `Data/Spore_Pack_03.package` moved aside (as
coop_unknown_parts.py does), so the part 0x75A5D2D4 (a detail of that pack) is unknown there. A base
game model record of the lane's `Data/Spore_Content.package` (a civ vehicle or a UFO) is copied with
one of its part ids replaced by that pack part and written into the guest's My Creations
(`EditorSaves.package`, the database the game reads creations from) under an instance of the test's
own: the game has the model key, and its model names a part the game does not have. The test then
makes the mod build that model through the path under test (a mirror vehicle, a host ship).

  import model_fixture as mf
  mf.install(guest_spore_dir, 'vehicle', 0x29030001)

The model part ids are read with the same layout as `model_part_refs` in mods/coop/creations.cpp
(byte-run stream, the group's last three bytes, the slot and parent after the id must be plausible):
docs/re/creations.md#creations-unknown-parts.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import part_substitutes as ps  # noqa: E402

from coop_nest_species import fixtures_dir, read_pkg, write_pkg  # noqa: E402

PACK_PART = 0x75A5D2D4
PACK_PART_BYTES = PACK_PART.to_bytes(4, 'little')
BEM = 0x1A99B06B
PROP = 0x00B1B104
MAX_PART_SLOT = 0x10000
# kind -> (creation type, model group of the bem record, the group's last three bytes: the part group)
KINDS = {
    'vehicle': (0x24682294, 0x40646200, 0x40646000),
    'ufo': (0x476A98C7, 0x40656200, 0x40646000),
}


def _decode_run(data, j, last, n):
    """n bytes of the model's byte-run stream from j (0xFE n: the byte before, n more times; FE 00:
    the byte 0xFE), as creations.cpp decode_run; None: past the end"""
    out = bytearray()
    while len(out) < n:
        if j >= len(data):
            return None
        b = data[j]
        if b != 0xFE:
            out.append(b)
            last = b
            j += 1
            continue
        if j + 1 >= len(data):
            return None
        run = data[j + 1]
        if run == 0:
            out.append(0xFE)
            last = 0xFE
            j += 2
            continue
        j += 2
        for _ in range(run):
            if len(out) >= n:
                break
            out.append(last)
    return bytes(out)


def part_ids(data, group):
    """the part ids of these model record bytes (creations.cpp model_part_refs)"""
    g = bytes(((group >> 8) & 0xFF, (group >> 16) & 0xFF, (group >> 24) & 0xFF))
    out = []
    for i in range(len(data) - 14):
        if data[i:i + 3] != g:
            continue
        j, ident = i + 3, bytearray()
        while len(ident) < 4 and j < len(data):
            if data[j] == 0xFE:
                if j + 1 >= len(data) or data[j + 1] != 0:
                    break
                ident.append(0xFE)
                j += 2
            else:
                ident.append(data[j])
                j += 1
        if len(ident) != 4:
            continue
        val = int.from_bytes(ident, 'little')
        if not val:
            continue
        fields = _decode_run(data, j, ident[3], 8)
        if fields is None:
            continue
        slot = int.from_bytes(fields[0:4], 'little')
        parent = int.from_bytes(fields[4:8], 'little')
        if not (slot == 0xFFFFFFFF or slot < MAX_PART_SLOT) or not (parent == 0xFFFFFFFF or parent < MAX_PART_SLOT):
            continue
        out.append(val)
    return out


def model_with_pack_part(game_dir, kind):
    """the bem record of a base game model of that kind, one part id replaced by the pack part"""
    _, group, parts = KINDS[kind]
    pg = parts.to_bytes(4, 'little')[1:]  # the group's last three bytes (its 0x00 is a byte run)
    path = os.path.join(game_dir, 'Data', 'Spore_Content.package')
    for e in ps.entries(path):
        if e['type'] != BEM or e['group'] != group:
            continue
        data = ps.read(path, e)
        if PACK_PART in part_ids(data, parts):
            continue
        for i in range(len(data) - 7):
            if data[i:i + 3] != pg:
                continue
            ident = data[i + 3:i + 7]
            if 0xFE in ident or data[i + 7] == 0xFE:
                continue  # a 0xFE byte in or after the id: not an id a substitute fits in place
            out = data[:i + 3] + PACK_PART_BYTES + data[i + 7:]
            if len(out) == len(data) and PACK_PART in part_ids(out, parts):
                return out  # the patch really is a part the model names
    raise AssertionError(f'no {kind} model of group {group:#x} with a part id to replace')


def install(spore_dir, kind, inst):
    """the lane's My Creations with such a model under `inst` (a key of the test's own); the guest's
    own creations of the fixture stay, so the profile looks like a player's"""
    recs = [r for r in read_pkg(os.path.join(fixtures_dir(), 'creations-guest.package')) if r[2] != inst]
    _, group, _ = KINDS[kind]
    data = model_with_pack_part(_game_dir(), kind)
    recs.append((BEM, group, inst, data))
    os.makedirs(spore_dir, exist_ok=True)
    write_pkg(os.path.join(spore_dir, 'EditorSaves.package'), recs)
    return data


def _game_dir():
    """the game folder of this test's lane (the base game models of Data/Spore_Content.package)"""
    import lane
    return lane.game_dir(lane.LANE)
