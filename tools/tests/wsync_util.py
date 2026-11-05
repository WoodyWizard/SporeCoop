"""Readers of the host-owned world state rows (mods/coop/wsync.cpp, `coop.wsync dump`): every value is read from
the game of the machine asked (the guest's own sky, relations, stages done, goal card), never from the mod's
bookkeeping, so a test compares what each player's game shows."""
import math
import re


def dump(con, row=None):
    """{row: [entry dict]} of this game's own values"""
    out = {}
    for line in con.call('coop.wsync dump' + (f' {row}' if row else ''))['out'].splitlines():
        parts = line.split(' ', 1)
        if len(parts) < 2 or parts[0] == 'end':
            continue
        name, rest = parts
        e = {'raw': rest}
        m = re.search(r'key=([0-9A-F]{8})', rest) or re.search(r'target=([0-9A-F]{8})', rest)
        if m:
            e['key'] = m.group(1)
        if name == 'tod':
            e['q'] = [float(x) for x in rest.split()[1:5]]
        elif name == 'day':
            e['v'] = float(rest.split()[1])
        elif name == 'stages':
            e['traits'] = re.search(r'traits=(\S+)', rest).group(1).split(',')
        elif name == 'tribe_rel':
            e['rel'] = float(re.search(r'rel=(-?[\d.]+|nan|-nan)', rest).group(1).replace('-nan', 'nan'))
            e['pos'] = tuple(float(x) for x in re.search(r'pos=\(([^)]*)\)', rest).group(1).split())
        elif name == 'tribe_food':
            e['food'] = float(re.search(r'food=(-?[\d.]+)', rest).group(1))
            e['pos'] = tuple(float(x) for x in re.search(r'pos=\(([^)]*)\)', rest).group(1).split())
        elif name == 'tribe_goal':
            e['pos'] = tuple(float(x) for x in re.search(r'pos=\(([^)]*)\)', rest).group(1).split())
            e['state'] = int(re.search(r'state=(\d+)', rest).group(1))
            p = re.search(r'progress=(-?[\d.]+)/(-?[\d.]+)', rest)
            e['progress'] = (float(p.group(1)), float(p.group(2)))
        out.setdefault(name, []).append(e)
    return out


def tod_angle(h, g):
    """the angle (rad) between the host's and the guest's sky (None: one has none)"""
    if not h.get('tod') or not g.get('tod'):
        return None
    a, b = h['tod'][0]['q'], g['tod'][0]['q']
    d = min(1.0, abs(sum(x * y for x, y in zip(a, b))))
    return 2 * math.acos(d)


def rel_pairs(h, g, row='tribe_rel'):
    """[(host entry, guest entry or None)] matched by political id, else position (40 m)"""
    out = []
    for e in h.get(row, []):
        m = [x for x in g.get(row, []) if x['key'] == e['key']]
        if not m:
            m = [x for x in g.get(row, []) if math.dist(x['pos'], e['pos']) < 40]
        out.append((e, m[0] if m else None))
    return out


def differences(h, g, rows=('tod', 'day', 'stages', 'tribe_rel', 'tribe_goal')):
    """[text] of every row whose guest value is not the host's (the class check after a transition)"""
    bad = []
    if 'tod' in rows and h.get('tod'):
        a = tod_angle(h, g)
        if a is None or a > 0.05:
            bad.append(f'tod: the guest\'s sky is {a if a is None else round(a, 3)} rad from the host\'s')
    if 'day' in rows and h.get('day'):
        if not g.get('day') or abs(g['day'][0]['v'] - h['day'][0]['v']) > 0.5:
            bad.append(f'day: host {h["day"][0]["v"]} guest {g.get("day") and g["day"][0]["v"]}')
    if 'stages' in rows and h.get('stages'):
        if not g.get('stages') or g['stages'][0]['traits'] != h['stages'][0]['traits']:
            bad.append(f'stages: host {h["stages"][0]["traits"]} guest {g.get("stages") and g["stages"][0]["traits"]}')
    if 'tribe_rel' in rows:
        for e, x in rel_pairs(h, g):
            if x is None:
                continue  # a village the guest lacks is W2's (objects), not a world-state row
            if not (abs(x['rel'] - e['rel']) < 0.1):
                bad.append(f'tribe_rel {e["key"]}: host {e["rel"]} guest {x["rel"]}')
    if 'tribe_food' in rows:  # BUG-0580 (not in the default rows: the class tests ask for it)
        for e, x in rel_pairs(h, g, 'tribe_food'):
            if x is not None and abs(x['food'] - e['food']) > 1.5:
                bad.append(f'tribe_food {e["key"]}: host {e["food"]} guest {x["food"]}')
    if 'tribe_goal' in rows and h.get('tribe_goal'):
        hg, gg = h['tribe_goal'][0], (g.get('tribe_goal') or [None])[0]
        if gg is None:
            bad.append('tribe_goal: the guest has none')
        else:
            if hg['key'] != 'FFFFFFFF' and (gg['key'] == 'FFFFFFFF' or math.dist(hg['pos'], gg['pos']) > 40):
                bad.append(f'tribe_goal: host target {hg["key"]} at {hg["pos"]}, guest {gg["key"]} at {gg["pos"]}')
            if abs(hg['progress'][0] - gg['progress'][0]) > 0.01 or abs(hg['progress'][1] - gg['progress'][1]) > 0.01:
                bad.append(f'tribe_goal progress: host {hg["progress"]} guest {gg["progress"]}')
    return bad
