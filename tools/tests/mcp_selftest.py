#!/usr/bin/env python3
"""Self-test for SporeCoop MCP server (stdio + Streamable HTTP) and 4-agent DeepSeek playtest runner.

Runs completely offline using simulated SporeKit consoles (no Wine or API keys required).
"""
import io
import json
import os
import shutil
import sys
import tempfile
import threading
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
if TOOLS not in sys.path:
    sys.path.insert(0, TOOLS)

import ai_playtest  # noqa: E402
import spore_mcp    # noqa: E402


class FakeConsole:
    """Deterministic in-memory fake SporeKit console for MCP & playtest self-tests."""

    def __init__(self, inst, stage='Civ', faults=0):
        self.inst = inst
        self.stage = stage
        self.faults = faults
        self.frame = 100
        self.f9_open = False
        self.cmds = []

    def call(self, line):
        self.cmds.append(line)
        self.frame += 1
        if line == 'ping':
            return {'ok': True, 'out': 'pong'}
        if line == 'game.mode':
            return {'ok': True, 'out': self.stage}
        if line == 'frame':
            return {'ok': True, 'out': f'frame={self.frame} fps=60'}
        if line == 'exc':
            return {'ok': True, 'out': f'total faults: {self.faults}'}
        if line == 'coop.pauses':
            return {'ok': True, 'out': 'none'}
        if line == 'coop.status':
            return {'ok': True, 'out': f'client id={self.inst} connected players=4'}
        if line == 'coop.avatar':
            return {'ok': True, 'out': 'pos=(10.0 20.0 30.0) hp=100'}
        if line == 'coop.res':
            return {'ok': True, 'out': 'money=3000 food=50'}
        if line == 'coop.rivals':
            return {'ok': True, 'out': 'preset=AllRivals stage=Civ'}
        if line == 'coop.civ':
            return {'ok': True, 'out': 'city 0 0x1000 pol=2205 (player) owner=1 buildings=3 pos=(1 2 3) capture=0/0 convert=0 spec=2'}
        if line == 'coop.civ rivals':
            return {'ok': True, 'out': 'civ_rivals: fix=1 active=1 assigned=4 spec_picked=0'}
        if line == 'coop.uistate':
            if not self.f9_open:
                return {'ok': True, 'out': 'open=0 modal=0'}
            return {
                'ok': True,
                'out': '\n'.join([
                    'open=1 page=rivals modal=0',
                    "status 'Ready'",
                    "chat 'Alice: hi'",
                    'rpage first=0 rows=12',
                    "rrow 0 '  Military (tanks / conquer)' 'Choose'",
                    "rrow 1 '  Alice: Neutral' 'Declare war'",
                    "rbtn0 550 120 710 144 1 'Choose'",
                    "rbtn1 550 150 710 174 1 'Declare war'",
                    "rnext 650 548 720 588 1 '>'",
                ]),
            }
        if line == 'coop.targets objs':
            return {'ok': True, 'out': 'member 1 0 320 240\nobj hut1 12.5 510 300'}
        if line.startswith('coop.ui where '):
            return {
                'ok': True,
                'out': '\n'.join([
                    '062D5618 120 960 180 1020',
                    '062D6902 220 960 280 1020',
                ]),
            }
        if line.startswith('coop.ui find '):
            if '"OK"' in line:
                return {'ok': True, 'out': '- 0x1 id=0x1 cmd=0 fl=0x1 [500 300 600 340]'}
            if '"Done"' in line or '"Save"' in line:
                return {'ok': True, 'out': '- 0x2 id=0x2 cmd=0 fl=0x1 [350 400 420 430]'}
            return {'ok': True, 'out': 'no window'}
        if line.startswith('key f9'):
            self.f9_open = not self.f9_open
            return {'ok': True, 'out': 'ok'}
        if line.startswith(('move ', 'click ', 'drag ', 'key ', 'text ', 'wheel ', 'coop.say ', 'coop.bug ')):
            return {'ok': True, 'out': 'ok'}
        if line.startswith('shot '):
            path = line.split(' ', 1)[1].strip()
            with open(path, 'wb') as f:
                f.write(b'\x89PNG\r\n\x1a\nfake')
            return {'ok': True, 'out': 'saved'}
        if line.startswith('log '):
            return {'ok': True, 'out': '[INF] coop: ok'}
        return {'ok': True, 'out': ''}


