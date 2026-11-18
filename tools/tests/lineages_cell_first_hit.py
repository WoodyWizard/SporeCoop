"""First native Cell hit on a delayed link, without a preliminary setup bite.

Uses already joined disposable QA instances 4/5. Does not replay the stage.
Enemy protection flags are cleared only in these disposable profiles.
"""
import re
import time
from lineages_cell_report import Console, command, values, wait_until


def main():
    h, g = Console(4), Console(5)
    for c in (h, g):
        assert 'duoqareportsession' in command(c, 'coop.savedb').lower()
        command(c, 'coop.pool invulnfor 120')
    command(g, 'coop.pool tp 3 0')
    time.sleep(2)
    candidates = re.findall(r'^see (c\d+) creature .*$', command(h, 'coop.pool see 60'), re.M)
    assert candidates, 'No nearby native creature'
    for key in candidates:
        for c in (h, g):
            command(c, f'coop.pool swimto {key} tp')
        time.sleep(2)
        state = g.call(f'coop.pool cell {key}')['out']
        if 'no such' not in state and values(g, f'coop.pool cell {key}').get('gfx', 0):
            break
    else:
        raise AssertionError('No drawn target')
    print('Target before', command(h, f'coop.pool cell {key}'),
          command(g, f'coop.pool cell {key}'), flush=True)
    command(h, f'coop.pool npcflag {key} spare 0')
    command(h, f'coop.pool npcflag {key} invuln 0')
    before = values(h, f'coop.pool cell {key}')['hp']
    start = time.monotonic()
    hit = command(g, f'coop.pool kill {key} hit')
    print('First bite', hit, flush=True)
    assert float(re.search(r'ring=(-?[\d.]+)', hit).group(1)) >= 0
    wait_until(lambda: values(h, f'coop.pool cell {key}').get('hp', 999) < before, 5)
    after = values(h, f'coop.pool cell {key}')['hp']
    wait_until(lambda: values(g, f'coop.pool cell {key}').get('hp', 999) == after, 5)
    print('PASS first hit authoritative HP', before, '->', after,
          'guest caught up in', round(time.monotonic()-start, 3), flush=True)
    print(command(g, f'coop.pool cell {key}'), flush=True)
    for c in (h, g):
        assert 'total faults: 0' in command(c, 'exc')


if __name__ == '__main__':
    main()
