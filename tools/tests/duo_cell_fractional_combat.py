"""Qualify separate whole-point combat carries on an isolated two-seat native Cell planet."""
import argparse
from pathlib import Path
import re
import time

from duo_orders import wait_until
from duo_villages import Console, command


def meter(peer):
    return {k:int(n) for k,n in re.findall(r'(\w+)=(\d+)',command(peer,'coop.pool combat_credit'))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=int,required=True)
    parser.add_argument('--guest',type=int)
    parser.add_argument('--restore',action='store_true')
    parser.add_argument('--groups',type=int,choices=(2,4),help='Verify extra native enemies and stop.')
    args = parser.parse_args()
    h = Console(args.host)
    wait_until(lambda:h.call('ping')['ok'],'No console',60)
    wait_until(lambda:'GalaxyGameEntry' in command(h,'game.mode'),'No galaxy menu',90)
    assert 'duoqa' in command(h,'coop.savedb').lower()
    command(h,'wait.frames 600 120000')
    command(h,'coop.host 47810 RoundHost 2 100')
    command(h,'coop.load Game0 QuartetSolo')
    seats = args.groups or 2
    wait_until(lambda:f'Reserved founders: {seats},' in command(h,'coop.status') and
               'Cell:' in h.call('coop.avatar')['out'],'Wrong Cell fixture',90)
    location = re.search(r'location=(.+?)\s+access=',command(h,'coop.savedb'))[1]
    save = Path(location)/'quartetsolo.spo.duo-food'
    if args.restore:
        saved = save.read_bytes()
        m = meter(h)
        assert saved[:4]==b'CFD2' and len(saved)==90 and saved[24] and saved[25]
        assert m['damageCarry']==(saved[25]+m['baseHit']*5)%100, (m,saved[25])
        assert m['hpCarry']==(saved[24]+m['baseHP']*15)%100, (m,saved[24])
        print('PASS resumed both native carries after process restart',m,flush=True)
        assert 'total faults: 0' in command(h,'exc')
        if not args.groups: return
    if args.groups:
        for _ in range(45):
            line = command(h,'coop.pool enemy_groups')
            m = re.search(r'groups=(\d+) added=(\d+) limited=(\d+) latest=(\d+),(\d+),(\d+)',line)
            assert m,line
            count, added, limited, *ids = map(int,m.groups())
            ids = [idx for idx in ids if idx]
            assert count*(seats-1) == added + limited
            if len(set(ids)) == seats-1 and added > 0:
                print('PASS native extra predator IDs',ids,'with',seats,'reserved seats.',line,flush=True)
                break
            command(h,'coop.pool goto 100 30')
            time.sleep(1)
        else:
            raise RuntimeError('No full extra group observed')
        assert 'total faults: 0' in command(h,'exc')
        return
    actors = [h]
    if args.guest:
        g = Console(args.guest)
        assert 'duoqa' in command(g,'coop.savedb').lower()
        command(g,'coop.join 127.0.0.1:47810 RoundGuest')
        wait_until(lambda:'Cell' in command(g,'game.mode') and 'Cell:' in g.call('coop.avatar')['out'] and
                   'Host campaign rules: 2' in command(g,'coop.status'),'Guest missing',90)
        actors.append(g)
    start = meter(h)
    print('Initial credits',start,flush=True)
    for peer in actors:
        baseline = meter(h)
        remote_start = meter(peer)['guestLoss']
        strikes = 0
        for attempt in range(70):
            # Keep the disposable avatar alive through repeated real DamageCell calls.
            command(peer,'coop.pool hp 100')
            result = command(peer,'coop.pool enemyhit')
            raw = int(re.search(r'raw=(\d+)',result)[1])
            if raw:
                strikes += 1
            else:
                command(peer,'coop.pool goto 65 5')
                time.sleep(.6)
            if (meter(h)['extraHit'] > baseline['extraHit'] and strikes and
                    (peer is h or meter(peer)['guestLoss'] > remote_start)):
                break
        current = meter(h)
        assert strikes and current['extraHit'] > baseline['extraHit'], ('No extra native damage',current)
        assert (current['extraHit']-baseline['extraHit'])*100+current['damageCarry']-baseline['damageCarry'] == (current['baseHit']-baseline['baseHit'])*5
        if peer is not h:
            assert meter(peer)['guestLoss'] > remote_start
        print('PASS native hit carry',current,flush=True)
    for _ in range(40):
        if meter(h)['extraHP']>start['extraHP']: break
        command(h,'coop.pool goto 150 40')
        time.sleep(1)
    current=meter(h)
    assert current['extraHP']>start['extraHP'] and current['assigned']>start['assigned'],current
    assert (current['extraHP']-start['extraHP'])*100+current['hpCarry']-start['hpCarry']==(current['baseHP']-start['baseHP'])*15
    print('PASS native spawned HP awards',current,flush=True)
    if args.guest:
        g=actors[-1]
        command(g,'coop.leave')
        wait_until(lambda:'players=1/2' in command(h,'coop.status'),'Departure not acknowledged',15)
        prior=meter(h)
        command(g,'coop.join 127.0.0.1:47810 RoundGuest')
        wait_until(lambda:'players=2/2' in command(h,'coop.status') and
                   'Host campaign rules: 2' in command(g,'coop.status'),'Rejoin failed',45)
        now=meter(h)
        for value,extra,carry,rate in (('baseHP','extraHP','hpCarry',15),('baseHit','extraHit','damageCarry',5)):
            assert 100*(now[extra]-prior[extra])+now[carry]-prior[carry]==(now[value]-prior[value])*rate
        print('PASS rejoining kept the host combat ledgers.',flush=True)
    for _ in range(40):
        before=meter(h)
        if not before['damageCarry']:
            command(h,'coop.pool hp 100')
            command(h,'coop.pool enemyhit')
        if not before['hpCarry']:
            command(h,'coop.pool goto 130 30')
            time.sleep(.7)
        command(h,'coop.pool hp 6')
        result=h.call('coop.save')
        if result['ok'] and save.exists():
            saved=save.read_bytes()
            if len(saved)==90 and saved[:4]==b'CFD2' and saved[24] and saved[25]:
                break
        time.sleep(.5)
    else:
        raise RuntimeError('Could not store two nonzero fractions')
    print('PASS stored fractions',saved[24],saved[25],flush=True)
    assert all('total faults: 0' in command(p,'exc') for p in actors)


if __name__ == '__main__':
    main()
