#!/usr/bin/env python3
"""Divergence detector: the game's own world on two machines, compared (docs/sync-architecture-plan.md L0).

`coop.census all <r>` (mods/coop/census.h) lists every placed game object of a game near its own player
(creatures with their net ids, Cell cells, and any other cGameData with a position: nests, eggs, fruit,
pickups, tools, buildings, vehicles, ...) and the count of every noun type. This module compares the
host's dump with a guest's, around the guest's own player (what the guest sees), and returns findings
with a stable signature, so many runs can be counted together (tools/detector_report.py).

  dump(con, r)                  -> World of one machine
  diff(host, guest)             -> [Finding] (empty: the two games agree in the area)
  SPORE_LANE=N python3 tools/tests/worlddiff.py [r]   diff of the lane's host and guest now

`coop.wstate` (mods/coop/wstate.cpp) adds each game's world state (time of day, stages done, DNA, food,
relations per species / tribe / civilization / empire, the current goal, member counts) and the planet's
whole lists of villages, huts, tools, nests, cities and buildings: `diff_state` compares them with
stage-specific tolerances (`<stage>:state:<kind>`, `All:state:<kind>`, `<stage>:planet:<kind>`,
`<stage>:planet_owner:<kind>`; pair_dump(..., state=True)).

Findings are data for triage, not a gate (yet): a kind on one machine only, a count far apart, a field
that differs on matched objects (alive, scale, hp, model). Matching: by net id where the mod has one
(creature npc/proxy, Cell cell index), else by noun and the nearest position.
"""
import collections
import math
import os
import re
import sys
import time

NOUNS = {  # GameNounIDs of the Spore ModAPI (Simulator/SimulatorEnums.h); unknown ones print as hex
    0x061494BE: 'animal_trap',
    0x018C9380: 'artillery_projectile',
    0x018EA1EB: 'building_city_hall',
    0x01A56ABA: 'building_entertainment',
    0x018EB106: 'building_house',
    0x018EA2CC: 'building_industry',
    0x070703B3: 'building_scenario',
    0x038CFB6B: 'celestial_body',
    0x02A37E35: 'cheat_object',
    0x018C43E8: 'city',
    0x0244FB08: 'city_territory',
    0x018C7C97: 'city_walls',
    0x018C816A: 'civilization',
    0x0403DF5C: 'node',
    0x018EB45E: 'creature',
    0x018EB4B7: 'citizen',
    0x1406A572: 'species_goal',
    0x34066DBA: 'creature_tutorial_mission',
    0x03DF2CC5: 'cultural_convert_city_order',
    0x04F76F0D: 'cultural_projectile',
    0x03D5C325: 'cultural_target',
    0x024270C7: 'deep_space_projectile',
    0x04167186: 'default_ao_e_area',
    0x024630D7: 'default_beam_projectile',
    0x024270C5: 'default_tool_projectile',
    0x02A034CD: 'egg',
    0x018EB9D2: 'empire',
    0x14D9597F: 'find_mission',
    0x0240E3BF: 'flak_projectile',
    0x02C9CC91: 'fruit',
    0x02E96892: 'fruit_group',
    0x018C431C: 'game_bundle',
    0x01906183: 'game_bundle_ground_container',
    0x017F243B: 'game_data',
    0x018EBADC: 'ufo',
    0x018C84A9: 'game_plant',
    0x018C40BC: 'game_terrain_cursor',
    0x036BE27E: 'gameplay_marker',
    0x01BE418E: 'herd',
    0x02E72CAE: 'hit_sphere',
    0x049CEC61: 'icbm',
    0x02B8A4E7: 'inter_city_road',
    0x03A2511E: 'interactive_ornament',
    0x02E9AE6C: 'military_attack_city_order',
    0x0283DDB1: 'movable_destructible_ornament',
    0x06EF0F11: 'nano_drone',
    0x52AA6122: 'nest',
    0x03ED8573: 'obstacle',
    0x018C88E4: 'ornament',
    0x07B38BA7: 'placeable_effect',
    0x074E0069: 'placeable_sound',
    0x03275728: 'planet',
    0x02DD8C42: 'planetary_artifact',
    0x02C21781: 'player',
    0x02265FDC: 'player_inventory',
    0x05776A2C: 'resource_projectile',
    0x02A8FB3F: 'rock',
    0x03572E72: 'sim_planet_low_l_o_d',
    0x032F9778: 'solar_hit_sphere',
    0x038CF94C: 'solar_system',
    0x018EB641: 'sound_loop_object',
    0x0244D3C8: 'space_defense_missile',
    0x021FFA3F: 'space_inventory',
    0x024270C9: 'spear',
    0x0355C93A: 'star',
    0x04E3FAB5: 'tool_object',
    0x055CF865: 'totem_pole',
    0x018C6D19: 'tribe',
    0x0629BAFE: 'tribe_food_mat',
    0x01E4DAAE: 'tribe_hut',
    0x3098AF98: 'tribe_planner',
    0x5436411A: 'tribe_to_tribe_mission',
    0x018C8F0C: 'tribe_tool',
    0x0436F342: 'turret',
    0x018C6DE8: 'vehicle',
    0x02E98AB7: 'vehicle_group_order',
    0x044462A6: 'visible_planet',
}

OBJ = re.compile(r'obj ([a-z])(-?\d+) key=([0-9A-F]{8}) alive=(-?\d+) hp=(\S+) ai=(-?\d+) own=(\S*) owner=(-?\d+) '
                 r'pos=\(([-\d.e+na]+) ([-\d.e+na]+) ([-\d.e+na]+)\)(?: noun=([0-9A-F]{8}) ser=(\d+) scale=(\S+))?')
