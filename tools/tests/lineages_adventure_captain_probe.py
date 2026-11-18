"""Disposable native act-reset fixture for experimental Adventure captain bodies.

This directly reinitializes the current act. It proves callback/body lifetime
safety for this fixture, not earned progression, combat or full mission play.
"""
import re
import time
from coop_ability_bar import sym,u32
from duo_villages import Console,command
from lineages_adventure_state import read_state

for c in (Console(4),Console(5)):
    state=read_state(c)
    assert state['state']==3
    def bodies():
        return [int(m[1],16) for m in re.finditer(r'creature ([0-9A-F]+).*puppet=1',command(c,'coop.npc herds'))]
    before=bodies(); assert len(before)==1,before
    body=before[0]; serial=u32(c,body+0x24)
    assert u32(c,body+0x1C)==0xFFFFFFFE and u32(c,body+0x18)
    for repeat in range(2):
        mode=u32(c,sym(c,'App::cScenarioMode::ptr')); play=u32(c,mode+0x78)
        # Native signature checked against the prologue and ret 8, BUG-9089.
        command(c,f"call Simulator::cScenarioPlayMode::SetCurrentAct thiscall {play:#x} {state['act']} 0")
        time.sleep(1)
        assert bodies()==before,'Act reset destroyed or duplicated the partner'
        assert u32(c,body+0x24)==serial,'Partner object address was reused'
        assert read_state(c)['act']==state['act']
        assert re.search(r'total faults: 0\b',command(c,'exc'))
    print(f'PASS partner {body:08X}: two native act resets, same identity, zero faults.')
