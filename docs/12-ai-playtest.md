# AI Playtesting via SporeCoop MCP

SporeCoop includes a zero-dependency Model Context Protocol (MCP) server ([spore_mcp.py](../tools/spore_mcp.py)) and a 4-agent autonomous playtest runner ([ai_playtest.py](../tools/ai_playtest.py)) that drive 2 to 4 real Wine Spore instances simultaneously using only genuine player UI and keyboard/mouse inputs (no cheats).

## Architecture

1. **MCP Server (`tools/spore_mcp.py`)**:
   - Exposes structured text/JSON perception (`observe`) so text-only LLMs like DeepSeek can play accurately without pixel guessing: stage, resources, F9 Co-op window rows/buttons/chat, visible modal dialogs, stage entities (tribes, cities, vehicles, NPCs, star systems), Rivals state, and live on-screen 2D target coordinates (`coop.targets objs`).
   - Exposes real player input actions (`click_ui`, `click_f9_row`, `click_target`, `click_screen`, `drag_screen`, `press_key`, `type_text`, `pan_camera`, `reconnect_player`).
   - Runs an automated **Oracle** (`check_anomalies`) checking all instances for crashes, caught faults (`exc`), frame freezes, stuck pauses (`coop.pauses`), and severe log errors, automatically saving multi-player evidence bundles (screenshots + logs + state + action timeline) to `out/ai-playtest/<timestamp>/bugs/`.
   - Supports both **stdio** transport (for standard MCP clients) and **Streamable HTTP** (`--http 127.0.0.1:8765`, `POST /mcp`, `GET /health`) so 4 independent agents can share one live 4-player session concurrently.

2. **4-Agent DeepSeek Runner (`tools/ai_playtest.py`)**:
   - Runs 2..4 concurrent threads with distinct human QA personas (`host` Normal Leader, `guest1` Diplomat/Social, `guest2` Aggressor/Combat, `guest3` Chaos QA Tester) against DeepSeek (`deepseek-chat` / `deepseek-reasoner`) or any OpenAI-compatible API.
   - Writes a consolidated Markdown report to `out/ai-playtest/<timestamp>/report.md`.

## Quick Start

### 1. Run 4 DeepSeek agents directly (recommended)

```bash
export DEEPSEEK_API_KEY="sk-..."

# 4 players in Tribe Rivals for 15 minutes on lab lane 2
SPORE_LANE=2 python3 tools/ai_playtest.py --players 4 --fixture tribe --mode rivals --minutes 15

# 4 players in Civ Rivals for 20 minutes on lab lane 2
SPORE_LANE=2 python3 tools/ai_playtest.py --players 4 --fixture civ_npc --mode rivals --minutes 20
```

Available fixtures: `cell`, `creature`, `tribe`, `civ_npc`, `space_free`.

### 2. Run MCP server separately (connect any external MCP client or 4 custom agents)

```bash
# HTTP MCP server (attach to already running Spore instances 1 and 2)
python3 tools/spore_mcp.py --http 127.0.0.1:8765 --attach host=1,guest1=2

# Or let clients call session_start() on lane 2
SPORE_LANE=2 python3 tools/spore_mcp.py --http 127.0.0.1:8765

# Point ai_playtest.py at that running MCP server
python3 tools/ai_playtest.py --mcp-url http://127.0.0.1:8765/mcp --minutes 15
```

### 3. Stdio MCP client configuration (Claude Desktop / Cursor / Antigravity)

```json
{
  "mcpServers": {
    "sporecoop": {
      "command": "python3",
      "args": ["/home/pam3es/Games/Modding/SPORE/SporeCoop/tools/spore_mcp.py", "--lane", "2"]
    }
  }
}
```

### 4. Offline self-test (no Wine or API key needed)

```bash
python3 tools/tests/mcp_selftest.py
```
