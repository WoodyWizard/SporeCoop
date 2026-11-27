#!/usr/bin/env python3
"""4-Agent DeepSeek Autonomous Playtest Runner for SporeCoop.

Spawns 2..4 parallel AI player agents (DeepSeek Chat / Reasoner or any OpenAI-compatible
endpoint) with distinct human-like personas (Leader, Diplomat, Aggressor, Chaos QA)
playing SporeCoop simultaneously through the SporeCoop MCP tools, plus a background
Oracle thread watching all instances for crashes, faults, hangs, and stuck pauses.

Writes a complete Markdown report with screenshots, logs, and repro steps to:
  out/ai-playtest/<timestamp>/report.md

Zero external pip dependencies (Python 3 stdlib only).

Usage:
  export DEEPSEEK_API_KEY="sk-..."

  # Deep gameplay test on Civ Rivals (15 min, OpenCode Go DeepSeek):
  LAB_LANE=2 python3 tools/ai_playtest.py --backend opencode --focus gameplay --players 4 --fixture civ_npc --mode rivals --minutes 15

  # Overnight 6-hour autonomous marathon from Cell (or Tribe/Civ) with live report checkpoints:
  LAB_LANE=2 python3 tools/ai_playtest.py --backend opencode --focus gameplay --players 4 --fixture cell --mode rivals --minutes 360 --round-timeout 150

  # Connect to an already-running MCP HTTP server:
  python3 tools/ai_playtest.py --mcp-url http://127.0.0.1:8765/mcp --minutes 10

  # Offline dry-run with mock LLM (verifies full 4-agent pipeline with zero API calls):
  python3 tools/ai_playtest.py --mock --players 4 --steps 4
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import spore_mcp  # noqa: E402

# Default OpenCode Go DeepSeek models per role:
# deepseek-v4.1-flash (1017) is faster (~175 tok/s), natively multimodal (can inspect its own screenshots),
# and matches or beats v4-pro on agentic benchmarks.
OPENCODE_MODELS = {
    'host': 'opencode-go/deepseek-v4.1-flash',
    'guest1': 'opencode-go/deepseek-v4.1-flash',
    'guest2': 'opencode-go/deepseek-v4.1-flash',
    'guest3': 'opencode-go/deepseek-v4.1-flash',
}

PERSONAS = {
    'host': {
        'title': 'Host / Normal Leader',
        'goal': (
            'Play normally as the session host: open F9 to inspect race progress and relations, '
            'build up your tribe/city/empire, interact with NPCs, test alliances or wars, '
            'and verify that stage goals and UI controls respond cleanly.'
        ),
    },
    'guest1': {
        'title': 'Guest 1 / Diplomat & Social Tester',
        'goal': (
            'Play as a peaceful/social guest: in Tribe try buying musical instruments and befriending NPCs; '
            'in Civ open F9 -> Rivals to test choosing your starting specialty (Military/Religious/Economic) '
            'and sending diplomatic gifts/alliances; in Creature try socializing; verify progress bars update.'
        ),
    },
    'guest2': {
        'title': 'Guest 2 / Aggressor & Combat Tester',
        'goal': (
            'Play as an aggressive rival guest: open F9 -> Rivals to declare war or buy Influence, '
            'select units/vehicles, attack NPCs or rival targets, try capturing huts/cities, '
            'and test combat tools/superweapons.'
        ),
    },
    'guest3': {
        'title': 'Guest 3 / Chaos QA Tester',
        'goal': (
            'Act like an unpredictable human tester: open and close F9 pages, toggle buttons, '
            'try opening planners/editors and selling or modifying buildings, test pan_camera + screen clicks, '
            'occasionally reconnect_player to test rejoin persistence, and verify nothing crashes or locks up.'
        ),
    },
}

GAMEPLAY_PERSONAS = {
    'host': {
        'title': 'Host / Strategic Progression Leader',
        'goal': (
            'Drive real stage progression like a dedicated human player: gather resources, build structures/units, '
            'ally or conquer NPCs, enter editors to upgrade your creature/vehicle/city, and push toward stage victory '
            'and the next stage transition. Do NOT camp in F9 menus; spend 85%+ of actions playing the 3D world.'
        ),
    },
    'guest1': {
        'title': 'Guest 1 / Builder, Economist & Diplomat',
        'goal': (
            'Play the peaceful/economic path in the live 3D world: in Civ pick Economic or Religious specialty once '
            'in round 1 then close F9, open City Planner to build Houses/Factories/Entertainment, buy vehicles, capture '
            'spice geysers, and trade/convert NPC cities; in Tribe gather food, build instrument huts and befriend NPC '
            'tribes in social_mode; in Creature socialize (Tab -> social, 1-4) and evolve; in Space trade spice and terraform.'
        ),
    },
    'guest2': {
        'title': 'Guest 2 / Military Aggressor & Raider',
        'goal': (
            'Play the military/combat path in the live 3D world: build combat vehicles or weapon huts, box-select your '
            'units, claim spice geysers, raid/besiege NPC tribes/cities or hostile rival players, test combat abilities '
            'and superweapons when unlocked, and push aggressively toward stage victory.'
        ),
    },
    'guest3': {
        'title': 'Guest 3 / Explorer, Planner & Stress QA',
        'goal': (
            'Actively exercise planners, editors, and map exploration: open City/Tribe Planner or Creature/Cell Editor '
            'to add/move/sell buildings or parts and save cleanly, explore across the planet/galaxy with pan_camera and '
            'wheel zoom, fight or socialize with distant NPCs, and every ~6 rounds run reconnect_player mid-match to '
            'verify persistence without crashes or lockups.'
        ),
    },
}

# Stage-specific real-gameplay playbooks (100% real mouse/keyboard/UI inputs, zero cheats).
# Injected dynamically based on each player's LIVE stage so overnight multi-stage runs adapt automatically.
STAGE_PLAYBOOKS = {
    'cell': (
        'CELL STAGE PLAYBOOK:\n'
        '- CHECK YOUR DIET FIRST in `observe().stage_state.pool` (`diet=herb` vs `diet=carn` vs `diet=omni`)!\n'
        '  * Herbivore (`diet=herb`, filter mouth) can ONLY eat GREEN plant chunks; red meat chunks and clear air bubbles '
        'give ZERO food (do NOT waste turns chasing red meat or file a bug when meat gives 0 food!).\n'
        '  * Carnivore (`diet=carn`) eats red/pink meat chunks and hunts smaller cells.\n'
        '- Swim continuously using `perform_actions` with `press_key` (w/a/s/d, hold_ms 700..1400) and left-clicks on '
        'edible food from `screen_targets` (use parameter `target_key`) or your 1:1 `screenshot`.\n'
        '- Avoid much larger predators; if at war with a rival player cell, chase and bite them.\n'
        '- Click `click_hud("mate_call")` when you want to enter the Cell Editor; exit via `click_hud("editor_done")`.\n'
        '- CRITICAL FOR STAGE ADVANCE: once your evolution bar reaches 100% (`food=1000`, glowing cyan bottom bar and '
        'Cell->Creature icon at bottom-right), click `click_hud("cell_advance")` to evolve into Creature stage!'
    ),
    'creature': (
        'CREATURE STAGE PLAYBOOK:\n'
        '- WATCH YOUR ORANGE HUNGER BAR (bottom-right below green HP): if it empties, your HP drains until you starve '
        'to death! Eat fruit trees (`fruittree`, herbivores/omnivores) or meat carcasses (carnivores/omnivores) regularly!\n'
        '- If a letterboxed cinematic (black bars top/bottom, e.g. stage intro or death message) is on screen, click the '
        'green checkmark at bottom-right `click_screen(1414, 1058)` or `press_key("enter")` to continue.\n'
        '- Click nearby wild creatures or fruit trees (`click_target`), move with `press_key` (w/a/s/d, hold_ms 800..1500), '
        'and orbit camera via `drag_screen(..., button="right")`.\n'
        '- Toggle Social vs Combat stance with `press_key("tab")`, then chain abilities `1`, `2`, `3`, `4` with short '
        '`wait` steps inside ONE `perform_actions` call to impress or hunt wild species!\n'
        '- When you have DNA, return to your nest, press `backspace` (Mating Call), and click your heart-marked mate to '
        'enter the Creature Editor; dismiss `Brain power!` popups with `press_key("enter")` or `click_ui("OK")`.'
    ),
    'tribe': (
        'TRIBE STAGE PLAYBOOK:\n'
        '- Select your own tribe members via `click_target("member1")` or box-select (`drag_screen` across your village).\n'
        '- Switch stance via `click_hud("social_mode")` or `click_hud("attack_mode")`, then right-click (`button="right"`) '
        'fruit trees/fish/animals to gather food, or right-click rival/NPC huts and members to raid or serenade!\n'
        '- Click your own main hut (`hut*`) and take a `screenshot` + `Read` it to open the Tribe Planner / buy tool huts '
        '(Axe, Spear, Horn, Didgeridoo, Maracas) and place them on green slots, or buy babies when you have food.\n'
        '- When social/combat concert starts, press `1`, `2`, `3` matching the requested instruments!'
    ),
    'civ': (
        'CIVILIZATION STAGE PLAYBOOK:\n'
        '- Round 1 (guests only): optionally open F9 -> Rivals ONCE to pick starting specialty (Military/Religious/Economic), '
        'then close F9 (`press_key("esc")`) and play the 3D planet!\n'
        '- Click your own city (`click_target`), then check `observe().hud_buttons`:\n'
        '  * `click_hud("city_planner")` opens the City Planner! Take a `screenshot` + `Read` it to click building palette '
        'icons (House, Factory, Entertainment, Turret) and click open slots inside the city ring, then `click_hud("planner_done")`.\n'
        '  * `click_hud("buy_land_vehicle")` / `buy_water_vehicle` / `buy_air_vehicle` buys vehicles!\n'
        '- Select your vehicle (`click_target` or drag-select) and right-click (`button="right"`) Spice Geysers or NPC/rival '
        'cities (`attack_city` / `convert_city` HUD buttons when shown) to expand your empire!\n'
        '- Answer captured-city specialty popups (`Military` / `Religious` / `Economic`) via `click_ui`.'
    ),
    'space': (
        'SPACE STAGE PLAYBOOK:\n'
        '- Use `perform_actions` with `wheel` (`notches=-5` twice at center 720,540) to zoom out Planet -> Solar System -> '
        'Galaxy View, then `screenshot` + `Read` to click neighboring stars and fly between systems!\n'
        '- Zoom back in (`notches=5`) to enter a star system and click planets to land; move your ship with clicks or WASD.\n'
        '- Inspect the bottom tool/cargo palette on screenshots to pick up/sell Spice, fire lasers at pirates/enemies, '
        'place colonies, or accept/complete empire missions.\n'
        '- If the Captain Equipment panel opens (`Equipment` dialog), close it via `click_ui("OK")`.'
    ),
    'editor': (
        'EDITOR PLAYBOOK:\n'
        '- You are currently inside a Spore Editor (Cell / Creature / Building / Vehicle / Outfitter)!\n'
        '- Take a `screenshot` + `Read` it (1:1 native pixel coordinates): drag 1 part from the left palette onto the '
        'creation, then click the green checkmark (`click_hud("editor_done")`) to save & exit back to live gameplay.\n'
        '- If the editor refuses to save (e.g. empty creation) or you want to abort back to the stage, click the red X '
        'cancel button (`click_hud("editor_cancel")`) and confirm `Yes`/`Cancel` via `click_ui` so you never get stuck!'
    ),
}

# Only player-facing tools are exposed to DeepSeek agents (never session_start/stop or cheats)
PLAYER_TOOL_NAMES = {
    'observe', 'click_ui', 'click_f9_row', 'click_hud', 'click_target', 'click_screen',
    'drag_screen', 'press_key', 'type_text', 'pan_camera', 'perform_actions', 'reconnect_player',
    'screenshot', 'report_bug',
}


def build_system_prompt(player, persona, fixture, mode, focus='gameplay', live_stage=''):
    st = (live_stage or _stage_from_fixture(fixture)).lower()
    playbook = STAGE_PLAYBOOKS.get(st, '')
    focus_rules = ''
    if focus == 'gameplay':
        focus_rules = (
            '\nDEEP GAMEPLAY FOCUS:\n'
            '- Spend 85%+ of your actions playing the real 3D game world, NOT clicking F9 menus!\n'
            '- Keep the F9 window CLOSED during normal play (only open F9 briefly in Round 1 for initial setup '
            'like Civ starting specialty/war, or every ~5 rounds to check race progress/proposals, then press Esc to close it).\n'
            '- Use `perform_actions` to batch 4..10 real mouse/keyboard/HUD steps in ONE call so you play at human speed!\n'
            '- Use `hud_buttons` from `observe()` + `click_hud` for native Spore controls (city_planner, buy_land_vehicle, '
            'social_mode, attack_mode, planner_done, editor_done).\n'
            '- If `pause_menu_open` is true, immediately click `Resume Game` or press `esc` to unpause.\n'
        )
    playbook_block = f'\n{playbook}\n' if playbook else ''
    return (
        f'You are an autonomous QA playtester controlling player "{player}" ({persona["title"]}) '
        f'in a live multiplayer SporeCoop game (fixture={fixture}, current_stage={st}, mode={mode}).\n\n'
        f'Your persona & goals:\n{persona["goal"]}\n'
        f'{focus_rules}'
        f'{playbook_block}\n'
        'Rules:\n'
        '1. ALWAYS call `observe` first and after significant actions so you know the real game state.\n'
        '2. Play ONLY through real player inputs (`perform_actions`, `click_hud`, `click_target`, `click_ui`, '
        '`click_f9_row`, `click_screen`, `drag_screen`, `press_key`, `type_text`, `pan_camera`, `reconnect_player`).\n'
        '3. Always pass player="' + player + '" to every tool call.\n'
        '4. Watch carefully for player-facing bugs: broken gameplay loops (units ignoring orders, vehicles not spawning, '
        'money/food not updating, planner/editor failing to open/save/spend, attacks/social doing nothing, stuck modals, '
        'missing guest cities/tribes/ships, progress bars frozen, desyncs).\n'
        '5. Whenever an action clearly fails to do what a real Spore player expects, IMMEDIATELY call '
        '`report_bug` with clear expected vs actual behavior and steps, then keep playing.\n'
        '6. Keep replies very brief (2 lines max: what you did + next plan).'
    )


def _stage_from_fixture(fixture):
    fx = (fixture or '').lower()
    for name in ('cell', 'creature', 'tribe', 'civ', 'space'):
        if name in fx:
            return name
    return 'unknown'


def mcp_tools_to_openai(tool_defs):
    """Convert MCP tool definitions to OpenAI/DeepSeek function-calling tool schemas."""
    out = []
    for td in tool_defs:
        if td['name'] not in PLAYER_TOOL_NAMES:
            continue
        out.append({
            'type': 'function',
            'function': {
                'name': td['name'],
                'description': td.get('description', ''),
                'parameters': td.get('inputSchema', {'type': 'object', 'properties': {}}),
            },
        })
    return out


class RemoteMcpClient:
    """Thin JSON-RPC 2.0 client over HTTP to `tools/spore_mcp.py --http`."""

    def __init__(self, url):
        self.url = url.rstrip('/')
        if not self.url.endswith('/mcp'):
            self.url += '/mcp'
        self._id = 0
        self._lock = threading.Lock()
        self._rpc('initialize', {'protocolVersion': spore_mcp.MCP_PROTOCOL_VERSION, 'capabilities': {},
                                 'clientInfo': {'name': 'ai_playtest', 'version': '0.1.0'}})

    def _rpc(self, method, params=None):
        with self._lock:
            self._id += 1
            rid = self._id
        payload = json.dumps({'jsonrpc': '2.0', 'id': rid, 'method': method, 'params': params or {}}).encode('utf-8')
        req = urllib.request.Request(self.url, data=payload, headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=120) as resp:
            data = json.loads(resp.read().decode('utf-8'))
        if 'error' in data:
            raise RuntimeError(f'MCP error {data["error"]}')
        return data.get('result') or {}

    def tool_definitions(self):
        return self._rpc('tools/list').get('tools', [])

    def call_tool(self, name, args=None):
        res = self._rpc('tools/call', {'name': name, 'arguments': args or {}})
        content = res.get('content') or []
        if content and content[0].get('type') == 'text':
            return json.loads(content[0]['text'])
        return res


class DeepSeekClient:
    """Zero-dependency OpenAI-compatible client for DeepSeek API."""

    def __init__(self, api_key, base_url='https://api.deepseek.com/v1', model='deepseek-chat', temperature=0.7):
        self.api_key = api_key
        self.base_url = base_url.rstrip('/')
        self.model = model
        self.temperature = temperature
        self.lock = threading.Lock()
        self.prompt_tokens = 0
        self.completion_tokens = 0
        self.calls = 0

    def chat(self, messages, tools):
        url = f'{self.base_url}/chat/completions'
        body = {
            'model': self.model,
            'messages': messages,
            'tools': tools,
            'tool_choice': 'auto',
            'max_tokens': 512,
        }
        if 'reasoner' not in self.model:
            body['temperature'] = self.temperature
        req = urllib.request.Request(
            url,
            data=json.dumps(body, ensure_ascii=False).encode('utf-8'),
            headers={
                'Content-Type': 'application/json',
                'Authorization': f'Bearer {self.api_key}',
            },
        )
        with urllib.request.urlopen(req, timeout=90) as resp:
            data = json.loads(resp.read().decode('utf-8'))
        usage = data.get('usage') or {}
        with self.lock:
            self.prompt_tokens += int(usage.get('prompt_tokens', 0))
            self.completion_tokens += int(usage.get('completion_tokens', 0))
            self.calls += 1
        return data['choices'][0]['message']


class MockLLMClient:
    """Deterministic offline LLM responder for self-testing the 4-agent runner without API calls."""

    def __init__(self):
        self.prompt_tokens = 0
        self.completion_tokens = 0
        self.calls = 0
        self._step = {}
        self._lock = threading.Lock()

    def chat(self, messages, tools):
        sys_txt = messages[0]['content']
        player = 'host'
        for p in ('host', 'guest1', 'guest2', 'guest3'):
            if f'player="{p}"' in sys_txt:
                player = p
                break
        with self._lock:
            n = self._step.get(player, 0)
            self._step[player] = n + 1
            self.calls += 1
        if n == 0:
            tc = {'id': f'call_{player}_{n}', 'type': 'function',
                  'function': {'name': 'observe', 'arguments': json.dumps({'player': player})}}
        elif n == 1:
            tc = {'id': f'call_{player}_{n}', 'type': 'function',
                  'function': {'name': 'press_key', 'arguments': json.dumps({'player': player, 'key': 'f9'})}}
        elif n == 2 and player == 'guest1':
            tc = {'id': f'call_{player}_{n}', 'type': 'function',
                  'function': {'name': 'report_bug', 'arguments': json.dumps({
                      'player': player,
                      'title': 'Mock test bug from guest1',
                      'severity': 'P2',
                      'expected': 'Button click triggers action',
                      'actual': 'Mock verification bug report',
                      'steps': '1. Observe 2. Press F9',
                  })}}
        else:
            tc = {'id': f'call_{player}_{n}', 'type': 'function',
                  'function': {'name': 'observe', 'arguments': json.dumps({'player': player})}}
        return {'role': 'assistant', 'content': '', 'tool_calls': [tc]}


def trim_messages(messages, max_tail=14):
    """Keep system prompt + initial user prompt + last `max_tail` messages, never breaking a tool_call pair."""
    if len(messages) <= 2 + max_tail:
        return messages
    head = messages[:2]
    tail = messages[-max_tail:]
    # Drop leading orphan tool messages whose assistant tool_call was trimmed
    while tail and tail[0].get('role') == 'tool':
        tail.pop(0)
    return head + tail


QUOTA_ERROR_RE = re.compile(
    r'(?:\b429\b|rate[\s_-]*limit|usage[\s_-]*limit|5[\s-]*hour|quota|too many requests|'
    r'limit[\s_-]*exceeded|insufficient[\s_-]*(?:quota|balance|credits?)|'
    r'credits?[\s_-]*exhausted|try again in\b|resets? (?:in|at)\b|weekly[\s_-]*limit)',
    re.IGNORECASE,
)


def is_quota_error(text):
    return bool(text and QUOTA_ERROR_RE.search(text))


def parse_retry_after_seconds(text):
    """Extract an explicit reset/retry wait duration (seconds) from a provider quota message, or None."""
    if not text:
        return None
    # Match combined "Xh Ym" first (e.g. "resets in 2h 15m" or "2 hours 30 minutes")
    m_hm = re.search(r'(\d+)\s*(?:h|hr|hrs|hour|hours)\s*(?:and\s*)?(\d+)\s*(?:m|min|mins|minute|minutes)\b',
                     text, re.IGNORECASE)
    if m_hm:
        secs = int(m_hm.group(1)) * 3600 + int(m_hm.group(2)) * 60
        return max(60, min(21600, secs))
    m_h = re.search(r'(?:in|after|wait)\s+(\d+)\s*(?:h|hr|hrs|hour|hours)\b', text, re.IGNORECASE)
    if m_h:
        return max(60, min(21600, int(m_h.group(1)) * 3600))
    m_m = re.search(r'(?:in|after|wait)\s+(\d+)\s*(?:m|min|mins|minute|minutes)\b', text, re.IGNORECASE)
    if m_m:
        return max(60, min(21600, int(m_m.group(1)) * 60))
    m_s = re.search(r'(?:retry[- ]after[:\s]+|in\s+)(\d+)\s*(?:s|sec|secs|second|seconds)?\b', text, re.IGNORECASE)
    if m_s and int(m_s.group(1)) >= 10:
        return max(30, min(21600, int(m_s.group(1))))
    return None


class QuotaPauseController:
    """Coordinates pausing all 4 AI agents + pausing the live Spore host when LLM/OpenCode 5-hour quota runs out,
    periodically probes recovery, extends the playtest deadline so quota sleep does not burn play minutes,
    and cleanly resumes gameplay once limits recover."""

    def __init__(self, client, engine=None, probe_fn=None, probe_minutes=10.0, extend_deadline=True):
        self.client = client
        self.engine = engine
        self.probe_fn = probe_fn
        self.probe_minutes = max(0.05, float(probe_minutes))
        self.extend_deadline = bool(extend_deadline)
        self._lock = threading.Lock()
        self._resume_evt = threading.Event()
        self._resume_evt.set()
        self.paused = False
        self.pause_started_at = 0.0
        self.total_paused_s = 0.0
        self.pause_count = 0
        self.last_reason = ''
        self._we_paused_host = False

    def current_paused_total_s(self):
        with self._lock:
            cur = (time.time() - self.pause_started_at) if self.paused and self.pause_started_at else 0.0
            return self.total_paused_s + cur

    def wait_if_paused(self, stop_evt):
        while not stop_evt.is_set() and not self._resume_evt.is_set():
            self._resume_evt.wait(1.0)

    def _pause_game(self):
        if self.engine is not None:
            self.engine.external_pause_active = True
        self._we_paused_host = False
        try:
            obs = self.client.call_tool('observe', {'player': 'host'})
            if not obs.get('ok'):
                return
            if obs.get('stage') == 'editor' or obs.get('planner_open') or obs.get('pause_menu_open'):
                return
            if (obs.get('f9_window') or {}).get('open'):
                self.client.call_tool('press_key', {'player': 'host', 'key': 'esc'})
                time.sleep(0.5)
            self.client.call_tool('press_key', {'player': 'host', 'key': 'esc'})
            time.sleep(0.5)
            self._we_paused_host = True
            print('[QUOTA] Host Spore game paused (Pause Menu) while waiting for quota recovery.', flush=True)
        except Exception as e:
            print(f'[QUOTA] Warning: could not pause host game: {e}', file=sys.stderr, flush=True)

    def _resume_game(self):
        try:
            if self._we_paused_host:
                obs = self.client.call_tool('observe', {'player': 'host'})
                if obs.get('ok') and obs.get('pause_menu_open'):
                    res = self.client.call_tool('click_ui', {'player': 'host', 'caption': 'Resume Game'})
                    if not res.get('ok'):
                        self.client.call_tool('press_key', {'player': 'host', 'key': 'esc'})
                    time.sleep(0.5)
                self._we_paused_host = False
                print('[QUOTA] Host Spore game unpaused; resuming 4-player playtest.', flush=True)
        except Exception as e:
            print(f'[QUOTA] Warning: could not unpause host game: {e}', file=sys.stderr, flush=True)
        finally:
            if self.engine is not None:
                self.engine.external_pause_active = False

    def handle_quota_hit(self, player, reason_text, stop_evt):
        """Called by any agent thread when a 5-hour/rate limit error occurs. First caller manages pause+probing;
        all other threads wait cleanly until quota recovers or stop_evt is set."""
        with self._lock:
            if self.paused:
                manage = False
            else:
                self.paused = True
                self.pause_started_at = time.time()
                self.pause_count += 1
                self.last_reason = (reason_text or 'quota/rate limit').strip()[:220]
                self._resume_evt.clear()
                manage = True
        if not manage:
            self.wait_if_paused(stop_evt)
            return

        hint_s = parse_retry_after_seconds(reason_text)
        print(f'[QUOTA] Limit hit by {player}: {self.last_reason}', flush=True)
        self._pause_game()

        attempt = 0
        base_s = self.probe_minutes * 60.0
        try:
            while not stop_evt.is_set():
                attempt += 1
                if attempt == 1 and hint_s is not None:
                    wait_s = min(hint_s + 20.0, max(base_s * 1.5, 1800.0))
                elif attempt == 1:
                    wait_s = min(base_s, 300.0)
                else:
                    wait_s = min(base_s * (1.5 if attempt >= 3 else 1.0), 900.0)
                print(f'[QUOTA] Sleeping {wait_s / 60.0:.1f} min before recovery probe #{attempt}...', flush=True)
                # Sleep in short slices so Ctrl+C / stop_evt exits promptly
                wake_at = time.time() + wait_s
                while not stop_evt.is_set() and time.time() < wake_at:
                    stop_evt.wait(min(5.0, max(0.1, wake_at - time.time())))
                if stop_evt.is_set():
                    break
                if self.probe_fn is None:
                    break
                try:
                    recovered, detail = self.probe_fn()
                except Exception as e:
                    recovered, detail = False, str(e)
                if recovered:
                    elapsed_m = (time.time() - self.pause_started_at) / 60.0
                    print(f'[QUOTA] Quota recovered after {elapsed_m:.1f} min! ({detail})', flush=True)
                    break
                print(f'[QUOTA] Probe #{attempt} still limited: {detail[:160]}', flush=True)
                new_hint = parse_retry_after_seconds(detail)
                if new_hint is not None:
                    hint_s = new_hint
        finally:
            if not stop_evt.is_set():
                self._resume_game()
            elif self.engine is not None:
                self.engine.external_pause_active = False
            with self._lock:
                if self.paused and self.pause_started_at:
                    self.total_paused_s += time.time() - self.pause_started_at
                self.paused = False
                self.pause_started_at = 0.0
                self._resume_evt.set()


def make_opencode_probe(model):
    """Lightweight standalone probe checking whether OpenCode Go 5-hour quota has recovered."""
    def _probe():
        tmp = tempfile.mkdtemp(prefix='spore_oc_probe_')
        try:
            cmd = [
                'opencode', 'run', '--standalone', '--auto', '--archived',
                '--model', model,
                '--title', 'SporeCoop quota probe',
                'Reply with only OK',
            ]
            p = subprocess.run(cmd, cwd=tmp, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
            out = (p.stdout or '').strip()
            if p.returncode == 0 and not is_quota_error(out):
                return True, 'opencode probe ok'
            return False, (out.splitlines()[-1] if out else f'rc={p.returncode}')[:180]
        except Exception as e:
            return False, str(e)
        finally:
            try:
                import shutil
                shutil.rmtree(tmp, ignore_errors=True)
            except Exception:
                pass
    return _probe


def make_api_probe(llm):
    def _probe():
        try:
            llm.chat([{'role': 'user', 'content': 'Reply OK'}], [])
            return True, 'api probe ok'
        except Exception as e:
            return False, str(e)
    return _probe


def run_agent_loop(player, persona, client, llm, openai_tools, fixture, mode, stop_evt, max_steps, pace_s,
                   quota_ctrl=None):
    messages = [
        {'role': 'system', 'content': build_system_prompt(player, persona, fixture, mode)},
        {'role': 'user', 'content': f'Begin playtesting as "{player}". Start by calling observe(player="{player}").'},
    ]
    steps = 0
    while not stop_evt.is_set() and (max_steps <= 0 or steps < max_steps):
        if quota_ctrl is not None:
            quota_ctrl.wait_if_paused(stop_evt)
            if stop_evt.is_set():
                break
        steps += 1
        messages = trim_messages(messages, max_tail=16)
        try:
            msg = llm.chat(messages, openai_tools)
        except urllib.error.HTTPError as e:
            body = ''
            try:
                body = e.read().decode('utf-8', 'replace')[:300]
            except Exception:
                pass
            print(f'[{player}] LLM HTTP {e.code}: {body}', file=sys.stderr, flush=True)
            if quota_ctrl is not None and (e.code == 429 or is_quota_error(body)):
                quota_ctrl.handle_quota_hit(player, f'HTTP {e.code}: {body}', stop_evt)
                steps -= 1
                continue
            time.sleep(5.0)
            continue
        except Exception as e:
            print(f'[{player}] LLM error: {e}', file=sys.stderr, flush=True)
            if quota_ctrl is not None and is_quota_error(str(e)):
                quota_ctrl.handle_quota_hit(player, str(e), stop_evt)
                steps -= 1
                continue
            time.sleep(3.0)
            continue

        messages.append(msg)
        tcalls = msg.get('tool_calls') or []
        if not tcalls:
            messages.append({
                'role': 'user',
                'content': f'Continue playing as "{player}": take a concrete tool action or call observe.',
            })
            time.sleep(pace_s)
            continue

        for tc in tcalls:
            fn = tc.get('function') or {}
            tname = fn.get('name', '')
            try:
                targs = json.loads(fn.get('arguments') or '{}')
            except json.JSONDecodeError:
                targs = {}
            # Enforce player ownership so one agent cannot accidentally hijack another slot
            if tname in PLAYER_TOOL_NAMES and tname != 'session_status':
                targs['player'] = player
            try:
                res = client.call_tool(tname, targs)
            except Exception as e:
                res = {'ok': False, 'error': str(e)}
            res_str = json.dumps(res, ensure_ascii=False)
            if len(res_str) > 6000:
                res_str = res_str[:6000] + '...(truncated)'
            print(f'[{player}] step {steps}: {tname}({json.dumps(targs, ensure_ascii=False)[:90]}) -> ok={res.get("ok")}',
                  flush=True)
            messages.append({
                'role': 'tool',
                'tool_call_id': tc.get('id', f'tc_{steps}'),
                'content': res_str,
            })
        time.sleep(pace_s)


def run_oracle_loop(client, stop_evt, interval_s=5.0):
    while not stop_evt.is_set():
        try:
            res = client.call_tool('check_anomalies', {})
            for a in (res.get('new_anomalies') or []):
                print(f'[ORACLE][{a.get("severity")}] {a.get("player")}: {a.get("summary")}', flush=True)
        except Exception as e:
            print(f'[ORACLE] error: {e}', file=sys.stderr, flush=True)
        stop_evt.wait(interval_s)


def run_opencode_agent_loop(player, persona, model, work_dir, run_dir, fixture, mode, stop_evt, max_rounds,
                            round_timeout_s, focus='gameplay', stage_getter=None, round_tracker=None,
                            quota_ctrl=None):
    """Run one autonomous player via `opencode run --standalone --auto --archived` connected to sporecoop MCP."""
    log_path = os.path.join(run_dir, f'agent_{player}.log')
    mem_path = os.path.join(run_dir, f'memory_{player}.txt')
    shots_dir = os.path.join(run_dir, 'shots')
    memory_entries = []
    rounds = 0
    while not stop_evt.is_set() and (max_rounds <= 0 or rounds < max_rounds):
        if quota_ctrl is not None:
            quota_ctrl.wait_if_paused(stop_evt)
            if stop_evt.is_set():
                break
        rounds += 1
        live_stage = stage_getter(player) if stage_getter else _stage_from_fixture(fixture)
        if live_stage == 'offline':
            print(f'[{player}] instance offline before round {rounds}; pausing 5s...', flush=True)
            stop_evt.wait(5.0)
            continue
        playbook = STAGE_PLAYBOOKS.get(live_stage, STAGE_PLAYBOOKS.get(_stage_from_fixture(fixture), ''))
        mem_block = ''
        if memory_entries:
            mem_block = (
                '\nYOUR RECENT MEMORY (continue your campaign from here; do NOT redo finished setup):\n'
                + '\n'.join(f'- {m}' for m in memory_entries[-6:])
                + '\n'
            )
        if focus == 'gameplay':
            f9_rule = (
                'Round 1 ONLY: if you are a guest in Civ, you may quickly pick starting specialty in F9 -> Rivals, '
                'or declare 1 war/ally, then IMMEDIATELY close F9 (`press_key("esc")`) and play the 3D world!'
                if rounds == 1 else
                'KEEP F9 CLOSED this round unless you are checking a stage-victory/draft/proposal event! '
                'Spend 85%+ of your actions playing the real 3D Spore world (moving, building, buying units, fighting, allying, editors).'
            )
            call_budget = '8 to 12'
            gameplay_block = (
                f'\n{playbook}\n\n'
                'GAMEPLAY EXECUTION RULES:\n'
                f'- {f9_rule}\n'
                '- Use `perform_actions` to execute 4..10 real clicks/keys/HUD presses/waits in ONE tool call (human speed!).\n'
                '- Call `screenshot` and inspect the returned PNG with the `Read` tool whenever you need to click '
                'icon-only Spore controls (planner building palette, building slots, tool wheel, galaxy stars, editor parts, advance button).\n'
                '- If `observe().pause_menu_open` is true, immediately click `Resume Game` or press `esc` to unpause.\n'
                '- If `observe().planner_open` is true, place/modify buildings then close via `click_hud("planner_done")`.\n'
            )
        else:
            call_budget = '6 to 8'
            gameplay_block = ''

        prompt = (
            f'You are autonomous QA playtester "{player}" ({persona["title"]}) in a LIVE multiplayer '
            f'SporeCoop game (fixture={fixture}, live_stage={live_stage}, mode={mode}, round={rounds}).\n'
            f'Role goal: {persona["goal"]}\n'
            f'{mem_block}'
            f'{gameplay_block}\n'
            'STRICT TOOL RULES:\n'
            f'- Use ONLY `sporecoop` MCP tools with player="{player}", PLUS you may use `Read` ONLY on PNG '
            f'screenshots inside `{shots_dir}` returned by `screenshot` so you can visually see the game.\n'
            '- NEVER use bash, edit, write, grep, or read any source files.\n'
            '- Do NOT call session_start, session_stop, or session_attach.\n'
            f'- Execute a focused gameplay batch of AT MOST {call_budget} tool calls this round, then STOP:\n'
            '  1. Call `observe` first to see stage, resources, hud_buttons, visible_dialogs, and screen_targets.\n'
            '  2. Play actively using `perform_actions`, `click_hud`, `click_target`, `click_ui`, `click_screen`, '
            '`drag_screen`, `press_key`, `pan_camera`, `screenshot` (and `Read` on your PNG to aim at icons/world).\n'
            '  3. Call `observe` (or `screenshot`) after your action sequence to verify real game state changed.\n'
            '  4. If ANY real gameplay action fails unexpectedly (unit ignores order, planner/editor broken, vehicle/hut '
            'cannot be bought, money/food/progress desyncs, modal stuck, UI broken), IMMEDIATELY call `report_bug` '
            'with exact expected vs actual BEFORE finishing.\n'
            '  5. STOP and output a concise 2-line summary:\n'
            '     DONE: <what you built/fought/explored/verified in this round>\n'
            '     NEXT: <your concrete gameplay plan for the next round>'
        )
        cmd = [
            'opencode', 'run', '--standalone', '--auto', '--archived',
            '--model', model,
            '--title', f'SporeCoop QA {player} r{rounds} ({live_stage})',
            prompt,
        ]
        print(f'[{player}] launching OpenCode round {rounds} ({model}, stage={live_stage})...', flush=True)
        proc = None
        try:
            proc = subprocess.Popen(
                cmd, cwd=work_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
            )
            t_start = time.time()
            chunks = []
            while proc.poll() is None:
                if stop_evt.is_set() or (time.time() - t_start) > round_timeout_s:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                    break
                time.sleep(0.5)
            out_txt = ''
            if proc.stdout:
                try:
                    out_txt = proc.stdout.read()
                except Exception:
                    pass
            chunks.append(out_txt)
            with open(log_path, 'a', encoding='utf-8') as f:
                f.write(f'\n=== Round {rounds} ({model}, stage={live_stage}, rc={proc.returncode}, {time.time() - t_start:.1f}s) ===\n')
                f.write(''.join(chunks))
            if quota_ctrl is not None and not stop_evt.is_set() and is_quota_error(out_txt):
                # Do not count a quota-aborted round against max_rounds or pollute gameplay memory
                rounds -= 1
                err_tail = ' | '.join([l.strip() for l in out_txt.splitlines() if l.strip()][-3:])
                quota_ctrl.handle_quota_hit(player, err_tail or out_txt[:220], stop_evt)
                continue
            last_lines = [l.strip() for l in out_txt.splitlines() if l.strip()][-3:]
            summary_note = ' | '.join(last_lines)[:260]
            if summary_note:
                entry = f'r{rounds} [{live_stage}]: {summary_note}'
                memory_entries.append(entry)
                with open(mem_path, 'a', encoding='utf-8') as mf:
                    mf.write(entry + '\n')
            if round_tracker is not None:
                round_tracker(player, rounds, live_stage)
            print(f'[{player}] round {rounds} done (rc={proc.returncode}, stage={live_stage}, {time.time() - t_start:.1f}s): {summary_note[:180]}',
                  flush=True)
        except Exception as e:
            print(f'[{player}] OpenCode error: {e}', file=sys.stderr, flush=True)
            if proc and proc.poll() is None:
                proc.kill()
            time.sleep(3.0)
        if not stop_evt.is_set():
            stop_evt.wait(1.0)


def write_markdown_report(run_dir, status, anomalies, bugs, actions, llm, elapsed_s,
                          total_actions=None, rounds_info=None, stages_seen=None, quota_ctrl=None):
    os.makedirs(run_dir, exist_ok=True)
    path = os.path.join(run_dir, 'report.md')
    act_total = total_actions if total_actions is not None else len(actions)
    lines = [
        '# SporeCoop AI Playtest Report',
        '',
        f'- Generated: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}',
        f'- Fixture: `{status.get("fixture") or "attached"}` ({status.get("mode_preset") or "-"})',
        f'- Duration: {elapsed_s:.1f} s ({elapsed_s / 60.0:.1f} min)',
        f'- Players: {len(status.get("players") or [])}',
        f'- Total real player actions executed: **{act_total}**',
        f'- LLM calls (API backend): {llm.calls} (prompt tokens: {llm.prompt_tokens}, completion tokens: {llm.completion_tokens})',
        f'- Engine anomalies detected by Oracle: **{len(anomalies)}**',
        f'- Gameplay/UX bugs reported by AI agents: **{len(bugs)}**',
    ]
    if rounds_info:
        r_str = ', '.join(f'{k}={v}' for k, v in sorted(rounds_info.items()))
        lines.append(f'- Rounds completed: {r_str}')
    if stages_seen:
        lines.append(f'- Stages reached: {", ".join(sorted(stages_seen))}')
    if quota_ctrl is not None and (quota_ctrl.pause_count > 0 or quota_ctrl.paused):
        state_lbl = 'PAUSED (waiting for quota reset)' if quota_ctrl.paused else 'active'
        lines.append(
            f'- Quota pauses: {quota_ctrl.pause_count} (total paused {quota_ctrl.current_paused_total_s() / 60.0:.1f} min, status: {state_lbl})'
        )
    lines.extend(['', '## Confirmed Engine Anomalies (Oracle)', ''])
    if not anomalies:
        lines.append('No engine crashes, faults, freezes, or stuck pauses detected.')
    else:
        for i, a in enumerate(anomalies, 1):
            lines.append(f'### {i}. [{a.get("severity", "P1")}] {a.get("kind")} ({a.get("player")})')
            lines.append(f'- Summary: {a.get("summary")}')
            if a.get('detail'):
                lines.append(f'- Detail:\n```\n{a["detail"]}\n```')
            if a.get('bundle_dir'):
                lines.append(f'- Evidence bundle: `{a["bundle_dir"]}`')
            lines.append('')

    lines.extend(['', '## Player Bug Reports (AI Agents)', ''])
    if not bugs:
        lines.append('No gameplay or UI bugs filed by agents.')
    else:
        for b in bugs:
            lines.append(f'### {b.get("id")} [{b.get("severity")}] {b.get("title")} (`{b.get("player")}`)')
            lines.append(f'- Expected: {b.get("expected")}')
            lines.append(f'- Actual: {b.get("actual")}')
            if b.get('steps'):
                lines.append(f'- Steps: {b.get("steps")}')
            if b.get('bundle_dir'):
                lines.append(f'- Evidence bundle: `{b["bundle_dir"]}`')
            lines.append('')

    lines.extend(['', '## Recent Action Timeline (last 50)', ''])
    for act in list(actions)[-50:]:
        lines.append(f'- `{act["ts"]}` **{act["player"]}** `{act["tool"]}`: {act["detail"]}')

    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    return path


def main(argv=None):
    ap = argparse.ArgumentParser(description='Run 2..4 parallel DeepSeek playtesters against SporeCoop via MCP')
    ap.add_argument('--players', type=int, default=4, choices=[2, 3, 4], help='Number of players (default 4)')
    ap.add_argument('--fixture', default='tribe', help='Fixture save: cell, creature, tribe, civ_npc, space_free')
    ap.add_argument('--mode', default='rivals', choices=['rivals', 'together'], help='Rivals or Together preset')
    ap.add_argument('--focus', default='gameplay', choices=['gameplay', 'balanced', 'ui'],
                    help='Testing focus: gameplay (85%+ real 3D play + editors + batching), balanced, or ui')
    ap.add_argument('--minutes', type=float, default=15.0, help='Playtest duration in minutes (default 15; use 240-360 for overnight)')
    ap.add_argument('--steps', type=int, default=0, help='Max rounds/turns per agent (0 = time-limited only)')
    ap.add_argument('--pace', type=float, default=1.5, help='Pause in seconds between turns per agent')
    ap.add_argument('--lane', type=int, default=None, help='SPORE_LANE (1..10)')
    ap.add_argument('--attach', default='', help='Attach to running instances instead of launching, e.g. host=1,guest1=2')
    ap.add_argument('--mcp-url', default='', help='Connect to an external `spore_mcp.py --http` server URL')
    ap.add_argument('--backend', default='api', choices=['api', 'opencode'],
                    help='LLM backend: "api" (direct DeepSeek HTTP API) or "opencode" (OpenCode Go CLI agents)')
    ap.add_argument('--base-url', default=os.environ.get('DEEPSEEK_BASE_URL', 'https://api.deepseek.com/v1'))
    ap.add_argument('--model', default=os.environ.get('DEEPSEEK_MODEL', 'deepseek-chat'),
                    help='Model for --backend api, or override all OpenCode agents (leave empty/default for per-role Go models)')
    ap.add_argument('--round-timeout', type=float, default=150.0,
                    help='Per-round timeout in seconds for --backend opencode (default 150)')
    ap.add_argument('--quota-probe-minutes', type=float, default=10.0,
                    help='Minutes between recovery probes when a 5-hour/rate limit is hit (default 10; first probe <=5 min)')
    ap.add_argument('--no-extend-on-quota', action='store_true',
                    help='Do NOT extend --minutes deadline while paused for quota recovery')
    ap.add_argument('--api-key', default=os.environ.get('DEEPSEEK_API_KEY', ''))
    ap.add_argument('--mock', action='store_true', help='Use offline MockLLMClient (no API key required)')
    ap.add_argument('--keep-open', action='store_true', help='Do not kill launched game instances on exit')
    args = ap.parse_args(argv)

    if args.backend == 'api' and not args.mock and not args.api_key:
        print('Error: set DEEPSEEK_API_KEY env var (or use --backend opencode / --mock)',
              file=sys.stderr)
        return 2

    llm = MockLLMClient() if (args.mock or args.backend == 'opencode') else DeepSeekClient(
        args.api_key, base_url=args.base_url, model=args.model
    )

    engine = None
    http_srv = None
    launched = False
    mcp_url = args.mcp_url
    if mcp_url:
        client = RemoteMcpClient(mcp_url)
        status = client.call_tool('session_status', {})
        if not status.get('players'):
            if args.attach:
                status = client.call_tool('session_attach', {'instances': spore_mcp.parse_attach_arg(args.attach)})
            else:
                status = client.call_tool('session_start', {
                    'fixture': args.fixture, 'players': args.players, 'mode': args.mode, 'lane': args.lane,
                })
                launched = True
    else:
        engine = spore_mcp.SporeMcpEngine(default_lane=args.lane)
        client = engine
        if args.attach:
            status = engine.session_attach(spore_mcp.parse_attach_arg(args.attach))
        else:
            status = engine.session_start(fixture=args.fixture, players=args.players, mode=args.mode, lane_num=args.lane)
            launched = True
        if args.backend == 'opencode':
            engine.session_locked = True
            http_srv = spore_mcp.make_http_server(engine, '127.0.0.1', 0)
            port = http_srv.server_address[1]
            threading.Thread(target=http_srv.serve_forever, daemon=True).start()
            mcp_url = f'http://127.0.0.1:{port}/mcp'
            print(f'[playtest] internal MCP HTTP server listening at {mcp_url}', flush=True)

    active_players = [p['player'] for p in (status.get('players') or [])]
    if not active_players:
        print('Error: no active players in session:', status, file=sys.stderr)
        return 1

    run_dir = status.get('run_dir') or os.path.join(spore_mcp.ROOT, 'out', 'ai-playtest', spore_mcp._utc_stamp())
    os.makedirs(run_dir, exist_ok=True)
    stop_evt = threading.Event()
    t0 = time.time()
    deadline = t0 + max(5.0, args.minutes * 60.0)

    oracle_t = threading.Thread(target=run_oracle_loop, args=(client, stop_evt, 5.0), daemon=True)
    oracle_t.start()

    persona_map = GAMEPLAY_PERSONAS if args.focus == 'gameplay' else PERSONAS
    progress_lock = threading.Lock()
    rounds_info = {}
    stages_seen = {_stage_from_fixture(args.fixture)}

    def track_round(pl, rnum, stg):
        with progress_lock:
            rounds_info[pl] = rnum
            if stg and stg not in ('unknown', 'offline'):
                stages_seen.add(stg)

    def get_stage(pl):
        if engine is not None:
            return engine.get_player_stage(pl)
        try:
            st = client.call_tool('session_status', {})
            for entry in (st.get('players') or []):
                if entry.get('player') == pl:
                    return entry.get('stage') or _stage_from_fixture(args.fixture)
        except Exception:
            pass
        return _stage_from_fixture(args.fixture)

    override_model = args.model if (args.model and args.model != 'deepseek-chat') else ''
    probe_model = override_model or OPENCODE_MODELS.get('host', 'opencode-go/deepseek-v4.1-flash')
    probe_fn = None if args.mock else (make_opencode_probe(probe_model) if args.backend == 'opencode' else make_api_probe(llm))
    quota_ctrl = QuotaPauseController(
        client, engine=engine, probe_fn=probe_fn,
        probe_minutes=args.quota_probe_minutes,
        extend_deadline=not args.no_extend_on_quota,
    )

    threads = []
    if args.backend == 'opencode':
        oc_work = os.path.join(run_dir, 'oc-work')
        os.makedirs(oc_work, exist_ok=True)
        oc_cfg = {
            'mcp': {
                'servers': {
                    'sporecoop': {
                        'type': 'remote',
                        'url': mcp_url,
                    }
                }
            }
        }
        with open(os.path.join(oc_work, 'opencode.json'), 'w', encoding='utf-8') as f:
            json.dump(oc_cfg, f, indent=2)
        for p in active_players:
            persona = persona_map.get(p, persona_map['guest1'])
            model = override_model or OPENCODE_MODELS.get(p, 'opencode-go/deepseek-v4.1-flash')
            th = threading.Thread(
                target=run_opencode_agent_loop,
                args=(p, persona, model, oc_work, run_dir, args.fixture, args.mode, stop_evt,
                      args.steps, args.round_timeout, args.focus, get_stage, track_round, quota_ctrl),
                daemon=True,
            )
            th.start()
            threads.append(th)
    else:
        openai_tools = mcp_tools_to_openai(client.tool_definitions())
        for p in active_players:
            persona = persona_map.get(p, persona_map['guest1'])
            th = threading.Thread(
                target=run_agent_loop,
                args=(p, persona, client, llm, openai_tools, args.fixture, args.mode, stop_evt,
                      args.steps, args.pace, quota_ctrl),
                daemon=True,
            )
            th.start()
            threads.append(th)

    last_ckpt = t0
    play_budget_s = max(5.0, args.minutes * 60.0)
    try:
        while any(th.is_alive() for th in threads):
            now = time.time()
            active_elapsed = (now - t0) - (quota_ctrl.current_paused_total_s() if quota_ctrl.extend_deadline else 0.0)
            if args.steps <= 0 and active_elapsed >= play_budget_s:
                stop_evt.set()
                break
            # Periodic live report checkpoint every 60s so overnight runs always have an up-to-date report.md
            if engine is not None and (now - last_ckpt) >= 60.0:
                last_ckpt = now
                try:
                    with progress_lock:
                        ri = dict(rounds_info)
                        ss = set(stages_seen)
                    write_markdown_report(
                        run_dir, engine.session_status(), list(engine.anomalies), list(engine.bugs),
                        list(engine.actions), llm, now - t0,
                        total_actions=engine.total_actions, rounds_info=ri, stages_seen=ss,
                        quota_ctrl=quota_ctrl,
                    )
                except Exception:
                    pass
            time.sleep(0.5)
    except KeyboardInterrupt:
        print('\nStopping playtest (Ctrl+C)...', flush=True)
        stop_evt.set()

    stop_evt.set()
    for th in threads:
        th.join(timeout=15.0)
    oracle_t.join(timeout=5.0)

    if engine is not None:
        engine.session_locked = False
    final_status = client.call_tool('session_status', {})
    anomalies = engine.anomalies if engine is not None else []
    bugs = engine.bugs if engine is not None else []
    actions = engine.actions if engine is not None else []
    total_acts = engine.total_actions if engine is not None else len(actions)
    with progress_lock:
        ri = dict(rounds_info)
        ss = set(stages_seen)
    report_path = write_markdown_report(
        run_dir, final_status, anomalies, bugs, actions, llm, time.time() - t0,
        total_actions=total_acts, rounds_info=ri, stages_seen=ss, quota_ctrl=quota_ctrl,
    )
    print(f'\nPlaytest finished. Report written to: {report_path}', flush=True)

    if http_srv is not None:
        http_srv.shutdown()
        http_srv.server_close()
    if launched and not args.keep_open:
        client.call_tool('session_stop', {})
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