HEAD = re.compile(r'census v1 role=(\S+) me=(\d+) mode=(.*?) modeid=([0-9A-F]+) world=([0-9A-F]+) t=([\d.]+)')
ME = re.compile(r'me pos=\(([-\d.e+na]+) ([-\d.e+na]+) ([-\d.e+na]+)\) ok=(\d)')

# compare area around the guest's own player (m) and the position tolerance of a match, per stage
AREA = {'Cell': 40.0, 'Creature': 70.0, 'Tribe': 90.0, 'Civilization': 160.0, 'Space': 120.0}
TOL_STATIC, TOL_MOVING = 3.0, 15.0
# BUG-0480: Cell: the host dumped before and after the guest, CELL_GAP s apart (SPORE_DETECTOR_BRACKET=0: one host dump)
CELL_BRACKET = os.environ.get('SPORE_DETECTOR_BRACKET', '1') != '0'
CELL_GAP = 1.5
# BUG-0596: a guest's Creature state is read twice, CELL_GAP s apart (SPORE_DETECTOR_BRACKET=0: once)
STATE_BRACKET = CELL_BRACKET
# objects that belong to one game by design (per-player logic, invisible helpers): never compared
IGNORE = {'hit_sphere', 'solar_hit_sphere', 'game_terrain_cursor', 'species_goal', 'creature_tutorial_mission',
          'tribe_to_tribe_mission', 'find_mission', 'military_attack_city_order', 'cultural_convert_city_order',
          'vehicle_group_order', 'sound_loop_object', 'placeable_sound', 'player', 'player_inventory',
          'space_inventory', 'cheat_object',
          # level of detail by each game's own camera (the planet a camera is close to is loaded): not a desync
          'visible_planet', 'sim_planet_low_l_o_d',
          # BUG-0473 / BUG-0498 (coop_world_lod.py, lab 20261103-161705): each game makes these around its own
          # player's camera (28 rocks on the walking guest only, 30-75 m ahead of it, never on the host that
          # stood 1 m from it at the start; 1-2 plants 8-14 m from the own player only): not a desync
          'rock', 'game_plant'}
MOVING = {'creature', 'citizen', 'herd', 'vehicle', 'ufo', 'tribe_tool'}


def noun_name(n):
    return NOUNS.get(n, f'{n:08X}')


def _f(x):
    try:
        return float(x)
    except ValueError:
        return float('nan')


class World:
    def __init__(self, text, who=''):
        self.who, self.role, self.mode, self.modeid, self.world, self.t = who, None, None, None, None, 0.0
        self.me, self.rows, self.nouns, self.complete = None, [], {}, False
        self.vts = {}  # noun -> Ghidra address of a vtable of its class
        for line in text.splitlines():
            line = line.strip()
            m = HEAD.match(line)
            if m:
                self.role, self.mode, self.modeid, self.world, self.t = (m.group(1), m.group(3), m.group(4),
                                                                         m.group(5), float(m.group(6)))
                continue
            m = OBJ.match(line)
            if m:
                self.rows.append(dict(kind=m.group(1), id=int(m.group(2)), key=m.group(3), alive=int(m.group(4)),
                                      hp=_f(m.group(5)), ai=int(m.group(6)), own=m.group(7), owner=int(m.group(8)),
                                      pos=tuple(_f(m.group(i)) for i in (9, 10, 11)),
                                      noun=int(m.group(12), 16) if m.group(12) else 0,
                                      ser=int(m.group(13)) if m.group(13) else 0,
                                      scale=_f(m.group(14)) if m.group(14) else float('nan')))
                continue
            m = ME.match(line)
            if m:
                if m.group(4) == '1':
                    self.me = tuple(_f(m.group(i)) for i in (1, 2, 3))
                continue
            if line.startswith('nouns'):
                for kv in line.split()[1:]:
                    k, _, v = kv.partition('=')
                    v, _, vt = v.partition('@')
                    self.nouns[int(k, 16)] = self.nouns.get(int(k, 16), 0) + int(v)
                    if vt:
                        self.vts[int(k, 16)] = int(vt, 16)
                continue
            if line.startswith('end objects='):
                self.complete = int(line.split('=')[1]) == len(self.rows)


def dump(con, r=250, at=None):
    cmd = f'coop.census all {r}' + (f' {at[0]:.1f} {at[1]:.1f} {at[2]:.1f}' if at else '')
    return World(con.call(cmd)['out'])


def pair_dump(host, guest, r=250, state=False):
    """(host World, guest World) over the same area: around the guest's player, or around the host's
    when the guest has no position (an RTS pointer at (0 0 0)). Each game lists only the objects near
    the centre it is given, so both dumps must use the same centre (a player far from the other one
    showed the other's objects as missing)."""
    hw, gw = _pair_dump(host, guest, r)
    if state:  # the world state and the planet's lists (docs/06-testing.md#testing-detector-state)
        hw.state, gw.state = state_dump(host), state_dump(guest)
        if gw.state is not None and gw.state.mode == 'Creature' and STATE_BRACKET:
            # BUG-0596: the guest's species table again CELL_GAP s after the host's dump: the host sends its table
            # within ~1 s of a change and a guest's own new record is undone within 0.5 s (social.cpp); a record in
            # one game only in both guest dumps is a difference, one in the first only was on its way
            time.sleep(CELL_GAP)
            gw.state.after = state_dump(guest)
    return hw, gw


