#!/usr/bin/env python3
"""SporeCoop MCP Server (Model Context Protocol, JSON-RPC 2.0 over stdio or Streamable HTTP).

Lets 1..4 autonomous AI agents (e.g. DeepSeek, Claude, or any MCP client) play SporeCoop
together like real players through real UI and keyboard/mouse input, while an automated
background Oracle watches all instances for faults, freezes, stuck pauses, and desyncs.

Zero external pip dependencies (Python 3 stdlib only).

Usage:
  # Stdio MCP transport (for MCP clients such as Claude Desktop, Cursor, Antigravity):
  python3 tools/spore_mcp.py

  # Streamable HTTP MCP transport (for 4 concurrent agents sharing one live 4-player game):
  python3 tools/spore_mcp.py --http 127.0.0.1:8765

  # Attach immediately to already-running SporeKit instances (by instance id 1..40):
  python3 tools/spore_mcp.py --http 127.0.0.1:8765 --attach host=1,guest1=2

  # Select a specific lab lane (default: SPORE_LANE env or 1):
  SPORE_LANE=2 python3 tools/spore_mcp.py --http 127.0.0.1:8765
"""
import argparse
import collections
import datetime
import http.server
import json
import os
import re
import socketserver
import sys
import threading
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TESTS_DIR = os.path.join(HERE, 'tests')
if HERE not in sys.path:
    sys.path.insert(0, HERE)
if TESTS_DIR not in sys.path:
    sys.path.insert(0, TESTS_DIR)

from sporectl import Console, recompress_png  # noqa: E402

MCP_PROTOCOL_VERSION = '2025-03-26'
SERVER_INFO = {'name': 'sporecoop-mcp', 'version': '0.1.0'}

# Common dialog / modal captions scanned during observe() (EN + RU)
DIALOG_CAPTIONS = (
    'OK', 'Accept', 'Decline', 'Yes', 'No', 'Cancel', 'Continue',
    'Alas',
    'I accept your offer! Start shipments immediately!',
    'We happily accept your surrender.',
    'Agreed. Bye for now.',
    'Принять', 'Отклонить', 'Да', 'Нет', 'Отмена', 'Продолжить',
)
# Captions that also exist on persistent/background HUD chrome and should only be reported when a modal dialog is up
MODAL_ONLY_DIALOG_CAPTIONS = (
    'Done', 'Save', 'Close', 'Military', 'Religious', 'Economic',
    'Готово', 'Сохранить', 'Закрыть', 'Военная', 'Религиозная', 'Торговая',
)

# Error patterns scanned in recent SporeKit logs by the Oracle
LOG_ERROR_RE = re.compile(
    r'\b(FAULT|CRASH|ASSERT|0xC0000005|exception|Access Violation|\[ERR\]|deadlock|watchdog)\b',
    re.IGNORECASE,
)


def _utc_stamp():
    return datetime.datetime.now().strftime('%Y%m%d-%H%M%S')


def _safe_slug(text, max_len=48):
    s = re.sub(r'[^a-zA-Z0-9_-]+', '-', (text or 'bug').strip()).strip('-').lower()
    return (s or 'bug')[:max_len]


class PlayerSlot:
    """Thread-safe wrapper around one running SporeKit console instance."""

    def __init__(self, slot_name, display_name, inst, con=None, owned=False, join_cmd=None):
        self.slot_name = slot_name        # 'host', 'guest1', 'guest2', 'guest3'
        self.display_name = display_name  # 'Alice', 'Bob', 'Carol', 'Dave'
        self.inst = int(inst)
        self.con = con if con is not None else Console(self.inst, timeout=30.0)
        self.owned = owned
        self.join_cmd = join_cmd
        self.lock = threading.RLock()
        self.last_frame = None
        self.last_frame_at = 0.0
        self.last_pause_sig = ''
        self.pause_since = 0.0
        self.last_fault_count = 0
        self.seen_log_lines = set()

    def call(self, cmd):
        with self.lock:
            try:
                res = self.con.call(cmd)
                if not isinstance(res, dict):
                    return {'ok': False, 'out': str(res)}
                return res
            except Exception as e:
                return {'ok': False, 'out': f'(console error: {e})'}

    def out(self, cmd):
        return (self.call(cmd).get('out') or '').strip()

    def ok(self, cmd):
        return bool(self.call(cmd).get('ok'))


