"""BUG-9081: compare native trade routes and purchase progress on an isolated pair.

Start at an existing route/delivery checkpoint. This does not replay a campaign,
grant progress in the default check. --cycle deliberately removes/restores
routes and changes one native contribution to check ongoing replication.
"""
import argparse
import struct
import re

from coop_ability_bar import mem,u32
from duo_civ_founder_control import cities
from duo_orders import wait_until
from duo_villages import Console,command


def credit_nodes(peer,city):
    entries={};seen=set()
    def visit(node):
        if not node:return
        assert node not in seen and len(seen)<250
        seen.add(node)
        right,left,_,_,key,value=struct.unpack('<5If',mem(peer,node,24))
        entries[key]=(node,value)
        visit(left);visit(right)
    visit(u32(peer,city+0x57C))
    assert len(entries)==u32(peer,city+0x584)
    return entries


def state(peer):
    result={}
    rows=cities(peer)
    known={ptr:pol for _,ptr,pol,_ in rows}
    assert len(set(known.values()))==len(rows),'Requires one fixture city per nation'
    for _,city,pol,_ in rows:
        lo,hi=struct.unpack('<II',mem(peer,city+0x55C,8))
        assert hi>=lo and (hi-lo)%12==0 and hi-lo<=250*12
        routes=[]
        for offset in range(lo,hi,12):
            partner,_,kind=struct.unpack('<III',mem(peer,offset,12))
            assert partner in known
            routes.append((known[partner],kind))
        buy,buyer=struct.unpack('<fI',mem(peer,city+0x758,8))
        credits={key:pair[1] for key,pair in credit_nodes(peer,city).items()}
        result[pol]=(sorted(routes),buy,buyer,credits)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True)
    p.add_argument('--guest',type=int,required=True)
    p.add_argument('--remove',action='store_true')
    p.add_argument('--cycle',action='store_true',help='Remove and restore routes, then verify a changed native meter.')
    p.add_argument('--rejoin-port',type=int,help='Reload the shared checkpoint before checking live updates.')
    a=p.parse_args();h,g=Console(a.host),Console(a.guest)
    for peer in(h,g):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        assert 'Civ' in command(peer,'game.mode')
    if a.rejoin_port:
        def slot():
            match=re.search(r' location=(.*?) access=',command(g,'coop.savedb'))
            return match[1].casefold() if match else ''
        old=slot();assert old
        founders=command(g,'coop.founders')
        command(g,'coop.leave')
        wait_until(lambda:'players=1/4' in command(h,'coop.status'),'Guest did not leave',15)
        command(g,f'coop.join 127.0.0.1:{a.rejoin_port} DuoTribalGuest')
        wait_until(lambda:'players=2/4' in command(h,'coop.status') and 'Civ' in command(g,'game.mode')
                   and bool(slot()) and slot()!=old,'Guest did not load a fresh shared checkpoint',100)
        assert command(g,'coop.founders')==founders
        print('PASS reconnect loaded a new native slot with both founders',flush=True)
    initial=state(h)
    print('HOST',initial,'GUEST',state(g),flush=True)
    assert any(row[0] for row in initial.values()),'Requires an earned route'
    assert any(row[1]>0 for row in initial.values()),'Requires an actual delivery'
    def equal():
        host,guest=state(h),state(g)
        return host.keys()==guest.keys() and all(
            host[k][0]==guest[k][0] and abs(host[k][1]-guest[k][1])<.1 and host[k][2]==guest[k][2]
            and host[k][3].keys()==guest[k][3].keys()
            and all(abs(value-guest[k][3][pol])<.1 for pol,value in host[k][3].items())
            for k in host)
    wait_until(equal,'Native guest route/economic progress differs from host',12)
    print('PASS native routes, purchase meter, buyer and pricing credits agree',flush=True)
    if a.remove or a.cycle:
        rows=cities(h)
        edges={}
        for _,city,_,_ in rows:
            lo,hi=struct.unpack('<II',mem(h,city+0x55C,8))
            for at in range(lo,hi,12):
                partner,_,kind=struct.unpack('<III',mem(h,at,12))
                edges[tuple(sorted((city,partner)))]=kind
        for _,city,_,_ in rows:
            lo,hi=struct.unpack('<II',mem(h,city+0x55C,8))
            partners=[u32(h,at) for at in range(lo,hi,12)]
            for partner in partners:
                command(h,f'call Simulator::cCity::RemoveTradeRoute thiscall {city:#x} {partner:#x}')
        wait_until(lambda:equal() and not any(row[0] for row in state(g).values()),
                   'Guest retained a removed native route',12)
        print('PASS removed native routes disappear on guest',flush=True)
        if a.cycle:
            for (city,partner),kind in edges.items():
                command(h,f'call Simulator::cCity::AddTradeRoute thiscall {city:#x} {partner:#x} {kind}')
            wait_until(lambda:equal() and any(row[0] for row in state(g).values()),
                       'Recreated route did not reach guest',12)
            pol=next(pol for pol,value in state(h).items() if value[1]>5)
            city=next(ptr for _,ptr,p,_ in rows if p==pol)
            before=state(h)[pol]
            # A bounded native contribution change checks live replication; the
            # saved initial progress was earned by the trader's actual delivery.
            command(h,f'call Simulator::cCity::AddBuyProgress thiscall {city:#x} f:-5 {before[2]:#x}')
            assert abs(state(h)[pol][1]-before[1])>1,'Native meter did not change'
            wait_until(equal,'Changed economic meter did not reach guest',12)
            print('PASS recreated routes and changed native purchase meter',flush=True)
            # Change one existing native pricing value as a replication fixture,
            # without claiming this direct adjustment was earned through trade.
            entry=next(pair for _,ptr,_,_ in rows for pair in credit_nodes(h,ptr).values())
            command(h,f'mem.write {entry[0]+20:#x} {struct.pack("<f",entry[1]+1).hex()}')
            wait_until(equal,'Changed pricing credit did not reach guest',12)
            print('PASS changed native pricing credit reaches guest',flush=True)
            # A local stale entry must be erased, not retained as a zero-price key.
            command(g,'coop.fix civ.economic_state off')
            try:
                _,local,faction,_=cities(g)[0]
                assert faction not in state(h)[faction][3]
                command(g,f'call SporeCoop::CityTradeMap_Index thiscall {local+0x570:#x} {local+0x30:#x}')
                assert faction in state(g)[faction][3]
            finally:command(g,'coop.fix civ.economic_state on')
            wait_until(equal,'Stale guest pricing entry was not erased',12)
            print('PASS stale guest pricing entry removed',flush=True)
    for peer in(h,g):assert 'total faults: 0' in command(peer,'exc')


if __name__=='__main__':main()