def _pair_dump(host, guest, r):
    gw = dump(guest, r)
    if gw.me and not _origin(gw.me):
        if gw.mode == 'Cell' and CELL_BRACKET:
            # BUG-0480: Cell cells live for seconds (food eaten, prey killed, a level change replaces the
            # pool): one host dump next to one guest dump made a mirror on its 0.1-1 s way out look stale.
            # The host is dumped before and after the guest (CELL_GAP s apart): a guest's mirror counts as
            # stale only when the host had that cell in neither, a host cell as missing only when it
            # had it in both (docs/06-testing.md#testing-detector).
            hb = dump(host, r, gw.me)
            time.sleep(CELL_GAP)
            gw = dump(guest, r, gw.me) if gw.me else dump(guest, r)
            ha = dump(host, r, gw.me)
            ha.before = hb
            return ha, gw
        return dump(host, r, gw.me), gw
    hw = dump(host, r)
    if hw.me and not _origin(hw.me):
        gw = dump(guest, r, hw.me)
    return hw, gw


# ---- world state and whole-planet lists (`coop.wstate`, mods/coop/wstate.cpp; docs/06-testing.md#testing-detector-state)
WS_HEAD = re.compile(r'wstate v1 role=(\S+) mode=(.*?) modeid=([0-9A-F]+) world=([0-9A-F]+) t=([\d.]+)')
_KV = re.compile(r'(\w+)=(\([^)]*\)|\S+)')
# the planet's lists compared whole (presence, position, owner): noun -> (label, position tolerance m)
PLANET = {0x018C6D19: ('village', 25.0), 0x01E4DAAE: ('tribe_hut', 8.0), 0x018C8F0C: ('tribe_tool', 8.0),
          0x055CF865: ('totem_pole', 8.0), 0x0629BAFE: ('tribe_food_mat', 8.0), 0x52AA6122: ('nest', 8.0),
          0x018C43E8: ('city', 25.0), 0x018EA1EB: ('building_city_hall', 8.0), 0x018EB106: ('building_house', 8.0),
          0x018EA2CC: ('building_industry', 8.0), 0x01A56ABA: ('building_entertainment', 8.0), 0x0436F342: ('turret', 8.0)}
STAGE_NAMES = ('Cell', 'Creature', 'Tribe', 'Civilization', 'Space')
TOD_DEG = 20.0      # the host sends its sky once a second; a guest applies it within a frame or two
TRIBE_MATCH = 30.0  # a tribe's centre on the two games (the same save: the same place)


def _vec(v):
    v = v.strip('()').split()
    return tuple(_f(x) for x in v)


def _kv(line):
    return {k: (_vec(v) if v.startswith('(') else v) for k, v in _KV.findall(line)}


def _num(d, k, default=float('nan')):
    try:
        return float(d[k])
    except (KeyError, ValueError, TypeError):
        return default


class State:
    """one game's `coop.wstate` answer (None fields: not in this mode / not readable)"""

    def __init__(self, text, who=''):
        self.who, self.role, self.mode, self.modeid, self.world = who, None, None, None, None
        self.tod = self.day = self.player = self.cell = self.dna = self.rel_lv = self.goal = None
        self.res, self.species, self.tribes, self.civs, self.emps, self.planet = {}, {}, [], [], {}, []
        self.res_unsynced = set()
        self.after = None  # BUG-0596: the guest's state read again CELL_GAP s after the host's (pair_dump)
        self.stage_end = False  # BUG-0588 / BUG-0594: the host's game is in the end of its stage
        self.complete = False
        for line in text.splitlines():
            line = line.strip()
            m = WS_HEAD.match(line)
            if m:
                self.role, self.mode, self.modeid, self.world = m.group(1), m.group(2), m.group(3), m.group(4)
                continue
            if line.startswith('end wstate'):
                self.complete = True
                continue
            head, _, rest = line.partition(' ')
            d = _kv(rest)
            if head == 'st':
                what = rest.split(' ', 1)[0]
                if what == 'tod':
                    self.tod, self.day = d.get('q'), _num(d, 'day')
                elif what == 'player':
                    self.player = dict(traits=[int(x) for x in d.get('traits', '').split(',') if x.isdigit()],
                                       progress=_num(d, 'progress'), total=_num(d, 'total'))
                elif what == 'res':
                    # BUG-0595: a guest's total before the host's first one prints `money=100000(unsynced)`
                    # (shared::brief): the number is the game's own value, the suffix is the mod's sync state
                    self.res = {k: _num({k: str(v).split('(', 1)[0]}, k) for k, v in d.items()}
                    self.res_unsynced = {k for k, v in d.items() if str(v).endswith('(unsynced)')}
                elif what == 'cell':
                    self.cell = {k: _num(d, k) for k in ('food', 'plant', 'spent')}
                elif what == 'dna':
                    self.dna = _num(d, 'v')
                elif what == 'phase':
                    self.stage_end = d.get('stage_end') == '1'
                elif what == 'rel':
                    self.rel_lv = [_f(x) for x in d.get('lv', '').split(',')]
            elif head == 'species':
                key = rest.split(' ', 1)[0]
                self.species[key] = dict(relation=int(_num(d, 'relation', 0)), impressed=int(_num(d, 'impressed', 0)),
                                         killed=int(_num(d, 'killed', 0)))
            elif head == 'tribe':
                self.tribes.append(dict(i=int(_num(d, 'i', -1)), player=d.get('player') == '1', pol=d.get('pol'),
                                        pos=d.get('pos'), members=_num(d, 'members'), food=_num(d, 'food'),
                                        rel=_num(d, 'rel')))
            elif head == 'goal':
                self.goal = dict(tribe=int(_num(d, 'tribe', -1)), state=int(_num(d, 'state', -1)), pos=d.get('pos'))
            elif head == 'civ':
                self.civs.append(dict(pol=d.get('pol'), player=d.get('player') == '1', money=_num(d, 'money'),
                                      rel=_num(d, 'rel'), cities=_num(d, 'cities')))
            elif head == 'emp':
                self.emps[d.get('pol')] = dict(rel=_num(d, 'rel'), flags=int(_num(d, 'flags', 0)))
            elif head == 'pl':
                try:
                    noun = int(rest.split(' ', 1)[0], 16)
                except ValueError:
                    continue
                self.planet.append(dict(noun=noun, pos=d.get('pos'), pol=d.get('pol')))