def _parse_uistate(raw):
    """Parse `coop.uistate` into a compact, LLM-friendly dict of F9 window state and buttons."""
    st = {
        'open': False,
        'page': '',
        'status': '',
        'session': '',
        'rows': [],
        'rivals_rows': [],
        'buttons': [],
        'chat': [],
        'pause_buttons': [],
    }
    if not raw or raw.startswith('(console error'):
        return st
    for line in raw.splitlines():
        if line.startswith('open='):
            for kv in line.split():
                if '=' in kv:
                    k, v = kv.split('=', 1)
                    if k == 'open':
                        st['open'] = v == '1'
                    else:
                        st[k] = v
            continue
        parts = line.split(' ', 1)
        if len(parts) < 2:
            continue
        key, rest = parts
        if key == 'row':
            cols = [p for p in rest.split("'")[1::2]]
            if cols:
                st['rows'].append(cols)
        elif key == 'rrow':
            idx_tok = rest.split(' ', 1)[0]
            cols = [p for p in rest.split("'")[1::2]]
            if idx_tok.isdigit() and cols:
                ridx = int(idx_tok)
                rtxt = cols[0]
                rbtn = cols[1] if len(cols) > 1 else ''
                st['rivals_rows'].append({'index': ridx, 'text': rtxt, 'button': rbtn})
        elif key == 'rpage':
            st['rivals_page'] = rest.strip()
        elif key == 'chat':
            st['chat'].append(rest.strip("'"))
        elif key in ('status', 'session', 'world', 'creation', 'hostaddr'):
            st[key] = rest.strip("'")
        elif key in ('pausereport', 'pausebtn'):
            f = rest.split(' ')
            if len(f) >= 4:
                try:
                    x1, y1, x2, y2 = (int(float(x)) for x in f[:4])
                    cap = rest.split("'")[1] if "'" in rest else key
                    st['pause_buttons'].append({'caption': cap, 'x': (x1 + x2) // 2, 'y': (y1 + y2) // 2})
                except ValueError:
                    pass
        else:
            f = rest.split(' ', 5)
            if len(f) >= 5 and all(re.fullmatch(r'-?[\d.]+', x) for x in f[:4]):
                vis = f[4] == '1'
                if vis:
                    x1, y1, x2, y2 = (int(float(x)) for x in f[:4])
                    cap = f[5].strip("'") if len(f) > 5 else ''
                    st['buttons'].append({
                        'widget': key,
                        'caption': cap,
                        'x': (x1 + x2) // 2,
                        'y': (y1 + y2) // 2,
                    })
    if st['rivals_rows'] and not st['rows']:
        st['rows'] = [[r['text'], r['button']] if r['button'] else [r['text']] for r in st['rivals_rows']]
    st['chat'] = st['chat'][-10:]
    return st


def _parse_targets(raw, limit=25):
    """Parse `coop.targets objs` -> list of visible on-screen targets."""
    items = []
    if not raw or raw.startswith('(console error'):
        return items
    for line in raw.splitlines():
        f = line.split()
        if len(f) == 5 and f[0] == 'member':
            try:
                items.append({'key': 'member' + f[1], 'x': int(f[3]), 'y': int(f[4]), 'dist': -1.0})
            except ValueError:
                pass
        elif len(f) == 5 and f[0] == 'obj':
            try:
                items.append({'key': f[1], 'x': int(f[3]), 'y': int(f[4]), 'dist': round(float(f[2]), 1)})
            except ValueError:
                pass
    # Prefer closer objects first (negative dist = own tribe members first)
    items.sort(key=lambda it: (it['dist'] if it['dist'] >= 0 else -1.0))
    return items[:limit]


def _parse_uifind(raw):
    """Parse `coop.ui find "<caption>"` -> [(cx, cy, visible)]."""
    line_re = re.compile(r'(?:- |Window )\S+ id=\S+ cmd=\S+ fl=0x([0-9A-Fa-f]+) \[(-?\d+) (-?\d+) (-?\d+) (-?\d+)\]')
    groups = []
    cur = []
    for line in (raw or '').splitlines() + ['']:
        if line.startswith('- ') and cur:
            groups.append(cur)
            cur = []
        m = line_re.match(line)
        if m:
            cur.append((int(m.group(1), 16), *(int(m.group(i)) for i in range(2, 6))))
    if cur:
        groups.append(cur)
    res = []
    for g in groups:
        ox = sum(w[1] for w in g[1:])
        oy = sum(w[2] for w in g[1:])
        f, x0, y0, x1, y1 = g[0]
        vis = all(w[0] & 1 for w in g)
        res.append((ox + (x0 + x1) // 2, oy + (y0 + y1) // 2, vis))
    return res


def _parse_fault_count(exc_out):
    m = re.search(r'total faults:\s*(\d+)', exc_out or '', re.IGNORECASE)
    if m:
        return int(m.group(1))
    m2 = re.search(r'\bfaults=(\d+)', exc_out or '')
    return int(m2.group(1)) if m2 else 0


def _parse_frame(frame_out):
    m = re.search(r'frame=(\d+)', frame_out or '')
    return int(m.group(1)) if m else -1


def _detect_stage(mode_out):
    lo = (mode_out or '').lower()
    for name in ('cell', 'creature', 'tribe', 'civ', 'space', 'editor', 'galaxy', 'load'):
        if name in lo:
            return name
    return (mode_out or 'unknown').strip()[:40]


def _truncate_lines(text, max_lines=25, max_chars=2400):
    if not text:
        return ''
    lines = text.splitlines()[:max_lines]
    out = '\n'.join(lines)
    if len(out) > max_chars:
        out = out[:max_chars] + '\n...(truncated)'
    return out


class SporeMcpEngine:
    """Core session & tool engine shared by both MCP stdio/HTTP and ai_playtest.py."""

    def __init__(self, out_root=None, default_lane=None):
        self.out_root = out_root or os.path.join(ROOT, 'out', 'ai-playtest')
        self.default_lane = default_lane
        self.lock = threading.RLock()
        self.players = {}           # slot_name -> PlayerSlot
        self.party = None           # Party instance when started with 3..4 players
        self.owned_twoinst = False  # True when started via twoinst.start (2 players)
        self.started_at = 0.0
        self.fixture = ''
        self.mode_preset = ''
        self.run_dir = ''
        self.actions = collections.deque(maxlen=300)
        self.anomalies = []
        self.bugs = []
        self._anomaly_keys = set()
        self._shot_seq = 0
        self.session_locked = False

    def _ensure_run_dir(self):
        if not self.run_dir:
            self.run_dir = os.path.join(self.out_root, _utc_stamp())
        os.makedirs(os.path.join(self.run_dir, 'shots'), exist_ok=True)
        os.makedirs(os.path.join(self.run_dir, 'bugs'), exist_ok=True)
        return self.run_dir

    def _record_action(self, player, tool, detail):
        entry = {
            'ts': datetime.datetime.now().strftime('%H:%M:%S'),
            'player': player,
            'tool': tool,
            'detail': detail,
        }
        with self.lock:
            self.actions.append(entry)

    def _slot(self, player):
        with self.lock:
            p = self.players.get(player)
        if p is None:
            avail = sorted(self.players.keys())
            raise ValueError(f'Unknown player "{player}". Active players: {avail}')
        return p

    # ---------------- Session lifecycle ----------------
    def session_start(self, fixture='tribe', players=2, mode='rivals', lane_num=None):
        players = int(players)
        if players < 2 or players > 4:
            return {'ok': False, 'error': 'players must be between 2 and 4'}
        if mode not in ('rivals', 'together'):
            return {'ok': False, 'error': 'mode must be "rivals" or "together"'}

        use_lane = lane_num if lane_num is not None else self.default_lane
        if use_lane is not None:
            os.environ['SPORE_LANE'] = str(int(use_lane))

        # Lazy imports so SPORE_LANE takes effect and self-tests do not touch Wine
        import lane as lane_mod
        import twoinst

        with self.lock:
            if self.players:
                self.session_stop()
            self.run_dir = os.path.join(self.out_root, _utc_stamp())
            self._ensure_run_dir()
            self.fixture = fixture
            self.mode_preset = mode
            self.started_at = time.time()
            self.anomalies.clear()
            self.bugs.clear()
            self._anomaly_keys.clear()
            self.actions.clear()

        if players == 2:
            h_con, g_con = twoinst.start(fixture)
            slots = {
                'host': PlayerSlot('host', 'Alice', lane_mod.HOST, con=h_con, owned=True),
                'guest1': PlayerSlot('guest1', 'Bob', lane_mod.GUEST, con=g_con, owned=True, join_cmd=lane_mod.JOIN),
            }
            with self.lock:
                self.players = slots
                self.party = None
                self.owned_twoinst = True
        else:
            from party import Party
            p = Party.start(fixture, players=players)
            slots = {'host': PlayerSlot('host', p.host.name, p.host.inst, con=p.host.con, owned=True)}
            for idx, gp in enumerate(p.guests, 1):
                slots[f'guest{idx}'] = PlayerSlot(
                    f'guest{idx}', gp.name, gp.inst, con=gp.con, owned=True, join_cmd=gp.join
                )
            with self.lock:
                self.players = slots
                self.party = p
                self.owned_twoinst = False

        # Configure Rivals or Together preset on host
        host_slot = self.players['host']
        if mode == 'rivals':
            host_slot.call('coop.rivals preset all_rivals')
        else:
            host_slot.call('coop.rivals preset all_together')
        time.sleep(1.5)

        # Initialize baseline fault counts and frame counters
        for sl in self.players.values():
            sl.last_fault_count = _parse_fault_count(sl.out('exc'))
            sl.last_frame = _parse_frame(sl.out('frame'))
            sl.last_frame_at = time.time()

        self._record_action('system', 'session_start', f'fixture={fixture} players={players} mode={mode}')
        return self.session_status()

    def session_attach(self, instances):
        """Attach to already-running SporeKit consoles: {slot_name: instance_id_or_Console}."""
        if not isinstance(instances, dict) or not instances:
            return {'ok': False, 'error': 'instances must be a non-empty mapping like {"host": 1, "guest1": 2}'}
        with self.lock:
            self._ensure_run_dir()
            self.started_at = self.started_at or time.time()
            self.players = {}
            self.party = None
            self.owned_twoinst = False
            for slot_name, target in instances.items():
                if isinstance(target, int):
                    sl = PlayerSlot(slot_name, slot_name, target, owned=False)
                elif hasattr(target, 'call'):
                    sl = PlayerSlot(slot_name, slot_name, getattr(target, 'inst', 1), con=target, owned=False)
                else:
                    sl = PlayerSlot(slot_name, slot_name, int(target), owned=False)
                sl.last_fault_count = _parse_fault_count(sl.out('exc'))
                sl.last_frame = _parse_frame(sl.out('frame'))
                sl.last_frame_at = time.time()
                self.players[slot_name] = sl
        self._record_action('system', 'session_attach', f'players={sorted(self.players.keys())}')
        return self.session_status()

    def session_status(self):
        with self.lock:
            plist = []
            for name, sl in self.players.items():
                alive = sl.ok('ping')
                mode = sl.out('game.mode') if alive else '(offline)'
                plist.append({
                    'player': name,
                    'display_name': sl.display_name,
                    'instance': sl.inst,
                    'alive': alive,
                    'stage': _detect_stage(mode),
                    'mode': mode,
                })
            return {
                'ok': True,
                'fixture': self.fixture,
                'mode_preset': self.mode_preset,
                'uptime_s': round(time.time() - self.started_at, 1) if self.started_at else 0.0,
                'run_dir': self.run_dir,
                'players': plist,
                'anomalies_count': len(self.anomalies),
                'bugs_count': len(self.bugs),
            }

    def session_stop(self):
        with self.lock:
            party = self.party
            owned_two = self.owned_twoinst
            self.party = None
            self.owned_twoinst = False
            self.players = {}
        try:
            if party is not None:
                party.stop()
            elif owned_two:
                import lane as lane_mod
                lane_mod.sh('kill', check=False)
        except Exception as e:
            return {'ok': False, 'error': str(e)}
        self._record_action('system', 'session_stop', 'stopped')
        return {'ok': True, 'stopped': True, 'run_dir': self.run_dir}

    # ---------------- Observation (Text LLM Vision) ----------------
    def observe(self, player, include_ui_tree=False):
        sl = self._slot(player)
        if not sl.ok('ping'):
            return {'ok': False, 'player': player, 'alive': False, 'error': 'instance not responding to ping'}

        mode_raw = sl.out('game.mode')
        stage = _detect_stage(mode_raw)
        frame = _parse_frame(sl.out('frame'))
        status_raw = sl.out('coop.status')
        avatar_raw = sl.out('coop.avatar')
        res_raw = sl.out('coop.res')
        pauses_raw = sl.out('coop.pauses')
        uistate = _parse_uistate(sl.out('coop.uistate'))
        rivals_raw = sl.out('coop.rivals')
        targets = _parse_targets(sl.out('coop.targets objs'), limit=25)
        exc_raw = sl.out('exc')
        faults = _parse_fault_count(exc_raw)

        # Stage-specific structured summary
        stage_info = {}
        if stage == 'tribe':
            stage_info['tribe'] = _truncate_lines(sl.out('coop.tribe'), 24, 1800)
            stage_info['tribe_rivals'] = _truncate_lines(sl.out('coop.tribe rivals'), 16, 1200)
        elif stage == 'civ':
            stage_info['civ'] = _truncate_lines(sl.out('coop.civ'), 28, 2000)
            stage_info['civ_rivals'] = _truncate_lines(sl.out('coop.civ rivals'), 16, 1200)
        elif stage == 'creature':
            stage_info['creature_rivals'] = _truncate_lines(sl.out('coop.crivals'), 16, 1200)
            stage_info['nearby_npcs'] = _truncate_lines(sl.out('coop.npc list'), 18, 1200)
        elif stage == 'cell':
            stage_info['pool'] = _truncate_lines(sl.out('coop.pool'), 18, 1200)
        elif stage == 'space':
            stage_info['space'] = _truncate_lines(sl.out('coop.space'), 18, 1200)
            stage_info['space_star'] = _truncate_lines(sl.out('coop.space star'), 14, 1000)
            stage_info['fleet'] = _truncate_lines(sl.out('coop.fleet list'), 14, 1000)

        # Check visible modal / dialog captions so agents immediately spot popups
        visible_dialogs = []
        captions_to_scan = list(DIALOG_CAPTIONS)
        if uistate.get('modal') == '1' or uistate.get('pause_buttons'):
            captions_to_scan.extend(MODAL_ONLY_DIALOG_CAPTIONS)
        for cap in captions_to_scan:
            found = _parse_uifind(sl.out(f'coop.ui find "{cap}"'))
            for cx, cy, vis in found:
                if vis:
                    visible_dialogs.append({'caption': cap, 'x': cx, 'y': cy})
                    break

        obs = {
            'ok': True,
            'player': player,
            'display_name': sl.display_name,
            'alive': True,
            'stage': stage,
            'mode': mode_raw,
            'frame': frame,
            'faults': faults,
            'pauses': pauses_raw,
            'session': _truncate_lines(status_raw, 14, 1000),
            'avatar': _truncate_lines(avatar_raw, 6, 400),
            'resources': _truncate_lines(res_raw, 6, 400),
            'rivals': _truncate_lines(rivals_raw, 18, 1400),
            'f9_window': uistate,
            'visible_dialogs': visible_dialogs,
            'screen_targets': targets,
            'stage_state': stage_info,
        }
        if include_ui_tree:
            obs['ui_tree'] = _truncate_lines(sl.out('coop.ui tree 3'), 60, 3500)
        return obs

    # ---------------- Real Player Input Actions ----------------
    def click_ui(self, player, caption, button='left'):
        """Click a visible UI button by caption (checks F9 uistate buttons first, then native UTFWin `coop.ui find`)."""
        sl = self._slot(player)
        btn = button if button in ('left', 'right', 'middle') else 'left'
        cap_lo = (caption or '').strip().lower()
        if not cap_lo:
            return {'ok': False, 'error': 'caption is required'}

        # 1. Check visible buttons in F9 Co-op Window (`coop.uistate`): exact caption/widget match first, then substring
        ui = _parse_uistate(sl.out('coop.uistate'))
        exact_btn = None
        sub_btn = None
        for b in ui['buttons']:
            bcap = (b.get('caption') or '').strip().lower()
            wname = (b.get('widget') or '').strip().lower()
            if bcap == cap_lo or wname == cap_lo:
                exact_btn = b
                break
            if sub_btn is None and cap_lo and cap_lo in bcap:
                sub_btn = b
        chosen_btn = exact_btn or sub_btn
        if chosen_btn is not None:
            sl.call(f'move {chosen_btn["x"]} {chosen_btn["y"]}')
            time.sleep(0.15)
            r = sl.call(f'click {chosen_btn["x"]} {chosen_btn["y"]} {btn}')
            time.sleep(0.3)
            self._record_action(player, 'click_ui', f'f9 "{caption}" at ({chosen_btn["x"]},{chosen_btn["y"]})')
            return {'ok': bool(r.get('ok')), 'source': 'f9_uistate',
                    'matched': chosen_btn['caption'] or chosen_btn['widget'],
                    'widget': chosen_btn['widget'],
                    'x': chosen_btn['x'], 'y': chosen_btn['y']}

        # 2. Check pause menu buttons in uistate
        for b in ui['pause_buttons']:
            if cap_lo in (b.get('caption') or '').lower():
                sl.call(f'move {b["x"]} {b["y"]}')
                time.sleep(0.15)
                r = sl.call(f'click {b["x"]} {b["y"]} {btn}')
                self._record_action(player, 'click_ui', f'pause "{caption}" at ({b["x"]},{b["y"]})')
                return {'ok': bool(r.get('ok')), 'source': 'pause_menu', 'matched': b['caption'],
                        'x': b['x'], 'y': b['y']}

        # 3. Native Spore UTFWin search (`coop.ui find`)
        safe_cap = caption.replace('"', '')
        found = _parse_uifind(sl.out(f'coop.ui find "{safe_cap}"'))
        for cx, cy, vis in found:
            if vis:
                sl.call(f'move {cx} {cy}')
                time.sleep(0.2)
                r = sl.call(f'click {cx} {cy} {btn}')
                self._record_action(player, 'click_ui', f'native "{caption}" at ({cx},{cy})')
                return {'ok': bool(r.get('ok')), 'source': 'native_ui', 'matched': caption, 'x': cx, 'y': cy}

        return {'ok': False, 'error': f'No visible UI button matching "{caption}"'}

    def click_f9_row(self, player, text_contains):
        """Click the action button on a matching row in the open F9 Co-op Window (e.g. Rivals / Settings / Space)."""
        sl = self._slot(player)
        needle = (text_contains or '').strip().lower()
        if not needle:
            return {'ok': False, 'error': 'text_contains is required'}
        raw = sl.out('coop.uistate')
        ui = _parse_uistate(raw)
        if not ui['open']:
            return {'ok': False, 'error': 'F9 window is not open; press_key(player, "f9") first'}

        # 1. Match parsed `rrow <i> '<text>' '<button>'` -> widget `rbtn<i>` (real coop.uistate Rivals format)
        by_widget = {b.get('widget'): b for b in ui['buttons']}
        for rr in ui.get('rivals_rows', []):
            rtxt = (rr.get('text') or '').lower()
            rbtn_cap = (rr.get('button') or '').lower()
            if needle in rtxt or (rbtn_cap and needle in rbtn_cap):
                b = by_widget.get(f'rbtn{rr["index"]}')
                if b:
                    sl.call(f'move {b["x"]} {b["y"]}')
                    time.sleep(0.15)
                    r = sl.call(f'click {b["x"]} {b["y"]} left')
                    time.sleep(0.3)
                    self._record_action(player, 'click_f9_row', f'"{text_contains}" -> "{b["caption"]}" ({b["x"]},{b["y"]})')
                    return {'ok': bool(r.get('ok')), 'row_text': rr['text'], 'button': b['caption'],
                            'widget': b['widget'], 'x': b['x'], 'y': b['y']}

        # 2. Match text widgets (e.g. rivals_text[i] / rtextN) or pair row text with button at similar Y coordinate
        text_widgets = [b for b in ui['buttons'] if needle in (b.get('caption') or '').lower()]
        # Find action buttons (excluding the text label itself) sharing the same row Y (within 18 px)
        for tw in text_widgets:
            candidates = [
                b for b in ui['buttons']
                if b is not tw and abs(b['y'] - tw['y']) <= 18 and b['x'] > tw['x']
            ]
            if candidates:
                btn = min(candidates, key=lambda b: abs(b['y'] - tw['y']))
                sl.call(f'move {btn["x"]} {btn["y"]}')
                time.sleep(0.15)
                r = sl.call(f'click {btn["x"]} {btn["y"]} left')
                time.sleep(0.3)
                self._record_action(player, 'click_f9_row', f'"{text_contains}" -> "{btn["caption"]}" ({btn["x"]},{btn["y"]})')
                return {'ok': bool(r.get('ok')), 'row_text': tw['caption'], 'button': btn['caption'],
                        'x': btn['x'], 'y': btn['y']}

        # 3. Direct button caption match fallback (exact match before substring)
        for exact_only in (True, False):
            for b in ui['buttons']:
                bcap = (b.get('caption') or '').strip().lower()
                if (bcap == needle) if exact_only else (needle in bcap):
                    sl.call(f'move {b["x"]} {b["y"]}')
                    time.sleep(0.15)
                    r = sl.call(f'click {b["x"]} {b["y"]} left')
                    time.sleep(0.3)
                    self._record_action(player, 'click_f9_row', f'direct "{b["caption"]}" ({b["x"]},{b["y"]})')
                    return {'ok': bool(r.get('ok')), 'button': b['caption'], 'widget': b['widget'],
                            'x': b['x'], 'y': b['y']}

        return {'ok': False, 'error': f'No matching F9 row button for "{text_contains}"'}

    def click_target(self, player, target_key, button='left'):
        """Click a live on-screen world object listed in observe().screen_targets by its key."""
        sl = self._slot(player)
        btn = button if button in ('left', 'right', 'middle') else 'left'
        targets = _parse_targets(sl.out('coop.targets objs'), limit=80)
        hit = next((t for t in targets if t['key'] == target_key), None)
        if not hit:
            return {'ok': False, 'error': f'Target "{target_key}" not currently on screen',
                    'available_keys': [t['key'] for t in targets[:15]]}
        sl.call(f'move {hit["x"]} {hit["y"]}')
        time.sleep(0.15)
        r = sl.call(f'click {hit["x"]} {hit["y"]} {btn}')
        self._record_action(player, 'click_target', f'{target_key} ({btn}) at ({hit["x"]},{hit["y"]})')
        return {'ok': bool(r.get('ok')), 'target': hit, 'button': btn}

    def click_screen(self, player, x, y, button='left'):
        sl = self._slot(player)
        btn = button if button in ('left', 'right', 'middle') else 'left'
        ix, iy = int(x), int(y)
        sl.call(f'move {ix} {iy}')
        time.sleep(0.1)
        r = sl.call(f'click {ix} {iy} {btn}')
        self._record_action(player, 'click_screen', f'{btn} ({ix},{iy})')
        return {'ok': bool(r.get('ok')), 'x': ix, 'y': iy, 'button': btn}

    def drag_screen(self, player, x1, y1, x2, y2, button='left'):
        sl = self._slot(player)
        btn = 'right' if button == 'right' else 'left'
        ix1, iy1, ix2, iy2 = int(x1), int(y1), int(x2), int(y2)
        r = sl.call(f'drag {ix1} {iy1} {ix2} {iy2} {btn}')
        self._record_action(player, 'drag_screen', f'{btn} ({ix1},{iy1})->({ix2},{iy2})')
        return {'ok': bool(r.get('ok')), 'from': [ix1, iy1], 'to': [ix2, iy2], 'button': btn}

    def press_key(self, player, key, mode='tap', hold_ms=0):
        sl = self._slot(player)
        k = re.sub(r'[^a-zA-Z0-9_]', '', str(key or '')).lower()
        if k.startswith('key') and len(k) > 3:
            k = k[3:]
        key_aliases = {
            'escape': 'esc',
            'return': 'enter',
            'backspace': 'back',
            'delete': 'del',
        }
        k = key_aliases.get(k, k)
        if not k:
            return {'ok': False, 'error': 'invalid key'}
        hold_ms = max(0, min(5000, int(hold_ms or 0)))
        if hold_ms > 0:
            sl.call(f'key {k} down')
            time.sleep(hold_ms / 1000.0)
            r = sl.call(f'key {k} up')
            self._record_action(player, 'press_key', f'{k} hold {hold_ms}ms')
            return {'ok': bool(r.get('ok')), 'key': k, 'hold_ms': hold_ms}
        m = mode if mode in ('tap', 'down', 'up') else 'tap'
        r = sl.call(f'key {k} {m}')
        self._record_action(player, 'press_key', f'{k} {m}')
        return {'ok': bool(r.get('ok')), 'key': k, 'mode': m}

    def type_text(self, player, text, send_chat=False):
        sl = self._slot(player)
        clean = str(text or '').replace('\r', ' ').replace('\n', ' ').strip()
        if not clean:
            return {'ok': False, 'error': 'text is empty'}
        if send_chat:
            r = sl.call(f'coop.say {clean}')
            self._record_action(player, 'type_text', f'chat: "{clean[:60]}"')
            return {'ok': bool(r.get('ok')), 'sent_chat': True, 'text': clean}
        r = sl.call(f'text {clean}')
        self._record_action(player, 'type_text', f'"{clean[:60]}"')
        return {'ok': bool(r.get('ok')), 'sent_chat': False, 'text': clean}

    def pan_camera(self, player, direction='left', duration_ms=500):
        key_map = {'up': 'w', 'down': 's', 'left': 'a', 'right': 'd', 'in': 'wheel_in', 'out': 'wheel_out'}
        d = (direction or 'left').lower()
        if d not in key_map:
            return {'ok': False, 'error': 'direction must be up|down|left|right|in|out'}
        sl = self._slot(player)
        if d in ('in', 'out'):
            notches = 3 if d == 'in' else -3
            r = sl.call(f'wheel 640 360 {notches}')
            self._record_action(player, 'pan_camera', f'zoom {d}')
            return {'ok': bool(r.get('ok')), 'direction': d}
        ms = max(100, min(2500, int(duration_ms or 500)))
        return self.press_key(player, key_map[d], hold_ms=ms)

    def reconnect_player(self, player):
        """Disconnect a guest and rejoin the host session (tests rejoin/persistence/cleanup paths)."""
        sl = self._slot(player)
        if player == 'host':
            return {'ok': False, 'error': 'reconnect_player is for guests only'}
        if not sl.join_cmd:
            return {'ok': False, 'error': 'no join command known for this attached guest'}
        sl.call('cheat coop leave')
        time.sleep(2.5)
        r = sl.call(sl.join_cmd)
        self._record_action(player, 'reconnect_player', 'leave + rejoin')
        return {'ok': bool(r.get('ok')), 'rejoined': True}

    def screenshot(self, player, label='shot'):
        sl = self._slot(player)
        run_dir = self._ensure_run_dir()
        with self.lock:
            self._shot_seq += 1
            seq = self._shot_seq
        fname = f'{seq:04d}-{player}-{_safe_slug(label, 32)}.png'
        path = os.path.join(run_dir, 'shots', fname)
        r = sl.call(f'shot {path}')
        if r.get('ok') and os.path.exists(path):
            try:
                recompress_png(path, 0.5)
            except Exception:
                pass
            self._record_action(player, 'screenshot', fname)
            return {'ok': True, 'path': path}
        return {'ok': False, 'error': r.get('out') or 'screenshot failed'}

    # ---------------- Automated Bug Oracle & Bug Bundler ----------------
    def _capture_bundle(self, tag, meta):
        run_dir = self._ensure_run_dir()
        bdir = os.path.join(run_dir, 'bugs', f'{_utc_stamp()}-{_safe_slug(tag, 36)}')
        os.makedirs(bdir, exist_ok=True)
        shots = {}
        snapshots = {}
        with self.lock:
            slots = list(self.players.items())
            recent_actions = list(self.actions)[-40:]
        for name, sl in slots:
            spath = os.path.join(bdir, f'{name}.png')
            try:
                if sl.ok('ping'):
                    if sl.call(f'shot {spath}').get('ok') and os.path.exists(spath):
                        try:
                            recompress_png(spath, 0.5)
                        except Exception:
                            pass
                        shots[name] = spath
                    snapshots[name] = {
                        'mode': sl.out('game.mode'),
                        'frame': _parse_frame(sl.out('frame')),
                        'exc': sl.out('exc'),
                        'pauses': sl.out('coop.pauses'),
                        'status': sl.out('coop.status'),
                        'res': sl.out('coop.res'),
                        'log_tail': sl.out('log 40'),
                    }
                else:
                    snapshots[name] = {'alive': False}
            except Exception as e:
                snapshots[name] = {'error': str(e)}
        payload = {
            'captured_at': datetime.datetime.now().isoformat(timespec='seconds'),
            'meta': meta,
            'shots': shots,
            'recent_actions': recent_actions,
            'snapshots': snapshots,
        }
        with open(os.path.join(bdir, 'bundle.json'), 'w', encoding='utf-8') as f:
            json.dump(payload, f, indent=2, ensure_ascii=False)
        return bdir, shots

    def check_anomalies(self):
        """Automated Oracle checking all active players for crashes, faults, hangs, stuck pauses, and errors."""
        now = time.time()
        new_found = []
        with self.lock:
            slots = list(self.players.items())
        for name, sl in slots:
            # 1. Liveness / crash check
            if not sl.ok('ping'):
                key = f'dead:{name}'
                if key not in self._anomaly_keys:
                    self._anomaly_keys.add(key)
                    rec = {'kind': 'crash_or_exit', 'severity': 'P0', 'player': name,
                           'summary': f'{name} (instance {sl.inst}) stopped responding to SporeKit ping'}
                    bdir, _ = self._capture_bundle(f'dead-{name}', rec)
                    rec['bundle_dir'] = bdir
                    with self.lock:
                        self.anomalies.append(rec)
                    new_found.append(rec)
                continue

            # 2. Caught faults (`exc`)
            exc_out = sl.out('exc')
            fc = _parse_fault_count(exc_out)
            if fc > sl.last_fault_count:
                delta = fc - sl.last_fault_count
                sl.last_fault_count = fc
                rec = {'kind': 'fault', 'severity': 'P0', 'player': name,
                       'summary': f'{name} hit {delta} new caught fault(s) (total={fc})',
                       'detail': _truncate_lines(exc_out, 12, 800)}
                bdir, _ = self._capture_bundle(f'fault-{name}-{fc}', rec)
                rec['bundle_dir'] = bdir
                with self.lock:
                    self.anomalies.append(rec)
                new_found.append(rec)

            # 3. Frame freeze / hang check (if frame hasn't moved for 20s outside paused modal)
            fr = _parse_frame(sl.out('frame'))
            pauses = sl.out('coop.pauses')
            mode = _detect_stage(sl.out('game.mode'))
            if fr >= 0:
                if sl.last_frame is None or fr != sl.last_frame:
                    sl.last_frame = fr
                    sl.last_frame_at = now
                elif now - sl.last_frame_at >= 20.0:
                    key = f'freeze:{name}:{fr}'
                    if key not in self._anomaly_keys:
                        self._anomaly_keys.add(key)
                        rec = {'kind': 'freeze', 'severity': 'P0', 'player': name,
                               'summary': f'{name} frame stuck at {fr} for {now - sl.last_frame_at:.0f}s (mode={mode})',
                               'pauses': pauses}
                        bdir, _ = self._capture_bundle(f'freeze-{name}', rec)
                        rec['bundle_dir'] = bdir
                        with self.lock:
                            self.anomalies.append(rec)
                        new_found.append(rec)

            # 4. Stuck pause check (non-empty pause reasons unchanged for >25s during gameplay stage)
            in_gameplay = mode in ('cell', 'creature', 'tribe', 'civ', 'space')
            has_pause = bool(pauses and 'none' not in pauses.lower() and re.search(r'[1-9]', pauses))
            if in_gameplay and has_pause:
                if pauses != sl.last_pause_sig:
                    sl.last_pause_sig = pauses
                    sl.pause_since = now
                elif now - sl.pause_since >= 25.0:
                    key = f'stuck_pause:{name}:{pauses[:40]}'
                    if key not in self._anomaly_keys:
                        self._anomaly_keys.add(key)
                        rec = {'kind': 'stuck_pause', 'severity': 'P1', 'player': name,
                               'summary': f'{name} stuck paused for {now - sl.pause_since:.0f}s in {mode}: {pauses[:120]}'}
                        bdir, _ = self._capture_bundle(f'pause-{name}', rec)
                        rec['bundle_dir'] = bdir
                        with self.lock:
                            self.anomalies.append(rec)
                        new_found.append(rec)
            else:
                sl.last_pause_sig = ''
                sl.pause_since = 0.0

            # 5. Severe log error lines
            log_tail = sl.out('log 25')
            for line in log_tail.splitlines():
                if LOG_ERROR_RE.search(line) and line not in sl.seen_log_lines:
                    sl.seen_log_lines.add(line)
                    if len(sl.seen_log_lines) > 500:
                        sl.seen_log_lines.clear()
                    rec = {'kind': 'log_error', 'severity': 'P1', 'player': name,
                           'summary': f'{name} log error: {line[:180]}'}
                    bdir, _ = self._capture_bundle(f'logerr-{name}', rec)
                    rec['bundle_dir'] = bdir
                    with self.lock:
                        self.anomalies.append(rec)
                    new_found.append(rec)
                    break  # at most 1 bundle per player per tick

        return {'ok': True, 'new_anomalies': new_found, 'total_anomalies': len(self.anomalies)}

    def report_bug(self, player, title, expected, actual, severity='P1', steps=''):
        """File a structured player-perspective bug report with automatic multi-player evidence bundle."""
        sev = severity if severity in ('P0', 'P1', 'P2') else 'P1'
        rec = {
            'id': f'PLAYTEST-{len(self.bugs) + 1:03d}',
            'ts': datetime.datetime.now().strftime('%H:%M:%S'),
            'player': player,
            'severity': sev,
            'title': str(title or 'Untitled bug').strip()[:160],
            'expected': str(expected or '').strip()[:600],
            'actual': str(actual or '').strip()[:600],
            'steps': str(steps or '').strip()[:800],
        }
        bdir, shots = self._capture_bundle(f'{rec["id"]}-{rec["title"]}', rec)
        rec['bundle_dir'] = bdir
        rec['shots'] = shots
        with self.lock:
            self.bugs.append(rec)
        # Also mark in the player's own SporeKit log if alive
        if player in self.players:
            try:
                self.players[player].call(f'coop.bug {rec["id"]}: {rec["title"]}')
            except Exception:
                pass
        self._record_action(player, 'report_bug', f'[{sev}] {rec["title"]}')
        return {'ok': True, 'bug': rec}

    # ---------------- MCP Tool Registry & Dispatcher ----------------
    def tool_definitions(self):
        player_prop = {
            'type': 'string',
            'description': 'Player slot name: "host", "guest1", "guest2", or "guest3".',
        }
        return [
            {
                'name': 'session_start',
                'description': 'Launch a live 2..4 player SporeCoop session on a test fixture (cell, creature, tribe, civ_npc, space_free).',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'fixture': {'type': 'string', 'default': 'tribe',
                                    'description': 'Fixture save name: cell, creature, tribe, civ_npc, space, space_free.'},
                        'players': {'type': 'integer', 'default': 4, 'minimum': 2, 'maximum': 4},
                        'mode': {'type': 'string', 'enum': ['rivals', 'together'], 'default': 'rivals'},
                        'lane': {'type': 'integer', 'description': 'Optional SPORE_LANE number (1..10).'},
                    },
                },
            },
            {
                'name': 'session_attach',
                'description': 'Attach to already-running SporeKit instances without restarting them.',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'instances': {
                            'type': 'object',
                            'description': 'Map of player slot to SporeKit instance number, e.g. {"host": 1, "guest1": 2}.',
                        },
                    },
                    'required': ['instances'],
                },
            },
            {
                'name': 'session_status',
                'description': 'Get connected players, active stages, uptime, anomaly count, and bug report count.',
                'inputSchema': {'type': 'object', 'properties': {}},
            },
            {
                'name': 'session_stop',
                'description': 'Stop a launched multi-player Spore session.',
                'inputSchema': {'type': 'object', 'properties': {}},
            },
            {
                'name': 'observe',
                'description': 'Inspect what a player currently sees: stage, resources, F9 UI window & buttons, visible dialogs, on-screen world targets with (x,y), Rivals state, and stage entities.',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'player': player_prop,
                        'include_ui_tree': {'type': 'boolean', 'default': False,
                                            'description': 'Set true to include a truncated native UTFWin UI tree dump.'},
                    },
                    'required': ['player'],
                },
            },
            {
                'name': 'click_ui',
                'description': 'Click a visible UI button by its caption text (works for F9 buttons, pause menu, dialogs, specialty boxes, and native Spore buttons).',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'player': player_prop,
                        'caption': {'type': 'string', 'description': 'Button text or widget name to click.'},
                        'button': {'type': 'string', 'enum': ['left', 'right', 'middle'], 'default': 'left'},
                    },
                    'required': ['player', 'caption'],
                },
            },
            {
                'name': 'click_f9_row',
                'description': 'Click the action button on a specific row inside the open F9 Co-op window (matches row label text, e.g. "Military", "Declare war", "Buy +25", "Propose ally").',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'player': player_prop,
                        'text_contains': {'type': 'string', 'description': 'Substring of the row label or button caption.'},
                    },
                    'required': ['player', 'text_contains'],
                },
            },
            {
                'name': 'click_target',
                'description': 'Click a live on-screen 3D world object from observe().screen_targets (e.g. hut, tribe member, city, vehicle, fruit tree, NPC).',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'player': player_prop,
                        'target_key': {'type': 'string', 'description': 'Key from observe().screen_targets.'},
                        'button': {'type': 'string', 'enum': ['left', 'right'], 'default': 'left'},
                    },
                    'required': ['player', 'target_key'],
                },
            },
            {
                'name': 'click_screen',
                'description': 'Move mouse and click at explicit client screen coordinates (x, y).',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'player': player_prop,
                        'x': {'type': 'integer'},
                        'y': {'type': 'integer'},
                        'button': {'type': 'string', 'enum': ['left', 'right', 'middle'], 'default': 'left'},
                    },
                    'required': ['player', 'x', 'y'],
                },
            },
            {
                'name': 'drag_screen',
                'description': 'Press, drag, and release the mouse between two screen points (box-select units, orbit camera with right button, drag buildings/parts).',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'player': player_prop,
                        'x1': {'type': 'integer'},
                        'y1': {'type': 'integer'},
                        'x2': {'type': 'integer'},
                        'y2': {'type': 'integer'},
                        'button': {'type': 'string', 'enum': ['left', 'right'], 'default': 'left'},
                    },
                    'required': ['player', 'x1', 'y1', 'x2', 'y2'],
                },
            },
            {
                'name': 'press_key',
                'description': 'Press or hold a keyboard key (e.g. f9, esc, enter, space, tab, w, a, s, d, 1..9).',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'player': player_prop,
                        'key': {'type': 'string'},
                        'mode': {'type': 'string', 'enum': ['tap', 'down', 'up'], 'default': 'tap'},
                        'hold_ms': {'type': 'integer', 'default': 0, 'minimum': 0, 'maximum': 5000},
                    },
                    'required': ['player', 'key'],
                },
            },
            {
                'name': 'type_text',
                'description': 'Type text into the active UI field, or send a co-op chat message when send_chat=true.',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'player': player_prop,
                        'text': {'type': 'string'},
                        'send_chat': {'type': 'boolean', 'default': False},
                    },
                    'required': ['player', 'text'],
                },
            },
            {
                'name': 'pan_camera',
                'description': 'Pan or zoom the player camera (up, down, left, right, in, out) to find more targets on screen.',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'player': player_prop,
                        'direction': {'type': 'string', 'enum': ['up', 'down', 'left', 'right', 'in', 'out']},
                        'duration_ms': {'type': 'integer', 'default': 500, 'minimum': 100, 'maximum': 2500},
                    },
                    'required': ['player', 'direction'],
                },
            },
            {
                'name': 'reconnect_player',
                'description': 'Disconnect a guest player and rejoin the host session to test rejoin/state persistence.',
                'inputSchema': {
                    'type': 'object',
                    'properties': {'player': player_prop},
                    'required': ['player'],
                },
            },
            {
                'name': 'screenshot',
                'description': 'Save a PNG screenshot of the player window to the playtest artifacts folder.',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'player': player_prop,
                        'label': {'type': 'string', 'default': 'shot'},
                    },
                    'required': ['player'],
                },
            },
            {
                'name': 'check_anomalies',
                'description': 'Run the automated bug Oracle across all active players (checks crashes, caught faults, frame freezes, stuck pauses, and log errors).',
                'inputSchema': {'type': 'object', 'properties': {}},
            },
            {
                'name': 'report_bug',
                'description': 'File a bug report whenever a real gameplay action fails, UI breaks, state desyncs, or UX blocks the player. Automatically saves screenshots and logs from all players.',
                'inputSchema': {
                    'type': 'object',
                    'properties': {
                        'player': player_prop,
                        'title': {'type': 'string', 'description': 'Short descriptive bug title.'},
                        'severity': {'type': 'string', 'enum': ['P0', 'P1', 'P2'], 'default': 'P1'},
                        'expected': {'type': 'string', 'description': 'What the player expected to happen.'},
                        'actual': {'type': 'string', 'description': 'What actually happened in the game.'},
                        'steps': {'type': 'string', 'description': 'Reproduction steps performed.'},
                    },
                    'required': ['player', 'title', 'expected', 'actual'],
                },
            },
        ]

    def call_tool(self, name, args=None):
        args = args or {}
        if self.session_locked and name in ('session_start', 'session_stop', 'session_attach'):
            return {'ok': False, 'error': 'Session lifecycle is managed by the playtest orchestrator'}
        dispatch = {
            'session_start': lambda a: self.session_start(
                fixture=a.get('fixture', 'tribe'),
                players=a.get('players', 4),
                mode=a.get('mode', 'rivals'),
                lane_num=a.get('lane'),
            ),
            'session_attach': lambda a: self.session_attach(a.get('instances') or {}),
            'session_status': lambda a: self.session_status(),
            'session_stop': lambda a: self.session_stop(),
            'observe': lambda a: self.observe(a['player'], include_ui_tree=bool(a.get('include_ui_tree', False))),
            'click_ui': lambda a: self.click_ui(a['player'], a['caption'], button=a.get('button', 'left')),
            'click_f9_row': lambda a: self.click_f9_row(a['player'], a['text_contains']),
            'click_target': lambda a: self.click_target(a['player'], a['target_key'], button=a.get('button', 'left')),
            'click_screen': lambda a: self.click_screen(a['player'], a['x'], a['y'], button=a.get('button', 'left')),
            'drag_screen': lambda a: self.drag_screen(a['player'], a['x1'], a['y1'], a['x2'], a['y2'],
                                                      button=a.get('button', 'left')),
            'press_key': lambda a: self.press_key(a['player'], a['key'], mode=a.get('mode', 'tap'),
                                                  hold_ms=a.get('hold_ms', 0)),
            'type_text': lambda a: self.type_text(a['player'], a['text'], send_chat=bool(a.get('send_chat', False))),
            'pan_camera': lambda a: self.pan_camera(a['player'], direction=a.get('direction', 'left'),
                                                    duration_ms=a.get('duration_ms', 500)),
            'reconnect_player': lambda a: self.reconnect_player(a['player']),
            'screenshot': lambda a: self.screenshot(a['player'], label=a.get('label', 'shot')),
            'check_anomalies': lambda a: self.check_anomalies(),
            'report_bug': lambda a: self.report_bug(
                a['player'], a['title'], a['expected'], a['actual'],
                severity=a.get('severity', 'P1'), steps=a.get('steps', ''),
            ),
        }
        fn = dispatch.get(name)
        if fn is None:
            raise KeyError(f'Unknown tool: {name}')
        return fn(args)


