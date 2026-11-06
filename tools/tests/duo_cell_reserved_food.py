"""Quarter-share Cell rewards on the disposable four-founder planet, including a cold save."""
import argparse
import pathlib
import re
import time

from duo_orders import wait_until
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=int,required=True)
    parser.add_argument('--port',type=int,required=True)
    parser.add_argument('--after-restart',action='store_true')
    parser.add_argument('--guest',type=int,help='Verify a second player receives the same quarter-share reward.')
    args = parser.parse_args()
    peer = Console(args.host)
    wait_until(lambda:peer.call('ping')['ok'], 'No console',60)
    wait_until(lambda:'GalaxyGameEntry' in command(peer,'game.mode'),'No menu',90)
    assert 'duoqa' in command(peer,'coop.savedb').lower()
    command(peer,'wait.frames 600 120000')
    command(peer,'coop.load Game0 QuartetSolo')
    wait_until(lambda:'Cell' in command(peer,'game.mode'),'No Cell stage',75)
    command(peer,f'coop.host {args.port} SoloChallenge')
    assert 'Reserved founders: 4, difficulty scaling 100%' in command(peer,'coop.status')
    def food():
        result = re.search(r'^cell\(host\): food=(-?\d+)',command(peer,'coop.pool'),re.M)
        return int(result[1]) if result else None
    wait_until(lambda:food() is not None,'No native food total',45)
    def meal(amount,plant):
        command(peer,f'call Simulator::Cell::AddFoodPoints cdecl {amount} {int(plant)}')
    before = food()
    if args.after_restart:
        meal(1,True)
        assert food() == before+1, 'Three saved quarter points were lost or duplicated'
        meal(3,False)
        assert food() == before+1, 'Plant credit leaked into the meat pool'
        meal(1,False)
        assert food() == before+2, 'Four raw meat points did not produce one food point'
        print('PASS restored three fractional plant points and kept meat credit separate.',flush=True)
    else:
        meal(4,False)
        assert food() == before+1, 'Four raw meat points did not produce one native food point'
        meal(3,True)
        assert food() == before+1, 'Three fractional plant points were rounded up early'
        location = re.search(r'location=(.+?)\s+access=',command(peer,'coop.savedb'))[1]
        sidecar = pathlib.Path(location) / 'quartetsolo.spo.duo-food'
        assert not sidecar.exists(), 'Use an untouched qualification fixture'
        for attempt in range(12):
            if sidecar.exists(): break
            print(peer.call('coop.save')['out'].strip(),flush=True)
            time.sleep(2.5)
        assert sidecar.exists(), 'Fractional credit was not attached to the completed native save'
        data = sidecar.read_bytes()
        assert data[:4] == b'CFD2' and len(data) == 90 and int.from_bytes(data[12:16],'little') == 300
        print('PASS four raw food points award one; three remaining quarter points saved.',flush=True)
    if args.guest:
        g = Console(args.guest)
        assert 'duoqa' in command(g,'coop.savedb').lower()
        assert 'GalaxyGameEntry' in command(g,'game.mode')
        command(g,f'coop.join 127.0.0.1:{args.port} GuestForager')
        def guest_food():
            out = re.search(r'^cell\(guest\): food=(-?\d+)',command(g,'coop.pool'),re.M)
            return int(out[1]) if out else None
        wait_until(lambda:'Cell' in command(g,'game.mode') and guest_food() == food(),
                   'Guest did not join the same food pool',100)
        prior = food()
        command(g,'call Simulator::Cell::AddFoodPoints cdecl 8 1')
        wait_until(lambda:food() == prior+2 and guest_food() == prior+2,
                   'Guest meal was scaled differently or paid twice',15)
        assert 'total faults: 0' in command(g,'exc')
        print('PASS eight raw guest food points paid two into both native totals.',flush=True)
        def live_plant():
            return re.search(r'see (c\d+) plant ',command(g,'coop.pool see 180'))
        plant = wait_until(live_plant,'No native plant in the guest pool',30)
        prior = food()
        result = command(g,'coop.pool eat ' + plant[1])
        raw = int(re.search(r'points=(\d+)',result)[1])
        assert raw > 0
        wait_until(lambda:food() == prior + raw // 4 and guest_food() == food(),
                   'Consuming a native plant did not produce the scaled reward on both peers',20)
        print('PASS native plant consumption:',raw,'raw points paid',raw // 4,'to both games.',flush=True)
    assert 'total faults: 0' in command(peer,'exc')


if __name__ == '__main__':
    main()