def state_dump(con):
    """the game's world state (None when the dll has no coop.wstate: an older build)"""
    r = con.call('coop.wstate')
    out = r.get('out', '') if r.get('ok', True) else ''
    st = State(out)
    return st if st.mode is not None else None


def _quat_deg(a, b):
    if not a or not b or len(a) != 4 or len(b) != 4 or not all(map(math.isfinite, a + b)):
        return 0.0
    d = abs(sum(x * y for x, y in zip(a, b))) / max(1e-9, math.sqrt(sum(x * x for x in a) * sum(y * y for y in b)))
    return math.degrees(2 * math.acos(min(1.0, d)))


def _differs(a, b, absolute, rel):
    if not (math.isfinite(a) and math.isfinite(b)):
        return math.isfinite(a) != math.isfinite(b)
    return abs(a - b) > max(absolute, rel * max(abs(a), abs(b)))


def _level(v, lv):
    """the relationship level the UI shows (how many of the manager's thresholds the value reaches, ~)"""
    if not lv or not math.isfinite(v):
        return None
    return sum(1 for t in lv if math.isfinite(t) and v >= t)


def diff_state(hs, gs, stage):
    """[(what, kind, n, text)] of the world state of two games of one place (stage-specific tolerances)"""
    out = []

    def add(what, kind, n, text):
        out.append((what, kind, n, text))
    if hs is None or gs is None:
        return out
    if hs.tod and gs.tod:
        d = _quat_deg(hs.tod, gs.tod)
        if d > TOD_DEG:
            add('All:state', 'time_of_day', 1, f'the sky {d:.0f} deg apart (host q={_fmtq(hs.tod)}, guest q={_fmtq(gs.tod)})')
        if _differs(hs.day or 0, gs.day or 0, 1.0, 0.02):
            add('All:state', 'day_length', 1, f'a day lasts {hs.day:.0f} s on the host, {gs.day:.0f} s on the guest')
    elif bool(hs.tod) != bool(gs.tod) and stage != 'Cell':
        add('All:state', 'time_of_day', 1, f'time of day on the {"host" if hs.tod else "guest"} only')
    if hs.player and gs.player:
        ht, gt = hs.player['traits'], gs.player['traits']
        names = ('Cell', 'Creature', 'Tribe', 'Civ', 'Space')
        dd = [names[i] for i in range(min(len(ht), len(gt), 5)) if (ht[i] != 0) != (gt[i] != 0)]
        if dd:
            add('All:state', 'stage_done', len(dd), 'stages done differ: ' + ', '.join(
                f'{n} {"done" if ht[names.index(n)] else "not done"} on the host, '
                f'{"done" if gt[names.index(n)] else "not done"} on the guest' for n in dd))
        hp, gp = hs.player['progress'], gs.player['progress']
        if _differs(hp, gp, 5.0, 0.1) or _differs(hs.player['total'], gs.player['total'], 0.5, 0.01):
            add(f'{stage}:state', 'goal_progress', 1, f'the stage progress bar {hp:.0f}/{hs.player["total"]:.0f} on the host, '
                                                    f'{gp:.0f}/{gs.player["total"]:.0f} on the guest')
    for k in sorted(set(hs.res) & set(gs.res)):
        if k == 'food' and stage == 'Tribe':
            continue  # the player tribe's food: its tribe row below
        tol = (20.0, 0.05) if k == 'money' else (3.0, 0.1)
        if _differs(hs.res[k], gs.res[k], *tol):
            add(f'{stage}:state', k, 1, f'{k} {hs.res[k]:.0f} on the host, {gs.res[k]:.0f} on the guest')
    if stage == 'Cell' and hs.cell and gs.cell:
        for k, tol in (('food', (3.0, 0.1)), ('spent', (1.0, 0.0))):
            if _differs(hs.cell[k], gs.cell[k], *tol):
                add('Cell:state', 'dna' if k == 'food' else 'dna_spent', 1,
                    f'cell {k} {hs.cell[k]:.0f} on the host, {gs.cell[k]:.0f} on the guest (the DNA shown comes from them)')
    if stage == 'Creature':
        if hs.dna is not None and gs.dna is not None and _differs(hs.dna, gs.dna, 5.0, 0.1):
            add('Creature:state', 'dna', 1, f'DNA {hs.dna:.0f} on the host, {gs.dna:.0f} on the guest')
        common = set(hs.species) & set(gs.species)
        for kind in ('relation', 'impressed', 'killed'):
            bad = [k for k in sorted(common) if hs.species[k][kind] != gs.species[k][kind]]
            if bad:
                k = bad[0]
                add('Creature:state', f'species_{kind}', len(bad), f'{len(bad)} species with another {kind} (e.g. {k}: '
                    f'{hs.species[k][kind]} on the host, {gs.species[k][kind]} on the guest)')
        # a guest keys a received creation 0x7C / 0x7D......: such keys have no partner by design
        only = [k for k in set(hs.species) ^ set(gs.species) if k[18:20] not in ('7C', '7D') and k[:2] not in ('7C', '7D')
                and (hs.species.get(k) or gs.species.get(k))['relation'] != 0]
        ga = getattr(gs, 'after', None)
        if ga is not None:  # BUG-0596: in one game only also in the guest's second dump (CELL_GAP s after the host's)
            only = [k for k in only if (k in hs.species) != (k in ga.species and ga.species[k]['relation'] != 0)]
        if only:
            add('Creature:state', 'species_only', len(only), f'{len(only)} species with a relation in one game only '
                f'(e.g. {only[0]} {"host" if only[0] in hs.species else "guest"})')
    if stage == 'Tribe' and hs.tribes and gs.tribes:
        hp = next((t for t in hs.tribes if t['player']), None)
        gp = next((t for t in gs.tribes if t['player']), None)
        if hp and gp:
            if _differs(hp['food'], gp['food'], 3.0, 0.1):
                add('Tribe:state', 'food', 1, f'the player tribe\'s food {hp["food"]:.0f} on the host, {gp["food"]:.0f} on the guest')
            if _differs(hp['members'], gp['members'], 1.0, 0.0):
                add('Tribe:state', 'player_members', 1, f'the player tribe has {hp["members"]:.0f} members on the host, '
                                                        f'{gp["members"]:.0f} on the guest')
        hn = [t for t in hs.tribes if not t['player'] and t['pos']]
        gn = [t for t in gs.tribes if not t['player'] and t['pos']]
        pairs, _, _ = _match(hn, gn, TRIBE_MATCH)
        rel, mem, food = [], [], []
        for x, y in pairs:
            lx, ly = _level(x['rel'], hs.rel_lv), _level(y['rel'], gs.rel_lv)
            if (lx is not None and ly is not None and lx != ly) or _differs(x['rel'], y['rel'], 1.0, 0.0):
                rel.append((x, y))
            if _differs(x['members'], y['members'], 1.0, 0.0) or (x['members'] == 0) != (y['members'] == 0):
                mem.append((x, y))
            if _differs(x['food'], y['food'], 5.0, 0.2):
                food.append((x, y))
        for kind, lst, f in (('relation', rel, 'rel'), ('members', mem, 'members'), ('tribe_food', food, 'food')):
            if lst:
                x, y = lst[0]
                add('Tribe:state', kind, len(lst), f'{len(lst)} of {len(pairs)} tribes differ in {f} (e.g. the tribe at '
                    f'{_fmt(x["pos"])}: {x[f]:.2f} on the host, {y[f]:.2f} on the guest; pol {x["pol"]} / {y["pol"]})')
        hg, gg = hs.goal, gs.goal
        if hg and gg:
            ht = hg['tribe'] >= 0 and hg['pos']
            gt = gg['tribe'] >= 0 and gg['pos']
            # (the mission's +0x84 is no state: pointer-like values at runtime, lab 20261104-203017; not compared)
            if bool(ht) != bool(gt) or (ht and gt and math.dist(hg['pos'], gg['pos']) > TRIBE_MATCH):
                add('Tribe:state', 'goal', 1, f'the current goal: the tribe at {_fmt(hg["pos"]) if ht else "none"} '
                    f'on the host, at {_fmt(gg["pos"]) if gt else "none"} on the guest')
    if stage == 'Civilization' and hs.civs and gs.civs:
        hp = next((c for c in hs.civs if c['player']), None)
        gp = next((c for c in gs.civs if c['player']), None)
        if hp and gp and _differs(hp['cities'], gp['cities'], 0.0, 0.0):
            add('Civilization:state', 'player_cities', 1, f'the player owns {hp["cities"]:.0f} cities on the host, '
                                                         f'{gp["cities"]:.0f} on the guest')
        hc = {c['pol']: c for c in hs.civs if not c['player']}
        gc = {c['pol']: c for c in gs.civs if not c['player']}
        both = sorted(set(hc) & set(gc))
        for kind, f, tol in (('relation', 'rel', (1.0, 0.0)), ('civ_money', 'money', (50.0, 0.2)), ('cities', 'cities', (0.0, 0.0))):
            bad = [p for p in both if _differs(hc[p][f], gc[p][f], *tol)]
            if bad:
                p = bad[0]
                add('Civilization:state', kind, len(bad), f'{len(bad)} of {len(both)} civilizations differ in {f} (e.g. {p}: '
                    f'{hc[p][f]:.2f} on the host, {gc[p][f]:.2f} on the guest)')
        only = set(hc) ^ set(gc)
        if only:
            add('Civilization:state', 'civ_only', len(only), f'{len(only)} civilizations in one game only (by political id)')
    if stage == 'Space' and (hs.emps or gs.emps):
        both = sorted(set(hs.emps) & set(gs.emps))
        bad = [p for p in both if _differs(hs.emps[p]['rel'], gs.emps[p]['rel'], 0.5, 0.0)]
        if bad:
            p = bad[0]
            add('Space:state', 'relation', len(bad), f'{len(bad)} of {len(both)} empires differ in relation (e.g. {p}: '
                f'{hs.emps[p]["rel"]:.2f} on the host, {gs.emps[p]["rel"]:.2f} on the guest)')
        bad = [p for p in both if (hs.emps[p]['flags'] & 3) != (gs.emps[p]['flags'] & 3)]
        if bad:
            p = bad[0]
            add('Space:state', 'war_alliance', len(bad), f'{len(bad)} empires at war / allied on one game only (e.g. {p}: '
                f'flags {hs.emps[p]["flags"]} host, {gs.emps[p]["flags"]} guest)')
        only = set(hs.emps) ^ set(gs.emps)
        if only:
            add('Space:state', 'empire_only', len(only), f'{len(only)} empires with a relation record in one game only '
                f'(e.g. {sorted(only)[0]} {"host" if sorted(only)[0] in hs.emps else "guest"})')
    out += diff_planet(hs, gs, stage)
    return out