# ---------------- JSON-RPC 2.0 MCP Protocol Handler ----------------
def handle_jsonrpc(engine, req):
    """Process one JSON-RPC 2.0 request dict; returns response dict or None for notifications."""
    if not isinstance(req, dict):
        return {'jsonrpc': '2.0', 'id': None, 'error': {'code': -32600, 'message': 'Invalid Request'}}
    req_id = req.get('id')
    method = req.get('method', '')
    params = req.get('params') or {}

    if method == 'notifications/initialized' or (req_id is None and method.startswith('notifications/')):
        return None

    try:
        if method == 'initialize':
            return {
                'jsonrpc': '2.0',
                'id': req_id,
                'result': {
                    'protocolVersion': MCP_PROTOCOL_VERSION,
                    'capabilities': {'tools': {'listChanged': False}},
                    'serverInfo': SERVER_INFO,
                },
            }
        if method == 'ping':
            return {'jsonrpc': '2.0', 'id': req_id, 'result': {}}
        if method == 'tools/list':
            return {'jsonrpc': '2.0', 'id': req_id, 'result': {'tools': engine.tool_definitions()}}
        if method == 'tools/call':
            tname = params.get('name', '')
            targs = params.get('arguments') or {}
            try:
                res = engine.call_tool(tname, targs)
                is_err = isinstance(res, dict) and res.get('ok') is False
                return {
                    'jsonrpc': '2.0',
                    'id': req_id,
                    'result': {
                        'content': [{'type': 'text', 'text': json.dumps(res, ensure_ascii=False)}],
                        'isError': is_err,
                    },
                }
            except KeyError as e:
                return {'jsonrpc': '2.0', 'id': req_id, 'error': {'code': -32601, 'message': str(e)}}
            except Exception as e:
                return {
                    'jsonrpc': '2.0',
                    'id': req_id,
                    'result': {
                        'content': [{'type': 'text', 'text': json.dumps({'ok': False, 'error': str(e)})}],
                        'isError': True,
                    },
                }
        return {'jsonrpc': '2.0', 'id': req_id, 'error': {'code': -32601, 'message': f'Method not found: {method}'}}
    except Exception as e:
        return {
            'jsonrpc': '2.0',
            'id': req_id,
            'error': {'code': -32603, 'message': f'Internal error: {e}', 'data': traceback.format_exc()},
        }


