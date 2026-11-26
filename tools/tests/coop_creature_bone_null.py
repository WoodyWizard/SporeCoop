#!/usr/bin/env python3
"""BUG-1576: Civ/Creature cCreatureBase::GetBoneTransform (0x00C11CB0) & AttachToBone (0x00C120B0)
null mpAnimatedCreature (+0xB54) / mpModel (+0x17C) guard.

Reproduces the 3x Civ crash at GOG 0x00C11CEF (Steam 0x00B81CEF, reading [eax+0x17C] with eax=0)
from Vlaid18's Civ Together and Civ Rivals crash reports:
  1. Boot Civ fixture, verify `coop.boneguard` reports `hooked=1 fix=1`.
  2. Run `coop.boneguard probe` (invokes hooked `Creature_GetBoneTransform` on a zeroed creature
     with `+0xB54 == nullptr`, `bone_idx = 25`, `world_space = 1`).
  - ON: returns `ret=0` cleanly, `xf_guarded >= 1`, 0 faults, game stays alive.
  - Twin OFF (`SPORE_FIX_OFF=game.creature_bone_null`): faults at 0x00C11CEF reading 0x0000017C
    -> MUST FAIL.

  SPORE_LANE=1 python3 tools/tests/coop_creature_bone_null.py
"""
import re
import sys

from twoinst import Checks, start


def bone_status(con):
    out = con.call('coop.boneguard').get('out') or ''
    d = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', out)}
    return d, out.strip()


def main():
    host, guest = start('civ')
    ck = Checks()

    st0, txt0 = bone_status(host)
    print('  initial:', txt0, flush=True)
    ck(st0.get('hooked') == 1 and st0.get('xf_hook') == 1 and st0.get('attach_hook') == 1,
       f'GetBoneTransform and AttachToBone hooks installed ({txt0})')

    try:
        probe = (host.call('coop.boneguard probe').get('out') or '').strip()
        print('  probe:', probe, flush=True)
        ck('boneguard probe: ret=0' in probe, f'GetBoneTransform returned false cleanly on null +0xB54 ({probe})')
    except Exception as e:
        ck(False, f'GetBoneTransform probe crashed or disconnected: {e}')
        return ck.result()

    st1, txt1 = bone_status(host)
    print('  after:', txt1, flush=True)
    ck(st1.get('xf_guarded', 0) >= st0.get('xf_guarded', 0) + 1,
       f'creature bone null guard incremented xf_guarded ({txt1})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
