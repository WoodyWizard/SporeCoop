"""Targeted Cell editor regression on already joined disposable 800x600 QA games.

Requires DuoQAReportSession profiles (instances 4 and 5). Uses real mating,
passive editor notices and editor buttons. No campaign replay. An optional loopback
latency/loss relay can stay between the games throughout this check.
"""
import re
import sys
import time
from lineages_cell_report import Console, command, values, wait_until


def click(c, x, y):
    command(c, f'click {x} {y}')
    command(c, 'wait.frames 12 3000')


def main():
    h, g = Console(4), Console(5)
    for c in (h, g):
        assert 'duoqareportsession' in command(c, 'coop.savedb').lower()
        assert 'Cell:' in command(c, 'coop.avatar')
        command(c, 'coop.pool invulnfor 600')
    def shares():
        return int(re.search(r'for shares (\d+)', command(h, 'coop.status')).group(1))
    try:
        cases = ((True, True), (True, False)) if '--fixed-only' in sys.argv else ((False, False), (True, True), (True, False))
        for enabled, save in cases:
            for c in (h, g):
                command(c, 'coop.pool invulnfor 600')
            command(h, 'coop.fix world.cell_editor_in_place ' + ('on' if enabled else 'off'))
            wait_until(lambda: 'cine=0' in command(h, 'coop.pool'), 30)
            before = shares()
            dna = values(h, 'coop.pool dna')
            command(h, 'coop.pool mate button')
            command(h, 'wait.frames 12 3000')
            click(h, 400, 538)
            wait_until(lambda: 'alive=1' in command(h, 'coop.pool mate'), 10)
            command(h, 'coop.pool mate tp')
            wait_until(lambda: 'Editor' in command(h, 'game.mode'), 20)
            wait_until(lambda: 'is editing their cell' in command(g, 'coop.uistate'), 10)
            assert 'vote none:' in command(g, 'coop.vote')
            assert 'Cell:' in command(g, 'coop.avatar')
            command(h, 'wait.frames 40 5000')
            if save:
                name = 'QA' + str(int(time.time()))
                click(h, 400, 576)
                click(h, 400, 332)
                # Clear the bounded native field without a modifier key: key
                # state is consumed asynchronously, so immediate Ctrl+A/up can
                # insert 'a' instead of selecting the field.
                command(h, 'key home')
                command(h, 'wait.frames 4 1000')
                for _ in range(64):
                    command(h, 'key del')
                command(h, 'wait.frames 4 1000')
                command(h, 'text ' + name)
                command(h, 'wait.frames 12 3000')
                click(h, 778, 582)  # close the name panel
                click(h, 778, 582)  # accept the editor
            else:
                click(h, 744, 582)
            wait_until(lambda: 'Cell' in command(h, 'game.mode'), 25)
            command(h, 'coop.pool invulnfor 600')
            if not enabled:
                wait_until(lambda: shares() > before, 25)
                print('EXPECTED FAIL old editor return starts a full-world share', flush=True)
                wait_until(lambda: 'receiving world:' in command(g, 'coop.status') or
                           'Loading' in command(g, 'game.mode'), 15)
                wait_until(lambda: 'Cell:' in g.call('coop.avatar')['out'] and
                           'receiving world:' not in command(g, 'coop.status') and
                           'load queued' not in command(g, 'coop.status'), 45)
            else:
                if save:
                    match = wait_until(lambda: re.search(
                        r"player 0 cell: ([0-9A-F]{8})/\S+ '[^'\n]*" + name + "[^'\n]*'",
                        command(g, 'coop.creations')), 30)
                    wait_until(lambda: 'species=' + match.group(1) in command(g, 'coop.status'), 30)
                for _ in range(20):
                    time.sleep(.5)
                    assert shares() == before
                    assert 'Cell' in command(g, 'game.mode')
                assert values(g, 'coop.pool').get('mirrors', 0) > 5
                if not save:
                    assert values(h, 'coop.pool dna')['spent'] == dna['spent']
                print('PASS editor return without reload, saved design=' + str(save), flush=True)
            for c in (h, g):
                assert 'total faults: 0' in command(c, 'exc')
    finally:
        command(h, 'coop.fix world.cell_editor_in_place on')


if __name__ == '__main__':
    main()