def serve_stdio(engine):
    """Run MCP over newline-delimited JSON-RPC 2.0 on stdin/stdout."""
    for raw in sys.stdin:
        line = raw.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError as e:
            resp = {'jsonrpc': '2.0', 'id': None, 'error': {'code': -32700, 'message': f'Parse error: {e}'}}
            sys.stdout.write(json.dumps(resp, ensure_ascii=False) + '\n')
            sys.stdout.flush()
            continue
        resp = handle_jsonrpc(engine, req)
        if resp is not None:
            sys.stdout.write(json.dumps(resp, ensure_ascii=False) + '\n')
            sys.stdout.flush()


class _ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def make_http_server(engine, host='127.0.0.1', port=8765):
    """Create a multi-threaded Streamable HTTP MCP server (`POST /mcp`, `GET /health`)."""
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            sys.stderr.write(f'[spore-mcp] {self.address_string()} - {fmt % args}\n')

        def _send_json(self, code, obj):
            body = json.dumps(obj, ensure_ascii=False).encode('utf-8')
            try:
                self.send_response(code)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.send_header('Mcp-Session-Id', 'sporecoop-mcp-session')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def do_OPTIONS(self):
            self.send_response(204)
            self.send_header('Allow', 'GET, POST, DELETE, OPTIONS')
            self.end_headers()

        def do_DELETE(self):
            self.send_response(200)
            self.send_header('Mcp-Session-Id', 'sporecoop-mcp-session')
            self.end_headers()

        def do_GET(self):
            if self.path == '/health':
                self._send_json(200, {'ok': True, 'server': SERVER_INFO, 'session': engine.session_status()})
                return
            if self.path in ('/mcp', '/'):
                # Per MCP Streamable HTTP spec, 405 tells clients this server uses POST JSON responses without SSE.
                self.send_response(405)
                self.send_header('Allow', 'POST, DELETE, OPTIONS')
                self.end_headers()
                return
            self._send_json(404, {'ok': False, 'error': 'Use POST /mcp or GET /health'})

        def do_POST(self):
            if self.path not in ('/mcp', '/'):
                self._send_json(404, {'ok': False, 'error': 'Unknown path'})
                return
            try:
                length = int(self.headers.get('Content-Length', '0'))
                body = self.rfile.read(length) if length > 0 else b''
                req = json.loads(body.decode('utf-8'))
            except Exception as e:
                self._send_json(400, {'jsonrpc': '2.0', 'id': None, 'error': {'code': -32700, 'message': str(e)}})
                return
            if isinstance(req, list):
                out = [r for r in (handle_jsonrpc(engine, item) for item in req) if r is not None]
                self._send_json(200, out)
                return
            resp = handle_jsonrpc(engine, req)
            if resp is None:
                self.send_response(202)
                self.send_header('Mcp-Session-Id', 'sporecoop-mcp-session')
                self.end_headers()
                return
            self._send_json(200, resp)

    return _ThreadedHTTPServer((host, int(port)), Handler)


