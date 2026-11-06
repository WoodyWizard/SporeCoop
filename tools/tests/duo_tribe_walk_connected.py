"""Measure native walking on an already joined disposable duo campaign.

Uses explicit selection/walk orders, not mouse-selection qualification. Reuses
the upstream per-frame movement limits. Never launches games or replaces saves.
"""
import argparse
import json
import math
from pathlib import Path
import re
import time

from duo_villages import Console, command
from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_native_ui import require_gameplay_ui, view_tribal_point
import coop_tribe_walk as walk
from coop_multi import measure


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--planet', required=True)
    p.add_argument('--owner', choices=('host', 'guest'), required=True)
    p.add_argument('--species', required=True)
    p.add_argument('--seconds', type=int, default=45)
    p.add_argument('--fps', type=int, choices=(30, 60), default=30)
    p.add_argument('--idle-fps', type=int, choices=range(5, 31), default=10,
                   help='Return to this low background frame rate after recording.')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    assert a.seconds >= 20
    assert not a.output.exists(), 'Preserve earlier evidence; use a new output path'
    h, g = Console(a.host), Console(a.guest)
    for c in (h, g):
        assert f"world '{a.planet}'" in command(c, 'coop.avatar')
        assert 'Tribe' in command(c, 'game.mode')
        require_gameplay_ui(c)
        command(c, 'coop.uistate 0')
    owner = 0 if a.owner == 'host' else int(re.search(r'client id=(\d+)', command(g, 'coop.status'))[1])
    selected = [i for i, r in citizens(h).items() if r['species'] == a.species.upper() and not r['dead']]
    assert selected, 'No living members of the requested founder'
    center = citizens(h)[selected[0]]['pos']
    radius = math.sqrt(sum(x*x for x in center))
    # A short tangent walk, projected onto the existing planet radius. Native
    # locomotion handles the terrain and collisions; no actor is teleported.
    tangent = (-center[1], center[0], 0)
    length = math.sqrt(sum(x*x for x in tangent))
    assert length > 1
    targets = []
    for direction in (-1, 1):
        point = [x + direction*22*t/length for x, t in zip(center, tangent)]
        norm = math.sqrt(sum(x*x for x in point))
        targets.append([x*radius/norm for x in point])
    for c in (h, g):
        view_tribal_point(c, center)
    # Native play continues while the cameras pan. Reacquire membership after
    # that setup and wait for the joining client's current snapshot.
    selected = [i for i, r in citizens(h).items() if r['species'] == a.species.upper() and not r['dead']]
    assert selected, 'Founder has no living members after camera setup'
    def membership_current():
        rows = citizens(g)
        return all((r := rows.get(i)) and not r['dead'] and r['species'] == a.species.upper() for i in selected)
    wait_until(membership_current, 'Current founder membership has not reached the guest', 20)
    driver = h if a.owner == 'host' else g
    for identity in citizens(driver):
        command(driver, f'coop.tribe select {identity} 0')
    for identity in selected:
        result = command(driver, f'coop.tribe select {identity} 1')
        assert 'selected=1' in result, f'Member {identity} selection failed: {result}'
    samples, orders = [], []
    try:
        for c in (h, g):
            command(c, f'frame.limit {round(1000/a.fps)}')
            command(c, 'coop.track start')
        start, next_order = time.monotonic(), 0
        while time.monotonic()-start < a.seconds:
            elapsed = time.monotonic()-start
            if elapsed >= next_order:
                point = targets[len(orders) % 2]
                result = command(driver, 'coop.tribe walk '+' '.join(f'{x:.3f}' for x in point))
                orders.append(dict(time=elapsed, target=point, result=result))
                next_order = elapsed+6
            errors, coverage, total, worst = measure(h, g)
            samples.append(dict(time=elapsed, errors=errors, coverage=coverage, total=total, worst=worst))
            time.sleep(.4)
    finally:
        for c in (h, g):
            command(c, 'coop.track stop')
            command(c, f'frame.limit {round(1000/a.idle_fps)}')
    rows = {'host': walk.dump(h), 'guest': walk.dump(g)}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(dict(args=vars(a) | {'output': str(a.output)}, orders=orders, samples=samples, rows=rows)), encoding='utf-8')
    walk.W, walk.H = map(int, re.search(r'screen (\d+) (\d+)', command(g, 'coop.uistate')).groups())
    walk.FPS_K = a.fps/60
    top = walk.pct([r['rv'] for r in rows['host'] if r['who'] == owner and r['rv'] > .3], .95) or 5
    failures = []
    def check(ok, label):
        print(('PASS ' if ok else 'FAIL ')+label, flush=True)
        if not ok: failures.append(label)
    for side in ('host', 'guest'):
        report = walk.analyse(rows[side], owner, top, f'{side} sees {a.owner}')
        walk.report(report, check, mirrors=side == 'guest')
    for c in (h, g):
        check('total faults: 0' in command(c, 'exc'), 'zero native faults')
    for kind in sorted({k for s in samples for k in s['errors']}):
        values = [v for s in samples for v in s['errors'].get(kind, [])]
        print(f'Sync {kind}: samples={len(values)} p90={walk.pct(values,.9):.3f} max={max(values):.3f}', flush=True)
    print('Coverage minimum:', min(s['coverage'] for s in samples), 'raw evidence:', a.output, flush=True)
    assert not failures, '; '.join(failures)


if __name__ == '__main__':
    main()
