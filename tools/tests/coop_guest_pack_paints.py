#!/usr/bin/env python3
"""BUG-0369: a guest whose game lacks the Creepy & Cute pack (Steam sells it apart) must see the
host's creature painted with base game paints where the host used the pack's paints. Player report
(2026-11-01): "my friend sees my creatures as cubes and different color after using DLC colors and
parts".

A creature model names its paint layers (base, coat, detail) by their skin effect ids; the pack's
paints use effects only the pack has. The lane's `bp1content/BoosterPack_01.package` is moved aside
for the whole test (one lane: both games without the pack; put back at the end). The host's My
Creations: Tiger Horse (290229B9) with the pack's "BP1 Dottie" base and coat paint effects.

Checked on the guest (the game's view, `coop.creations paints`: the three effect ids of the model
its game reads for the host's creature): no effect of the pack left, the base layer is the base game
substitute; the guest builds the species (`coop.creations parts`: species=1) and plays on without a
fault. Screenshots of the guest's view of the host.

Runs that must FAIL: SPORE_FIX_OFF=creations.paint_substitute (the pack's effect ids stay: cc=2)."""
import os
import re
import time

import lane
import twoinst
from twoinst import Checks, shot, start

TIGER = 0x290229B9
BEM = 0x1A99B06B
# the model's base and coat effects (header bytes 26..33): Tiger Horse's own, and the pack's Dottie
OLD = bytes.fromhex('1be2d1ee231f8152')       # 0xEED1E21B, 0x52811F23
CC = bytes.fromhex('d48d39ff7ef17391')        # 0xFF398DD4 (BP1 Dottie base), 0x9173F17E (BP1 Dottie coat)
BASE_SUB = '8C077662'                         # content_packs.h: 0xFF398DD4 -> skinpaint_base_rubberband


def pack_path():
    return os.path.join(lane.game_dir(), 'bp1content', 'BoosterPack_01.package')


def patch_host_creations():
    """the host's My Creations: Tiger Horse painted with the pack's Dottie base and coat"""
    from coop_nest_species import read_pkg, write_pkg, fixtures_dir
    recs = read_pkg(os.path.join(fixtures_dir(), 'creations-host.package'))
    out, n = [], 0
    for t, g, i, data in recs:
        if t == BEM and i == TIGER:
            n = data.count(OLD)
            assert data[26:34] == OLD, 'Tiger Horse model: the effects are not at header byte 26'
            data = data[:26] + CC + data[34:]
        out.append((t, g, i, data))
    assert n == 1, f'Tiger Horse model: {n} places of the effects'
    write_pkg(os.path.join(lane.HOST_DIR, 'EditorSaves.package'), out)


def main():
    assert lane.LANE != 1, 'lane 1 is the real install: its pack is never moved'
    pk = pack_path()
    off = pk + '.bug0369-off'
    if os.path.exists(off) and not os.path.exists(pk):
        os.rename(off, pk)  # an earlier run that died
    orig = twoinst.restore_creations

    def restore(guest=True):
        orig(guest)
        patch_host_creations()
    twoinst.restore_creations = restore
    os.rename(pk, off)
    ck = Checks()
    try:
        host, guest = start('creature')
        print(' ', host.call(f'coop.creations choose creature {TIGER:08X}')['out'].strip())
        t0 = time.time()
        while time.time() - t0 < 30 and f'key=0x{TIGER:08X}' not in host.call('coop.avatar')['out']:
            time.sleep(1)
        h_av = re.search(r'key=0x([0-9A-F]{8})', host.call('coop.avatar')['out'])
        ck(h_av and h_av.group(1) == f'{TIGER:08X}', f'the host plays its creature with the pack\'s paint ({h_av and h_av.group(1)})')
        # the guest builds the host's puppet from the records co-op registers
        paints, parts = '', ''
        t0 = time.time()
        while time.time() - t0 < 60:
            paints = guest.call(f'coop.creations paints {TIGER:08X}')['out'].strip()
            parts = guest.call(f'coop.creations parts {TIGER:08X}')['out'].strip()
            if 'none' not in paints and re.search(r'\bspecies=1\b', parts):
                break
            time.sleep(2)
        hp = host.call(f'coop.creations paints {TIGER:08X}')['out'].strip()
        print('  guest:', paints, '|', parts)
        print('  host: ', hp)
        time.sleep(5)
        shot(guest, 'guest_sees_host_paints.png')
        ck(re.search(r'\bhave_pack=0\b', paints), f'the guest\'s game has no Creepy & Cute pack ({paints})')
        ck(re.search(r'\bcc=0\b', paints) and f' {BASE_SUB} ' in paints + ' ',
           f'the model the guest\'s game reads paints the host\'s creature with base game effects ({paints})')
        ck(re.search(r'\bspecies=1\b', parts), f'the guest\'s game builds the host\'s species ({parts})')
        ck(guest.call('coop.avatar')['ok'], 'the guest plays on')
        ck.faults(host=host, guest=guest)
    finally:
        # these games run without the pack: the next warm test must not reuse them
        twoinst.lane_state(**{**twoinst.lane_state(), 'fix_off': 'no-pack'})
        if os.path.exists(off) and not os.path.exists(pk):
            os.rename(off, pk)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