def parse_attach_arg(raw):
    """Parse 'host=1,guest1=2,guest2=21,guest3=31' -> dict."""
    out = {}
    for item in (raw or '').split(','):
        item = item.strip()
        if not item:
            continue
        if '=' not in item:
            raise ValueError(f'Invalid --attach entry "{item}", expected name=instance_num')
        k, v = item.split('=', 1)
        out[k.strip()] = int(v.strip())
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description='SporeCoop MCP server for AI playtesting (stdio or HTTP)')
    ap.add_argument('--http', metavar='HOST:PORT', default='',
                    help='Serve Streamable HTTP MCP (e.g. 127.0.0.1:8765) instead of stdio')
    ap.add_argument('--attach', default='',
                    help='Attach to running SporeKit instances, e.g. host=1,guest1=2')
    ap.add_argument('--start-fixture', default='',
                    help='Launch a live session on startup (cell, creature, tribe, civ_npc, space_free)')
    ap.add_argument('--players', type=int, default=4, choices=[2, 3, 4],
                    help='Player count for --start-fixture (default 4)')
    ap.add_argument('--mode', default='rivals', choices=['rivals', 'together'],
                    help='Rivals or Together preset for --start-fixture')
    ap.add_argument('--oracle-interval', type=float, default=5.0,
                    help='Background Oracle polling interval in seconds when --http is active (0 = off)')
    ap.add_argument('--lane', type=int, default=None, help='Default SPORE_LANE for session_start (1..10)')
    args = ap.parse_args(argv)

    engine = SporeMcpEngine(default_lane=args.lane)
    if args.attach:
        engine.session_attach(parse_attach_arg(args.attach))
    elif args.start_fixture:
        sys.stderr.write(f'[spore-mcp] starting {args.players}p {args.start_fixture} ({args.mode})...\n')
        st = engine.session_start(fixture=args.start_fixture, players=args.players, mode=args.mode, lane_num=args.lane)
        sys.stderr.write(f'[spore-mcp] session ready: {json.dumps(st, ensure_ascii=False)}\n')

    if args.http:
        host, port_s = args.http.rsplit(':', 1) if ':' in args.http else ('127.0.0.1', args.http)
        srv = make_http_server(engine, host, int(port_s))
        stop_evt = threading.Event()
        oracle_t = None
        if args.oracle_interval > 0:
            def _oracle():
                while not stop_evt.is_set():
                    if engine.players:
                        try:
                            res = engine.check_anomalies()
                            for a in (res.get('new_anomalies') or []):
                                sys.stderr.write(
                                    f'[spore-mcp][ORACLE][{a.get("severity")}] {a.get("player")}: {a.get("summary")}\n'
                                )
                        except Exception as e:
                            sys.stderr.write(f'[spore-mcp][ORACLE] error: {e}\n')
                    stop_evt.wait(args.oracle_interval)
            oracle_t = threading.Thread(target=_oracle, daemon=True)
            oracle_t.start()
        sys.stderr.write(f'[spore-mcp] listening on http://{host}:{port_s}/mcp (health: /health)\n')
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            stop_evt.set()
            srv.server_close()
            engine.session_stop()
    else:
        serve_stdio(engine)


if __name__ == '__main__':
    main()