def test_mcp_protocol_and_tools():
    tmp = tempfile.mkdtemp(prefix='spore_mcp_test_')
    try:
        eng = spore_mcp.SporeMcpEngine(out_root=tmp)
        fakes = {
            'host': FakeConsole(1, stage='Civ'),
            'guest1': FakeConsole(2, stage='Civ'),
            'guest2': FakeConsole(21, stage='Civ'),
            'guest3': FakeConsole(31, stage='Civ'),
        }
        eng.session_attach(fakes)

        # 1. JSON-RPC initialize & tools/list
        r_init = spore_mcp.handle_jsonrpc(eng, {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {}})
        assert r_init['result']['serverInfo']['name'] == 'sporecoop-mcp'
        r_list = spore_mcp.handle_jsonrpc(eng, {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'})
        names = {t['name'] for t in r_list['result']['tools']}
        assert {'observe', 'click_ui', 'click_f9_row', 'click_target', 'report_bug', 'check_anomalies'} <= names

        # 2. observe + click_ui + press_key(f9) + click_f9_row (rrow -> rbtnN) + click_target
        obs = eng.observe('guest1')
        assert obs['ok'] and obs['stage'] == 'civ' and len(obs['screen_targets']) == 2
        assert any(d['caption'] == 'OK' for d in obs['visible_dialogs'])
        # Background HUD Done/Save must NOT be reported as visible_dialogs when modal=0
        assert not any(d['caption'] in ('Done', 'Save') for d in obs['visible_dialogs'])

        ok_click = eng.click_ui('guest1', 'OK')
        assert ok_click['ok'] and ok_click['source'] == 'native_ui' and ok_click['x'] == 550

        eng.press_key('guest1', 'f9')
        obs_f9 = eng.observe('guest1')
        assert len(obs_f9['f9_window']['rivals_rows']) == 2
        assert len(obs_f9['f9_window']['rows']) == 2
        row_click = eng.click_f9_row('guest1', 'Military')
        assert row_click['ok'] and row_click['button'] == 'Choose' and row_click['x'] == 630
        row_click2 = eng.click_f9_row('guest1', 'Alice')
        assert row_click2['ok'] and row_click2['button'] == 'Declare war' and row_click2['widget'] == 'rbtn1'

        # Also test Space tab `srow` -> `sbtnN` parsing and pause_buttons not triggering Done/Save when modal=0
        sp_ui = spore_mcp._parse_uistate(
            "open=1 page=space modal=0\npausebtn 100 100 200 130 'Resume Game'\n"
            "spage first=0 rows=12\nsrow 0 '  Bob: neutral' 'Propose alliance'\nsbtn0 550 200 710 227 1 'Propose alliance'\n"
        )
        assert len(sp_ui['rivals_rows']) == 1 and sp_ui['rivals_rows'][0]['btn_prefix'] == 'sbtn'
        assert len(sp_ui['rows']) == 1 and sp_ui['rows'][0][1] == 'Propose alliance'

        tgt_click = eng.click_target('guest1', 'hut1', button='right')
        assert tgt_click['ok'] and tgt_click['target']['x'] == 510

        # 2b. HUD buttons scan, click_hud, and perform_actions multi-step batching
        assert 'city_planner' in obs['hud_buttons'] and 'buy_land_vehicle' in obs['hud_buttons']
        assert obs['pause_menu_open'] is False and obs['planner_open'] is False
        hud_res = eng.click_hud('guest1', 'city_planner')
        assert hud_res['ok'] and hud_res['x'] == 150 and hud_res['y'] == 990

        batch = eng.perform_actions('guest1', [
            {'type': 'click_target', 'target_key': 'hut1', 'button': 'left'},
            {'type': 'click_hud', 'name': 'buy_land_vehicle'},
            {'type': 'press_key', 'key': 'tab'},
            {'type': 'wheel', 'x': 720, 'y': 540, 'notches': -4},
            {'type': 'wait', 'seconds': 0.1},
        ])
        assert batch['ok'] and batch['executed'] == 5 and batch['succeeded'] == 5
        assert eng.get_player_stage('guest1') == 'civ'
        assert set(('cell', 'creature', 'tribe', 'civ', 'space', 'editor')) <= set(ai_playtest.STAGE_PLAYBOOKS.keys())

        # 2c. 5-hour quota pause & auto-resume controller
        assert ai_playtest.is_quota_error('HTTP 429: 5-hour usage limit reached, resets in 1h 20m')
        assert not ai_playtest.is_quota_error('DONE: built a house and bought a tank')
        assert ai_playtest.parse_retry_after_seconds('Quota exceeded, resets in 1h 20m') == 4800
        assert ai_playtest.parse_retry_after_seconds('Rate limit, try again in 15 minutes') == 900
        probe_calls = []

        def fake_probe():
            probe_calls.append(1)
            return (len(probe_calls) >= 2, 'still limited' if len(probe_calls) == 1 else 'ok')

        qc = ai_playtest.QuotaPauseController(eng, engine=eng, probe_fn=fake_probe, probe_minutes=0.05)
        # Override sleep granularity by testing with tiny probe_minutes (3s would slow selftest; patch directly)
        qc.probe_minutes = 0.001
        stop_q = threading.Event()
        qc.handle_quota_hit('guest2', '5-hour usage limit reached', stop_q)
        assert qc.pause_count == 1 and not qc.paused and len(probe_calls) == 2
        assert eng.external_pause_active is False
        assert any('key esc' in c for c in fakes['host'].cmds)

        # 3. Oracle fault detection + evidence bundle creation
        assert eng.check_anomalies()['new_anomalies'] == []
        fakes['guest2'].faults = 1
        anom = eng.check_anomalies()
        assert len(anom['new_anomalies']) == 1
        assert anom['new_anomalies'][0]['kind'] == 'fault'
        assert os.path.exists(os.path.join(anom['new_anomalies'][0]['bundle_dir'], 'bundle.json'))

        # 4. Agent bug report with bundle & screenshots
        bug = eng.report_bug('guest1', 'Specialty button test', 'Switches to Military', 'Stayed Economic',
                             severity='P1', steps='Press F9 -> Click Choose')
        assert bug['ok'] and os.path.exists(os.path.join(bug['bug']['bundle_dir'], 'bundle.json'))

        # 5. Streamable HTTP MCP transport + RemoteMcpClient + 4-agent MockLLM parallel loop
        srv = spore_mcp.make_http_server(eng, '127.0.0.1', 0)
        port = srv.server_address[1]
        st = threading.Thread(target=srv.serve_forever, daemon=True)
        st.start()
        try:
            with urllib.request.urlopen(f'http://127.0.0.1:{port}/health', timeout=5) as r:
                health = json.loads(r.read().decode('utf-8'))
                assert health['ok'] and len(health['session']['players']) == 4

            remote = ai_playtest.RemoteMcpClient(f'http://127.0.0.1:{port}/mcp')
            tools = ai_playtest.mcp_tools_to_openai(remote.tool_definitions())
            assert all(t['function']['name'] in ai_playtest.PLAYER_TOOL_NAMES for t in tools)

            llm = ai_playtest.MockLLMClient()
            stop_evt = threading.Event()
            workers = []
            for slot in ('host', 'guest1', 'guest2', 'guest3'):
                th = threading.Thread(
                    target=ai_playtest.run_agent_loop,
                    args=(slot, ai_playtest.PERSONAS[slot], remote, llm, tools, 'civ_npc', 'rivals', stop_evt, 3, 0.01),
                )
                th.start()
                workers.append(th)
            for th in workers:
                th.join(timeout=10.0)
                assert not th.is_alive()

            assert llm.calls == 12  # 4 agents * 3 steps
            assert len(eng.bugs) >= 2  # manual report_bug + guest1 mock bug

            rpt = ai_playtest.write_markdown_report(
                eng.run_dir, eng.session_status(), eng.anomalies, eng.bugs, eng.actions, llm, 1.2
            )
            txt = open(rpt, encoding='utf-8').read()
            assert 'Mock test bug from guest1' in txt
            assert 'hit 1 new caught fault(s)' in txt
        finally:
            srv.shutdown()
            srv.server_close()

        # 6. Stdio transport round-trip
        orig_in, orig_out = sys.stdin, sys.stdout
        try:
            sys.stdin = io.StringIO(
                json.dumps({'jsonrpc': '2.0', 'id': 10, 'method': 'ping'}) + '\n'
                + json.dumps({'jsonrpc': '2.0', 'id': 11, 'method': 'tools/call',
                              'params': {'name': 'session_status', 'arguments': {}}}) + '\n'
            )
            buf = io.StringIO()
            sys.stdout = buf
            spore_mcp.serve_stdio(eng)
            lines = [json.loads(l) for l in buf.getvalue().splitlines() if l.strip()]
            assert len(lines) == 2 and lines[0]['id'] == 10 and lines[1]['id'] == 11
        finally:
            sys.stdin, sys.stdout = orig_in, orig_out

        print('mcp_selftest: ALL OK (stdio, HTTP, 4-agent concurrent mock playtest, Oracle, bug bundles)')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    test_mcp_protocol_and_tools()
