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

  # Launch 4 players in Tribe Rivals for 15 minutes on lane 2:
  SPORE_LANE=2 python3 tools/ai_playtest.py --players 4 --fixture tribe --mode rivals --minutes 15

  # Launch 4 players in Civ Rivals for 20 minutes:
  SPORE_LANE=2 python3 tools/ai_playtest.py --players 4 --fixture civ_npc --mode rivals --minutes 20

  # Connect to an already-running MCP HTTP server:
  python3 tools/ai_playtest.py --mcp-url http://127.0.0.1:8765/mcp --minutes 10

  # Offline dry-run with mock LLM (verifies full 4-agent pipeline with zero API calls):
  python3 tools/ai_playtest.py --mock --players 4 --steps 4
"""
import argparse
import datetime
import json
import os
import subprocess
import sys
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

# Only player-facing tools are exposed to DeepSeek agents (never session_start/stop or cheats)
PLAYER_TOOL_NAMES = {
    'observe', 'click_ui', 'click_f9_row', 'click_target', 'click_screen',
    'drag_screen', 'press_key', 'type_text', 'pan_camera', 'reconnect_player',
    'screenshot', 'report_bug',
}


def build_system_prompt(player, persona, fixture, mode):
    return (
        f'You are an autonomous QA playtester controlling player "{player}" ({persona["title"]}) '
        f'in a live multiplayer SporeCoop game (fixture={fixture}, mode={mode}).\n\n'
        f'Your persona & goals:\n{persona["goal"]}\n\n'
        'Rules:\n'
        '1. ALWAYS call `observe` first and after significant actions so you know the real game state.\n'
        '2. Play ONLY through real player inputs (`click_ui`, `click_f9_row`, `click_target`, '
        '`click_screen`, `drag_screen`, `press_key`, `type_text`, `pan_camera`, `reconnect_player`).\n'
        '3. Always pass player="' + player + '" to every tool call.\n'
        '4. Watch carefully for player-facing bugs: buttons that do nothing, stuck modal dialogs, '
        'broken guest features (instruments, specialization choice, building sell, planner spend, '
        'diplomacy, progress bar not updating, missing units/cities), or UI glitches.\n'
        '5. Whenever an action clearly fails to do what a real Spore player expects, IMMEDIATELY call '
        '`report_bug` with clear expected vs actual behavior and steps, then keep testing other flows.\n'
        '6. Keep replies very brief (1 sentence max) and focus on taking concrete tool actions.'
    )


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


def run_agent_loop(player, persona, client, llm, openai_tools, fixture, mode, stop_evt, max_steps, pace_s):
    messages = [
        {'role': 'system', 'content': build_system_prompt(player, persona, fixture, mode)},
        {'role': 'user', 'content': f'Begin playtesting as "{player}". Start by calling observe(player="{player}").'},
    ]
    steps = 0
    while not stop_evt.is_set() and (max_steps <= 0 or steps < max_steps):
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
            time.sleep(5.0)
            continue
        except Exception as e:
            print(f'[{player}] LLM error: {e}', file=sys.stderr, flush=True)
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


def run_opencode_agent_loop(player, persona, model, work_dir, run_dir, fixture, mode, stop_evt, max_rounds, round_timeout_s):
    """Run one autonomous player via `opencode run --standalone --auto --archived` connected to sporecoop MCP."""
    log_path = os.path.join(run_dir, f'agent_{player}.log')
    rounds = 0
    while not stop_evt.is_set() and (max_rounds <= 0 or rounds < max_rounds):
        rounds += 1
        prompt = (
            f'You are autonomous QA playtester "{player}" ({persona["title"]}) in a LIVE multiplayer '
            f'SporeCoop game (fixture={fixture}, mode={mode}, round={rounds}).\n'
            f'Role goal: {persona["goal"]}\n\n'
            'STRICT RULES:\n'
            f'- Use ONLY the `sporecoop` MCP tools with player="{player}". NEVER use bash, file tools, or edit files.\n'
            '- Do NOT call session_start, session_stop, or session_attach.\n'
            '- Execute a focused batch of AT MOST 6 to 8 tool calls this round, then STOP and finish:\n'
            '  1. Call `observe` first to see stage, resources, F9 UI, visible dialogs, and screen_targets.\n'
            '  2. Interact like a real player (`press_key`, `click_ui`, `click_f9_row`, `click_target`, '
            '`click_screen`, `drag_screen`, `pan_camera`, `type_text`, `reconnect_player`, `screenshot`).\n'
            '  3. Call `observe` (or `screenshot` + inspect it) after actions to verify real game response.\n'
            '  4. If ANY action fails unexpectedly, a button wraps/overlaps or does nothing, a modal gets stuck, '
            'guest tools/UI break, or progress/resources/HUD show wrong values, IMMEDIATELY call `report_bug` '
            'with exact expected vs actual BEFORE finishing.\n'
            '  5. STOP after at most 8 tool calls and output a 2-line summary of what you tested and found.'
        )
        cmd = [
            'opencode', 'run', '--standalone', '--auto', '--archived',
            '--model', model,
            '--title', f'SporeCoop QA {player} r{rounds}',
            prompt,
        ]
        print(f'[{player}] launching OpenCode round {rounds} ({model})...', flush=True)
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
                f.write(f'\n=== Round {rounds} ({model}, rc={proc.returncode}, {time.time() - t_start:.1f}s) ===\n')
                f.write(''.join(chunks))
            last_lines = [l.strip() for l in out_txt.splitlines() if l.strip()][-2:]
            preview = ' | '.join(last_lines)[:180]
            print(f'[{player}] round {rounds} done (rc={proc.returncode}, {time.time() - t_start:.1f}s): {preview}',
                  flush=True)
        except Exception as e:
            print(f'[{player}] OpenCode error: {e}', file=sys.stderr, flush=True)
            if proc and proc.poll() is None:
                proc.kill()
            time.sleep(3.0)
        if not stop_evt.is_set():
            stop_evt.wait(1.0)


def write_markdown_report(run_dir, status, anomalies, bugs, actions, llm, elapsed_s):
    os.makedirs(run_dir, exist_ok=True)
    path = os.path.join(run_dir, 'report.md')
    lines = [
        '# SporeCoop AI Playtest Report',
        '',
        f'- Generated: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}',
        f'- Fixture: `{status.get("fixture") or "attached"}` ({status.get("mode_preset") or "-"})',
        f'- Duration: {elapsed_s:.1f} s',
        f'- Players: {len(status.get("players") or [])}',
        f'- LLM calls: {llm.calls} (prompt tokens: {llm.prompt_tokens}, completion tokens: {llm.completion_tokens})',
        f'- Engine anomalies detected by Oracle: **{len(anomalies)}**',
        f'- Gameplay/UX bugs reported by AI agents: **{len(bugs)}**',
        '',
        '## Confirmed Engine Anomalies (Oracle)',
        '',
    ]
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
    ap.add_argument('--minutes', type=float, default=15.0, help='Playtest duration in minutes (default 15)')
    ap.add_argument('--steps', type=int, default=0, help='Max turns per agent (0 = time-limited only)')
    ap.add_argument('--pace', type=float, default=1.5, help='Pause in seconds between turns per agent')
    ap.add_argument('--lane', type=int, default=None, help='SPORE_LANE (1..10)')
    ap.add_argument('--attach', default='', help='Attach to running instances instead of launching, e.g. host=1,guest1=2')
    ap.add_argument('--mcp-url', default='', help='Connect to an external `spore_mcp.py --http` server URL')
    ap.add_argument('--backend', default='api', choices=['api', 'opencode'],
                    help='LLM backend: "api" (direct DeepSeek HTTP API) or "opencode" (OpenCode Go CLI agents)')
    ap.add_argument('--base-url', default=os.environ.get('DEEPSEEK_BASE_URL', 'https://api.deepseek.com/v1'))
    ap.add_argument('--model', default=os.environ.get('DEEPSEEK_MODEL', 'deepseek-chat'),
                    help='Model for --backend api, or override all OpenCode agents (leave empty/default for per-role Go models)')
    ap.add_argument('--round-timeout', type=float, default=120.0,
                    help='Per-round timeout in seconds for --backend opencode (default 120)')
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
        override_model = args.model if (args.model and args.model != 'deepseek-chat') else ''
        for p in active_players:
            persona = PERSONAS.get(p, PERSONAS['guest1'])
            model = override_model or OPENCODE_MODELS.get(p, 'opencode-go/deepseek-v4.1-flash')
            th = threading.Thread(
                target=run_opencode_agent_loop,
                args=(p, persona, model, oc_work, run_dir, args.fixture, args.mode, stop_evt,
                      args.steps, args.round_timeout),
                daemon=True,
            )
            th.start()
            threads.append(th)
    else:
        openai_tools = mcp_tools_to_openai(client.tool_definitions())
        for p in active_players:
            persona = PERSONAS.get(p, PERSONAS['guest1'])
            th = threading.Thread(
                target=run_agent_loop,
                args=(p, persona, client, llm, openai_tools, args.fixture, args.mode, stop_evt, args.steps, args.pace),
                daemon=True,
            )
            th.start()
            threads.append(th)

    try:
        while any(th.is_alive() for th in threads):
            if args.steps <= 0 and time.time() >= deadline:
                stop_evt.set()
                break
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
    report_path = write_markdown_report(run_dir, final_status, anomalies, bugs, actions, llm, time.time() - t0)
    print(f'\nPlaytest finished. Report written to: {report_path}', flush=True)

    if http_srv is not None:
        http_srv.shutdown()
        http_srv.server_close()
    if launched and not args.keep_open:
        client.call_tool('session_stop', {})
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
