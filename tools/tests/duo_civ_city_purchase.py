"""BUG-9082: checkpoint-only native negotiation and authoritative city purchase.

Requires isolated Clyth with one guest economic capital and one rival. Grants
eligibility and funds as explicit fixture setup, then clicks native dialogue.
Selection and the trading journey are qualified separately. No stage replay.
"""
import argparse,random,re,struct
import play_civ
from coop_ability_bar import mem,u32
from duo_civ_capture_owners import ownership
from duo_civ_founder_control import cities
from duo_civ_species import native
from duo_civ_economic_state import state
from duo_orders import wait_until
from duo_tribe_graduation_editor import button
from duo_villages import Console,command


def caption_point(c,text):
    result=c.call('coop.ui find '+text)
    if not result['ok']:return None
    if any(not int(flag,16)&1 for flag in re.findall(r'fl=0x([0-9a-fA-F]+)',result['out'])):return None
    # find prints the matched control followed by each parent, local rectangles.
    rects=[tuple(map(float,m)) for m in re.findall(r'\[(-?[\d.]+) (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\]',result['out'])]
    if not rects:return None
    a,b,x,y=rects[0]
    px=round((a+x)/2+sum(r[0] for r in rects[1:]))
    py=round((b+y)/2+sum(r[1] for r in rects[1:]))
    return px,py


def click_caption(c,text):
    point=caption_point(c,text)
    if not point:return False
    command(c,f'click {point[0]} {point[1]}')
    command(c,'wait.frames 12 10000')
    return True


