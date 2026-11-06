"""Verify upfront founder reservations on a new disposable Cell planet or a cold reload."""
import argparse
from pathlib import Path
import subprocess
import sys
import time

from duo_orders import wait_until
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--reload-slot', help='Load the existing four-seat planet with deliberately different lobby defaults.')
    args = parser.parse_args()
    h = Console(args.host)
    wait_until(lambda:h.call('ping')['ok'], 'No lab console', 60)
    wait_until(lambda:'GalaxyGameEntry' in command(h,'game.mode'), 'No galaxy menu', 90)
    assert 'duoqa' in command(h,'coop.savedb').lower(), 'Disposable test profiles only'
    seats, percent = (2,50) if args.reload_slot else (4,100)
    command(h,f'coop.host {args.port} SoloChallenge {seats} {percent}')
    assert f'players=1/{seats}' in command(h,'coop.status')
    if args.reload_slot:
        command(h,'wait.frames 600 120000')
        command(h,f'coop.load {args.reload_slot} QuartetSolo')
    else:
        wizard = Path(__file__).with_name('new_stage_game.py')
        subprocess.run([sys.executable,str(wizard),'cell','-i',str(args.host),'--name','QuartetSolo','--no-settle'],check=True)
    wait_until(lambda:'Cell' in command(h,'game.mode') and 'QuartetSolo.spo' in command(h,'coop.avatar'),
               'Wrong test planet',90)
    status = 'Reserved founders: 4, difficulty scaling 100%'
    if not args.reload_slot:
        for _ in range(18):
            if status in command(h,'coop.status'): break
            h.call('coop.save')
            time.sleep(3)
    wait_until(lambda:status in command(h,'coop.status'), 'Campaign did not retain its original reservation',45)
    assert 'players=1/4' in command(h,'coop.status') and 'total faults: 0' in command(h,'exc')
    print('PASS one player, four reserved founders and the original difficulty setting.',flush=True)


if __name__ == '__main__':
    main()