def _player_pol(st, stage):
    if stage == 'Tribe':
        return next((t['pol'] for t in st.tribes if t['player']), None)
    if stage == 'Civilization':
        return next((c['pol'] for c in st.civs if c['player']), None)
    return None


def diff_planet(hs, gs, stage):
    """the planet's whole lists (villages, huts, tools, nests, cities, buildings): presence + position + owner"""
    out = []
    hpp, gpp = _player_pol(hs, stage), _player_pol(gs, stage)
    for noun, (label, tol) in PLANET.items():
        a = [x for x in hs.planet if x['noun'] == noun and x['pos'] and not _origin(x['pos'])]
        b = [x for x in gs.planet if x['noun'] == noun and x['pos'] and not _origin(x['pos'])]
        if not a and not b:
            continue
        pairs, ua, ub = _match(a, b, tol)
        if ua or ub:
            ex = (f'; e.g. host only at {_fmt(ua[0]["pos"])}' if ua else '') + (f'; e.g. guest only at {_fmt(ub[0]["pos"])}' if ub else '')
            out.append((f'{stage}:planet', label, len(ua) + len(ub),
                        f'{label}: {len(a)} on the host\'s planet, {len(b)} on the guest\'s; {len(ua)} only on the host, '
                        f'{len(ub)} only on the guest{ex}'))
        # owners: the player's on one game only, or one host owner that is two owners on the guest
        bad, seen = [], {}
        for x, y in pairs:
            if hpp and gpp and (x['pol'] == hpp) != (y['pol'] == gpp):
                bad.append((x, y))
                continue
            g = seen.setdefault(x['pol'], y['pol'])
            if g != y['pol']:
                bad.append((x, y))
        if bad:
            x, y = bad[0]
            out.append((f'{stage}:planet_owner', label, len(bad), f'{len(bad)} of {len(pairs)} {label} with another owner '
                        f'(e.g. at {_fmt(x["pos"])}: {x["pol"]}{" (player)" if x["pol"] == hpp else ""} on the host, '
                        f'{y["pol"]}{" (player)" if y["pol"] == gpp else ""} on the guest)'))
    return out