def money(c):
    return float(re.search(r'player=1 .*?wealth=([\d.]+)',command(c,'coop.civ nations'))[1])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True);p.add_argument('--guest',type=int,required=True)
    p.add_argument('--transaction-off',action='store_true')
    p.add_argument('--cancel-first',action='store_true',help='Verify native cancellation and no charge before purchasing.')
    p.add_argument('--refuse-first',action='store_true',help='Verify a rejected low offer resets eligibility without payment.')
    p.add_argument('--rejoin-port',type=int)
    a=p.parse_args();h,g=Console(a.host),Console(a.guest)
    for c in(h,g):
        assert 'duoqa' in command(c,'coop.savedb').lower()
        assert "world 'Clyth.spo'" in command(c,'coop.avatar')
    home=next(row for row in cities(h) if row[3]==1)
    rival=next(row for row in cities(h) if row[3]==-1)
    assert u32(h,home[1]+0x540)==2
    command(h,f'call Simulator::cCity::AddBuyProgress thiscall {rival[1]:#x} f:200 {home[2]:#x}')
    native(h,f'SporeCoop::CityTradeMap_Index thiscall {rival[1]+0x570:#x} {home[1]+0x30:#x}')
    wallet=u32(h,next(row[1] for row in cities(h) if row[3]==0)+0x590)
    command(h,f'call Simulator::cCivilization::AddMoney thiscall {wallet:#x} f:100000 0')
    wait_until(lambda:state(g)[rival[2]][1]>=99,'Guest eligibility missing',12)
    print('FIXTURE full trade meter and shared funds; no trade journey repeated',flush=True)
    play_civ.W,play_civ.H=map(int,re.search(r'screen (\d+) (\d+)',command(g,'coop.uistate')).groups())
    play_civ.HUD_BOTTOM=play_civ.H-95
    driver=play_civ.Player('Purchase',g,1,random.Random(0),[])
    trader=next(i for i,v in play_civ.civ_state(g)[1].items() if v['owner']==1 and v['purpose']==2)
    command(g,f'coop.civ select {trader} 1')
    def dialogue_open():
        return bool(re.search(r'^04AB5E50 [\d.]',command(g,'coop.ui where 04AB5E50'),re.M))
    def negotiate():
        if dialogue_open():return
        point=driver.look_at(play_civ.civ_state(g)[0][rival[0]]['pos'],box=(330,240,820,380),tries=30)
        assert point
        for dy in(-20,0,-38):
            command(g,f'click {round(point[0])} {round(point[1])+dy}')
            command(g,'wait.frames 10 10000')
            if button(g,'037E95BB'):break
        else:raise AssertionError('Native Buy City button missing')
        wait_until(dialogue_open,'Native negotiation did not open',15)
        command(g,'wait.frames 40 10000')
    negotiate()
    if a.cancel_first:
        before=money(h)
        command(g,'wheel 580 400 -4');command(g,'wait.frames 10 10000')
        assert click_caption(g,'Nevermind...'),'Native cancel response missing'
        wait_until(lambda:not dialogue_open(),
                   'Native cancellation did not close negotiation',15)
        assert abs(before-money(h))<1000,'Cancellation charged the shared treasury'
        assert state(h)[rival[2]][1]>=99 and state(g)[rival[2]][1]>=99,'Cancellation lost purchase eligibility'
        print('PASS native cancellation retains eligibility without payment',flush=True)
        command(g,'wait.frames 20 10000')
        negotiate()
    if a.refuse_first:
        local_wallet=u32(g,next(row[1] for row in cities(g) if row[3]==0)+0x590)
        quote=struct.unpack('<f',mem(g,local_wallet+0x448,4))[0]
        assert quote>4120,'Fixture quote cannot guarantee a rejected 4,000 offer'
        before=money(h)
        if not caption_point(g,'Oops.'):
            first=caption_point(g,'32,000 Sporebucks')
            assert first,'Native offer list missing'
            # Five native rows are 31 pixels apart. The 4,000 substring alone
            # would match 24,000, and the generic list ID also names the text pane.
            command(g,f'click {first[0]} {first[1]+4*31}')
            command(g,'wait.frames 40 10000')
        wait_until(lambda:caption_point(g,'Oops.'),'Native low offer was not refused',15)
        assert click_caption(g,'Oops.')
        wait_until(lambda:state(h)[rival[2]][1]<1 and state(g)[rival[2]][1]<1,
                   'Refused offer did not reset both native purchase meters',15)
        assert abs(before-money(h))<1000,'Refused offer charged shared funds'
        print('PASS native low-offer refusal resets both meters without payment',flush=True)
        command(h,f'call Simulator::cCity::AddBuyProgress thiscall {rival[1]:#x} f:200 {home[2]:#x}')
        wait_until(lambda:state(g)[rival[2]][1]>=99,'Renewed eligibility missing',12)
        negotiate()
    if a.transaction_off:command(g,'coop.fix civ.economic_purchase off')
    before=money(h);history=u32(h,wallet+0x4C8)
    assert click_caption(g,'32,000 Sporebucks')
    wait_until(lambda:caption_point(g,'Yay!'),'Native offer was not accepted',15)
    command(g,'wait.frames 30 10000')
    assert click_caption(g,'Yay!')
    def acquired(c):
        return any(i==rival[0] and pol==home[2] and owner==1 for i,_,pol,owner in cities(c))
    wait_until(lambda:acquired(h) and acquired(g),'Accepted guest purchase did not transfer city on both peers',20)
    assert 30000<before-money(h)<33000,'Purchase did not debit the shared treasury exactly once'
    assert u32(h,wallet+0x4C8)==history+1,'Team economic history not credited once'
    assert abs(money(h)-money(g))<100,'Shared treasury differs'
    parts=ownership(h,rival[0])
    wait_until(lambda:ownership(g,rival[0])==parts,'Purchased city part owners differ',12)
    assert parts[0]==home[2] and parts[1] and set(parts[1]+parts[2])=={home[2]}
    for c in(h,g):assert 'total faults: 0' in command(c,'exc')
    print('PASS native guest negotiation, own-founder capture, one shared payment and team history',flush=True)
    if a.rejoin_port:
        original=command(g,'coop.savedb')
        command(g,'coop.leave')
        wait_until(lambda:'players=1/4' in command(h,'coop.status'),'Guest did not leave',15)
        command(g,f'coop.join 127.0.0.1:{a.rejoin_port} DuoTribalGuest')
        wait_until(lambda:'Civ' in command(g,'game.mode') and command(g,'coop.savedb')!=original and acquired(g),
                   'Purchased city did not survive reconnect into a fresh slot',100)
        assert ownership(g,rival[0])==ownership(h,rival[0])
        assert u32(h,wallet+0x4C8)==history+1,'Reconnect duplicated team capture history'
        for c in(h,g):assert 'total faults: 0' in command(c,'exc')
        print('PASS purchased city, part owners and single history credit survive reconnect',flush=True)


if __name__=='__main__':main()
