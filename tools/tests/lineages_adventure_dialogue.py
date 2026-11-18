"""Finish one already-open native conversation; assert the idle partner's goals.

Use a disposable Adventure Town pair. Positioning/opening the conversation is
fixture setup; every page and the final acknowledgement use native UI input.
No native credit calls or progress writes are used by this regression.
"""
import argparse
import json
import math
import re
import time
from duo_villages import Console, command
from duo_orders import wait_until
from lineages_adventure_state import read_state


def position(c):
    return tuple(map(float,re.search(r'pos=\(([-.\d ]+)\)',command(c,'coop.avatar'))[1].split()))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--speaker',type=int,default=4)
    p.add_argument('--listener',type=int,default=5)
    p.add_argument('--reconnect',action='store_true',help='Listener leaves before credit, then rejoins host 47810')
    a=p.parse_args()
    speaker,listener=Console(a.speaker),Console(a.listener)
    before=read_state(speaker)
    assert before==read_state(listener),'Start with matching native goals'
    assert any(g['kind']==2 and not g['done'] for g in before['goals'])
    idle=position(listener)
    if a.reconnect:
        assert a.speaker==4 and a.listener==5,'Recovery fixture requires host 4 and guest 5'
        command(listener,'coop.leave'); time.sleep(1)
    for page in range(12):
        if read_state(speaker)['completed']>before['completed']: break
        def ready():
            hit=command(speaker,'coop.ui at 1148 634')
            return '062EF353 ' in hit or '0445C99D ' in hit
        wait_until(ready,'No native conversation next/finish button',10)
        command(speaker,'move 1148 634'); time.sleep(.3)
        command(speaker,'click 1148 634'); time.sleep(1.2)
    else:
        raise AssertionError('Conversation exceeded bounded page budget')
    if a.reconnect:
        assert read_state(listener)==before,'Offline captain advanced without receiving a credit'
        command(listener,'coop.join 127.0.0.1:47810 AdventureGuest')
    wait_until(lambda: read_state(speaker)==read_state(listener),
               'The idle captain did not receive the shared native Talk goal',12)
    after=read_state(speaker)
    assert after['completed']==before['completed']+1,'Credit duplicated'
    assert math.dist(idle,position(listener))<2,'The idle captain moved'
    time.sleep(2)
    assert read_state(speaker)==after==read_state(listener),'Retry duplicated or changed credit'
    for c in (speaker,listener):
        assert re.search(r'total faults: 0\b',command(c,'exc'))
    print(json.dumps(after,indent=2))
    print('PASS native conversation: shared once, idle partner, zero faults.')


if __name__=='__main__': main()