def _fmtq(q):
    return '(' + ' '.join(f'{v:.2f}' for v in q) + ')'


class Finding(dict):
    """sig: stable signature (stage:what:kind), text: one line, n: objects involved"""

    def __str__(self):
        return f'{self["sig"]} n={self["n"]}: {self["text"]}'


def _near(p, c, r):
    return c is not None and all(map(math.isfinite, p)) and math.dist(p, c) <= r


def _origin(p):
    return all(math.isfinite(v) and abs(v) < 0.01 for v in p)


def _fmt(p):
    return f'({p[0]:.0f} {p[1]:.0f} {p[2]:.0f})'


def _match(a, b, tol):
    """greedy nearest pairs of two row lists within tol: (pairs, unmatched a, unmatched b)"""
    cand = sorted((math.dist(x['pos'], y['pos']), i, j) for i, x in enumerate(a) for j, y in enumerate(b))
    ua, ub, pairs = set(range(len(a))), set(range(len(b))), []
    for d, i, j in cand:
        if d > tol:
            break
        if i in ua and j in ub:
            ua.discard(i)
            ub.discard(j)
            pairs.append((a[i], b[j]))
    return pairs, [a[i] for i in sorted(ua)], [b[j] for j in sorted(ub)]


def _rel(x, y):
    if not (math.isfinite(x) and math.isfinite(y)):
        return 0.0
    m = max(abs(x), abs(y), 1e-6)
    return abs(x - y) / m


def _own_ship_scale(w, ufos):
    """the size of the ship at the game's own position (its own ship), None without one"""
    if not w.me or not ufos:
        return None
    x = min(ufos, key=lambda u: math.dist(u['pos'], w.me))
    s = x['scale']
    return s if math.dist(x['pos'], w.me) < 5.0 and math.isfinite(s) and s > 1e-3 else None


