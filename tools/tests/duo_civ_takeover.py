"""Measure native city takeover effort on joined disposable four-seat profiles."""
import argparse
import json
from pathlib import Path
import re
import struct

from coop_ability_bar import mem, u32
from duo_civ_founder_control import cities
from duo_civ_species import native
from duo_civ_vehicle_combat import health
from duo_orders import wait_until
from duo_villages import Console, command


def followers(peer, city):
    # Native EASTL map<uint32,int>: anchor+12 root, nodes key+16/value+20.
    root = u32(peer,city+0x5AC)
    wanted = u32(peer,city+0x5B4)
    assert wanted <= 128
    result,seen = {},set()
    def walk(node):
        if not node: return
        assert node not in seen and len(seen)<128
        seen.add(node)
        right,left,_,_,pol,count = struct.unpack('<5Ii',mem(peer,node,24))
        result[pol] = count
        walk(left); walk(right)
    walk(root)
    assert len(result)==wanted,(result,wanted)
    return result


def preach(peer, city, attacker, count=1):
    before = followers(peer,city).get(attacker,0)
    command(peer,f'call Simulator::cCity::ConvertCitizens thiscall {city:#x} {attacker:#x} {count}')
    return followers(peer,city).get(attacker,0)-before


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=int,required=True)
    parser.add_argument('--guest',type=int,required=True)
    parser.add_argument('--finish',choices=('military','economic'),help='Complete a native takeover after measuring effort.')
    receipts = parser.add_mutually_exclusive_group()
    receipts.add_argument('--save-receipt',type=Path)
    receipts.add_argument('--cold-receipt',type=Path)
    args = parser.parse_args()
    h,g = Console(args.host),Console(args.guest)
    for peer in (h,g):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        assert "world 'Clyth.spo'" in command(peer,'coop.avatar')
    assert 'city takeover effort 115%' in command(h,'coop.status')
    assert 'Host city takeover effort: 115%' in command(g,'coop.status')
    rows = cities(h)
    own = next(row for row in rows if row[3]==0)
    partner = next(row for row in rows if row[3]==1)
    rival = next(row for row in rows if row[3]==-1)
    city,attacker = rival[1],partner[2]
    if args.cold_receipt:
        saved = json.loads(args.cold_receipt.read_text())
        city = next(ptr for _,ptr,_,_ in rows if u32(h,ptr+0x24)==saved['serial'])
        attacker = saved['attacker']
        assert followers(h,city).get(attacker,0)==saved['followers']
        assert preach(h,city,attacker)==1, 'Reload lost the previously withheld fraction'
        print('PASS cold religious fraction resumes with a whole converted citizen',flush=True)
        # Complete an actual native conversion and verify both cities change owner.
        index = next(i for i,ptr,_,_ in rows if ptr==city)
        def converted():
            if next(pol for i,_,pol,_ in cities(h) if i==index)==attacker:
                return True
            command(h,f'call Simulator::cCity::ConvertCitizens thiscall {city:#x} {attacker:#x} 1000')
        wait_until(converted,'Native religious conversion did not complete',25)
        wait_until(lambda:next(pol for i,_,pol,_ in cities(g) if i==index)==attacker,
                   'Guest did not receive religious ownership',25)
        print('PASS religious takeover reaches both native cities',flush=True)
    else:
        context = native(h,'Simulator::GetActivePlanetRecord cdecl')
        command(h,f'call Simulator::cCity::OnAttacked thiscall {city:#x} 0 {attacker:#x}')
        def military():
            before = health(h,city,0x748)
            command(h,f'call Simulator::cCity::AddCaptureDamage cdecl {context:#x} {city:#x} '
                      f'{city+0x2F0:#x} {rival[2]:#x} f:100 {attacker:#x} 0')
            return health(h,city,0x748)-before
        def economic(target=city,buyer=attacker):
            before = health(h,target,0x758)
            command(h,f'call Simulator::cCity::AddBuyProgress thiscall {target:#x} f:10 {buyer:#x}')
            return health(h,target,0x758)-before
        command(h,'coop.fix civ.takeover_resistance off')
        raw_military,raw_trade = military(),economic()
        assert preach(h,city,attacker)==1
        command(h,'coop.fix civ.takeover_resistance on')
        scaled_military,scaled_trade = military(),economic()
        print('Military progress',raw_military,'->',scaled_military,flush=True)
        print('Economic progress',raw_trade,'->',scaled_trade,flush=True)
        assert raw_military>0 and abs(scaled_military-raw_military/1.15)<.02
        assert raw_trade>0 and abs(scaled_trade-raw_trade/1.15)<.02
        mirror = next(ptr for i,ptr,_,_ in cities(g) if i==rival[0])
        wait_until(lambda:abs(health(h,city,0x748)-health(g,mirror,0x748))<.15,
                   'Military progress differs on guest',15)
        # Buying progress on a teammate remains native: no difficulty penalty.
        command(h,'coop.fix civ.takeover_resistance off')
        friendly_raw = economic(own[1],attacker)
        command(h,'coop.fix civ.takeover_resistance on')
        assert abs(economic(own[1],attacker)-friendly_raw)<.02
        assert preach(h,city,attacker)==0
        print('PASS one-citizen tick retained as fractional progress',flush=True)
        if args.save_receipt:
            assert not args.save_receipt.exists()
            expected = dict(serial=u32(h,city+0x24),attacker=attacker,
                            followers=followers(h,city).get(attacker,0))
            location = Path(re.search(r'location=(.+?)\s+access=',command(h,'coop.savedb'))[1])
            planet = location/'Clyth.spo'
            stamp = planet.stat().st_mtime_ns
            print(command(h,'coop.save'),flush=True)
            wait_until(lambda:planet.stat().st_mtime_ns!=stamp,'Native save failed',30)
            command(h,'coop.civ')
            assert Path(str(planet)+'.duo-conversion').is_file()
            args.save_receipt.write_text(json.dumps(expected))
            print('PASS fractional conversion checkpoint written',expected,flush=True)
        else:
            assert preach(h,city,attacker)==1
            print('PASS repeated small preaching makes native progress',flush=True)
        if args.finish:
            def owner(peer):
                return next(pol for index,_,pol,_ in cities(peer) if index==rival[0])
            if args.finish=='military':
                def conquered():
                    if owner(h)==attacker: return True
                    military()
                wait_until(conquered,'Scaled military capture did not finish',30)
            else:
                def ready_to_buy():
                    if health(h,city,0x758)>=100: return True
                    economic()
                wait_until(ready_to_buy,'Scaled trade eligibility did not finish',30)
                def money(peer):
                    return float(re.search(r'shared money=([-\d.]+)',command(peer,'coop.res'))[1])
                before = money(h)
                assert before>=2500
                command(h,f'call Simulator::cCity::BuyByCiv thiscall {city:#x} {attacker:#x} f:2500')
                assert abs((before-money(h))-2500)<40, 'Native purchase debit changed'
                wait_until(lambda:abs(money(h)-money(g))<1,'Shared purchase debit differs',10)
                print('PASS native economic purchase retained its 2500 debit',flush=True)
            wait_until(lambda:owner(h)==owner(g)==attacker,'Takeover ownership differs on guest',30)
            print('PASS',args.finish,'takeover reaches both native cities',flush=True)
    assert all('total faults: 0' in command(peer,'exc') for peer in (h,g))
    print('PASS takeover resistance and zero native faults',flush=True)


if __name__ == '__main__':
    main()
