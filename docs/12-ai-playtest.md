# AI Playtesting via SporeCoop MCP

SporeCoop includes a zero-dependency Model Context Protocol (MCP) server ([spore_mcp.py](../tools/spore_mcp.py)) and a 4-agent autonomous playtest runner ([ai_playtest.py](../tools/ai_playtest.py)) that drive 2 to 4 real Wine Spore instances simultaneously using only genuine player UI and keyboard/mouse inputs (no cheats).

## Architecture

1. **MCP Server (`tools/spore_mcp.py`)**:
   - Exposes structured perception (`observe`) so LLMs can play accurately without pixel guessing: stage, resources, F9 Co-op window rows/buttons/chat, live native Spore HUD icon buttons (`hud_buttons` via `coop.ui where`), `pause_menu_open` / `planner_open` flags, visible modal dialogs, stage entities (tribes, cities, vehicles, NPCs, star systems), Rivals state, and live on-screen 2D target coordinates (`coop.targets objs`).
   - Exposes real player input actions (`perform_actions` for 1..12 batched human-speed steps per tool call, `click_hud`, `click_ui`, `click_f9_row`, `click_target`, `click_screen`, `drag_screen`, `press_key`, `type_text`, `pan_camera`, `reconnect_player`, `screenshot`).
   - Runs an automated **Oracle** (`check_anomalies`) checking all instances for crashes, caught faults (`exc`), frame freezes, stuck pauses (`coop.pauses`), and severe log errors, automatically saving multi-player evidence bundles (screenshots + logs + state + action timeline) to `out/ai-playtest/<timestamp>/bugs/`.
   - Supports both **stdio** transport (for standard MCP clients) and **Streamable HTTP** (`--http 127.0.0.1:8765`, `POST /mcp`, `GET /health`) so 4 independent agents can share one live 4-player session concurrently.

2. **4-Agent DeepSeek Runner (`tools/ai_playtest.py`)**:
   - Runs 2..4 concurrent agents with distinct gameplay personas (`--focus gameplay` default: Strategic Leader, Builder/Economist/Diplomat, Military Aggressor, Explorer/Planner/Stress QA) plus dynamic per-stage playbooks (`cell`, `creature`, `tribe`, `civ`, `space`, `editor`) that adapt automatically if an overnight campaign transitions between stages.
   - Keeps persistent per-player round memory (`memory_<player>.txt`), allows multimodal `deepseek-v4.1-flash` agents to visually `Read` their own PNG screenshots to click icon-only Spore palettes/planners/editors, and checkpoints `out/ai-playtest/<timestamp>/report.md` every 60 seconds during long overnight runs.

## Quick Start

### 1. Run 4 DeepSeek agents via OpenCode Go subscription (no API key env needed)

Uses `opencode-go/deepseek-v4.1-flash` (fast + multimodal screenshot inspection) across all 4 players by default (override via `--model`):

```bash
# Deep gameplay test on Civ Rivals (15 minutes, lane 2)
LAB_LANE=2 python3 tools/ai_playtest.py --backend opencode --focus gameplay --players 4 --fixture civ_npc --mode rivals --minutes 15

# Deep gameplay test on Tribe Rivals (20 minutes, lane 2)
LAB_LANE=2 python3 tools/ai_playtest.py --backend opencode --focus gameplay --players 4 --fixture tribe --mode rivals --minutes 20

# Overnight autonomous marathon (e.g. 6 hours from Cell -> Space, live report.md updated every 60s)
LAB_LANE=2 python3 tools/ai_playtest.py --backend opencode --focus gameplay --players 4 --fixture cell --mode rivals --minutes 360 --round-timeout 150
```

During an overnight run, monitor live progress at any time:
```bash
# Latest report (updated every 60 seconds)
ls -td out/ai-playtest/*/report.md | head -1 | xargs cat
```

### 2. Run 4 DeepSeek agents directly via DeepSeek API

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