def diff(host, guest, area=None):
    out = []
    stage = host.mode or '?'

    def add(what, kind, n, text):
        out.append(Finding(sig=f'{stage}:{what}:{kind}', stage=stage, n=n, text=text))

    if host.modeid != guest.modeid or host.world != guest.world:
        return [Finding(sig='apart', stage=stage, n=0, text=f'not in the same place: host {host.mode} {host.world}, '
                                                            f'guest {guest.mode} {guest.world}')]
    # BUG-0596: a game that lists no object at all while the other lists its world is loading it (a rejoin, a load:
    # campaign_ct 20261105-015350 compared a rejoining guest with 0 rows, its species table not received yet)
    for w, other in ((guest, host), (host, guest)):
        if w.complete and not w.rows and len(other.rows) >= 10:
            return [Finding(sig='apart', stage=stage, n=0, text=f'the {w.who or w.role}\'s game lists no object (loading)')]
    # BUG-0588, BUG-0594: from the City Hall editor on the host's game takes its village and animals out of the world
    # for the Civ stage (totem pole and food mat destroyed, animals at (0 0 0)); the guest's game has no such phase and
    # shows its Tribe world until the Civ world comes: not the same place (SPORE_DETECTOR_STAGE_END=1 compares anyway)
    hst = getattr(host, 'state', None)
    if hst is not None and hst.stage_end and os.environ.get('SPORE_DETECTOR_STAGE_END') != '1':
        return [Finding(sig='apart', stage=stage, n=0, text='the host\'s game is in the end of its stage (Tribe -> Civ)')]
    # Tribe / Civ: a player's position is its mouse pointer, (0 0 0) when it has none (a game in the
    # background): the host's centre then (the planet's centre is no place to compare)
    # the world state and the whole planet (no centre needed): `<stage>:state:*`, `All:state:*`, `<stage>:planet:*`
    for what, kind, n, text in diff_state(getattr(host, 'state', None), getattr(guest, 'state', None), stage):
        out.append(Finding(sig=f'{what}:{kind}', stage=what.split(':')[0], n=n, text=text))
    c = guest.me if guest.me and not _origin(guest.me) else host.me if host.me and not _origin(host.me) else None
    if c is None:
        if getattr(host, 'state', None) is not None and getattr(guest, 'state', None) is not None:
            return out  # the state was compared: the sample counts, the area compare had no centre
        return [Finding(sig='nocentre', stage=stage, n=0, text='no player position to compare around')]
    r = area or AREA.get(stage, 70.0)
    inner = r * 0.8  # a host object near the edge may be just outside on the guest (dumps are not simultaneous)

    def rows(w, kind, own=None, lim=r):  # objects at (0 0 0) are not placed (pooled): not compared
        return [x for x in w.rows if x['kind'] == kind and (own is None or x['own'] in own) and not _origin(x['pos'])
                and _near(x['pos'], c, lim)]

    # creatures: host npc <-> guest proxy by net id
    before = getattr(host, 'before', None)  # pair_dump's earlier host dump (Cell), see CELL_BRACKET
    for kind, hown, gown, label in (('c', ('npc',), ('proxy',), 'creature'), ('l', ('npc',), ('proxy',), 'cell')):
        h = {x['id']: x for x in rows(host, kind, hown, lim=r * 1.5)}
        g = {x['id']: x for x in rows(guest, kind, gown, lim=r * 1.5)}
        hb = {x['id'] for x in rows(before, kind, hown, lim=r * 1.5)} if before is not None else None
        for i, x in h.items():
            if hb is not None and i not in hb:
                continue  # new on the host since its first dump: the guest may not have it yet
            if i not in g and _near(x['pos'], c, inner):
                add('host_only', label, 1, f'{label} {i} key={x["key"]} at {_fmt(x["pos"])} '
                                          f'{math.dist(x["pos"], c):.0f} m from the guest: the guest has no proxy')
        for i, y in g.items():
            if i not in h and (hb is None or i not in hb) and _near(y['pos'], c, inner):
                add('guest_only', label, 1, f'{label} proxy {i} key={y["key"]} at {_fmt(y["pos"])}: the host has no such object here')
        for i in set(h) & set(g):
            x, y = h[i], g[i]
            if not (_near(x['pos'], c, r) or _near(y['pos'], c, r)):
                continue
            if x['alive'] != y['alive']:
                add('alive', label, 1, f'{label} {i}: alive={x["alive"]} on the host, {y["alive"]} on the guest')
            if _rel(x['scale'], y['scale']) > 0.12:
                add('scale', label, 1, f'{label} {i} key={x["key"]}: scale {x["scale"]:.2f} on the host, {y["scale"]:.2f} on the guest')
            # a guest registers a received creation under a key of its own (0x7C / 0x7D......): not a difference
            if x['key'] != y['key'] and not (y['key'][:2] in ('7C', '7D') or x['key'][:2] in ('7C', '7D')):
                add('model', label, 1, f'{label} {i}: model {x["key"]} on the host, {y["key"]} on the guest')
            if x['alive'] and y['alive'] and math.isfinite(x['hp']) and math.isfinite(y['hp']) and abs(x['hp'] - y['hp']) > max(2.0, 0.2 * max(x['hp'], y['hp'])):
                add('hp', label, 1, f'{label} {i}: hp {x["hp"]:.0f} on the host, {y["hp"]:.0f} on the guest')
            d = math.dist(x['pos'], y['pos'])
            if d > (TOL_MOVING if label == 'creature' else 6.0):
                add('pos', label, 1, f'{label} {i}: {d:.0f} m apart (host {_fmt(x["pos"])}, guest {_fmt(y["pos"])})')
    # objects without a net id on one machine: the guest's own (local) creatures and cells in the area, and
    # the host's local creatures (not tracked by the mod)
    for kind, label in (('c', 'creature'), ('l', 'cell')):
        hl = [x for x in rows(host, kind, ('local',)) if x['ai'] != 0]
        gl = [x for x in rows(guest, kind, ('local',)) if x['ai'] != 0]
        pairs, uh, ug = _match(hl, gl, TOL_MOVING)
        if ug:
            keys = collections.Counter(x['key'] for x in ug)
            add('guest_local', label, len(ug), f'{len(ug)} {label}(s) of the guest\'s own game near it, not on the host '
                                               f'(keys {", ".join(f"{k}x{v}" for k, v in keys.most_common(4))})')
        if uh:
            keys = collections.Counter(x['key'] for x in uh)
            add('host_local', label, len(uh), f'{len(uh)} {label}(s) of the host not tracked by the mod near the guest, '
                                              f'not on the guest (keys {", ".join(f"{k}x{v}" for k, v in keys.most_common(4))})')
    # every other placed object, per noun
    hg = collections.defaultdict(list)
    gg = collections.defaultdict(list)
    for x in rows(host, 'g', lim=r):
        hg[x['noun']].append(x)
    for x in rows(guest, 'g', lim=r):
        gg[x['noun']].append(x)
    for noun in sorted(set(hg) | set(gg)):
        name = noun_name(noun)
        if name in IGNORE:
            continue
        vt = host.vts.get(noun) or guest.vts.get(noun)
        vts = f' [vtable 0x{vt:X}]' if vt and noun not in NOUNS else ''
        a, b = hg.get(noun, []), gg.get(noun, [])
        # Space: a colony planet's citizens are a pool each game puts at the cities near its own camera
        # (docs/re/space.md#space-planet-life, BUG-0490): none of them in the host's game near the guest means
        # the host's camera never came to that city (level of detail), not a desync; the host's citizens that
        # are there are compared (worldobj `space_citizen`)
        if stage == 'Space' and name == 'citizen' and not [x for x in a if _near(x['pos'], c, inner)]:
            continue
        # Civilization: the same pool (150 per planet, 19 at the city near each game's own camera; BUG-0526,
        # coop_civ_citizen_lod.py, lab 20261104-130500): a city with citizens in one game only is a city only that
        # game's camera is at (level of detail); cities with citizens in both games are compared
        if stage == 'Civilization' and name == 'citizen' and (not [x for x in a if _near(x['pos'], c, inner)] or
                                                             not [x for x in b if _near(x['pos'], c, inner)]):
            continue
        tol = TOL_MOVING if name in MOVING or stage == 'Space' else TOL_STATIC  # Space: ships and planets' objects move
        pairs, ua, ub = _match(a, b, tol)
        ua = [x for x in ua if _near(x['pos'], c, inner)]
        ub = [x for x in ub if _near(x['pos'], c, inner)]
        if ua and not b:
            add('host_only', name, len(ua), f'{len(ua)} {name} near the guest on the host, none on the guest '
                                            f'(e.g. at {_fmt(ua[0]["pos"])}){vts}')
        elif ub and not a:
            add('guest_only', name, len(ub), f'{len(ub)} {name} on the guest, none on the host (e.g. at {_fmt(ub[0]["pos"])}){vts}')
        elif ua or ub:
            add('unmatched', name, len(ua) + len(ub), f'{name}: {len(a)} on the host, {len(b)} on the guest, '
                                                       f'{len(ua)} host / {len(ub)} guest without a partner within {tol:.0f} m{vts}')
        if stage == 'Space' and name == 'ufo':
            # BUG-0479 follow-up: every game sizes every ship by its own camera's zoom (docs/re/space.md#space-ufo-scale:
            # 0.922 at zoom 200, 0.664 at 135.8, the partner's ship always the own ship's size): a ship's size is
            # compared relative to the size of the own ship of the game that draws it
            fh, fg = _own_ship_scale(host, a), _own_ship_scale(guest, b)
            sc = [(x, y) for x, y in pairs if fh and fg and _rel(x['scale'] / fh, y['scale'] / fg) > 0.12]
        else:
            sc = [(x, y) for x, y in pairs if _rel(x['scale'], y['scale']) > 0.12]
        if sc:
            x, y = sc[0]
            add('scale', name, len(sc), f'{len(sc)} {name} with another scale (e.g. {x["scale"]:.2f} host, {y["scale"]:.2f} guest)')
    # the whole lists: noun types one game has and the other has none of (any distance)
    for noun in sorted(set(host.nouns) | set(guest.nouns)):
        if noun_name(noun) in IGNORE:
            continue
        # Space: a planet's life (creatures, citizens) is loaded only by a game whose camera is on that
        # planet (level of detail): the whole-list counts differ by design; the area compare still runs
        if stage == 'Space' and noun_name(noun) in ('creature', 'citizen'):
            continue
        h, g = host.nouns.get(noun, 0), guest.nouns.get(noun, 0)
        if (h == 0) != (g == 0) and max(h, g) >= 1:
            vt = host.vts.get(noun) or guest.vts.get(noun)
            add('noun_only_host' if g == 0 else 'noun_only_guest', noun_name(noun), max(h, g),
                f'{noun_name(noun)}: {h} in the host\'s game, {g} in the guest\'s (whole list)'
                + (f', vtable 0x{vt:X}' if vt else ''))
    # one finding per signature and comparison (objects summed, the first text as the example)
    merged = {}
    for f in out:
        m = merged.get(f['sig'])
        if m is None:
            merged[f['sig']] = Finding(f, more=0)
        else:
            m['n'] += f['n']
            m['more'] += 1
    for m in merged.values():
        if m['more']:
            m['text'] += f' (+{m["more"]} more)'
    return list(merged.values())


def main():
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    sys.path.insert(0, os.path.dirname(here))
    from sporectl import Console  # noqa: E402
    import lane  # noqa: E402
    r = float(sys.argv[1]) if len(sys.argv) > 1 else 250
    h, g = Console(lane.HOST, timeout=20), Console(lane.GUEST, timeout=20)
    hw, gw = pair_dump(h, g, r, state=True)
    print(f'host {hw.mode} rows={len(hw.rows)} nouns={len(hw.nouns)}; guest {gw.mode} rows={len(gw.rows)} nouns={len(gw.nouns)}')
    for f in diff(hw, gw):
        print(f)


if __name__ == '__main__':
    main()
