#!/usr/bin/env bash
# The divergence detector over a night (docs/sync-architecture-plan.md L0).
# Runs the `detector` suite (a quiet session per stage, the play bots of every stage, the campaigns)
# with the detector on (SPORE_DETECTOR: a comparison of the host's world with every guest's every
# N seconds), REPEAT times, on the given lanes, then writes out/detector/<date>.md
# (tools/detector_report.py). The findings are data, not a gate: tests pass or fail as usual.
#
#   tools/nightly_detect.sh                       lanes 2-7, 9, 10; 3 rounds; a comparison every 30 s
#   LANES="4 5 6 7" REPEAT=2 tools/nightly_detect.sh
#   tools/detector_report.py --since <YYYYMMDD-HHMM>   the report again (or over other runs)
#
# Never lane 1 (the default game) or lane 8 (the owner's). Lanes with a game still running from
# another job are left out.
set -u
cd "$(dirname "$0")/.."
LANES=${LANES:-"2 3 4 5 6 7 9 10"}
REPEAT=${REPEAT:-3}
PERIOD=${SPORE_DETECTOR:-30}
start=$(date +%Y%m%d-%H%M)

busy_lane() {  # a game of this lane answers its console (another job runs there)
    python3 - "$1" <<'PY'
import sys
sys.path.insert(0, 'tools')
from sporectl import Console
lane = int(sys.argv[1])
for inst in (2 * lane - 1, 2 * lane):
    try:
        if Console(inst, timeout=3).call('ping').get('ok'):
            sys.exit(0)
    except (OSError, ValueError):
        pass
sys.exit(1)
PY
}

args=()
for l in $LANES; do
    if [ "$l" = 1 ] || [ "$l" = 8 ]; then
        echo "nightly_detect: lane $l is never used (1: default, 8: the owner's)"
        continue
    fi
    if busy_lane "$l"; then
        echo "nightly_detect: lane $l has a running game (another job?): left out"
        continue
    fi
    args+=(--lane "$l")
done
if [ ${#args[@]} -eq 0 ]; then
    echo "nightly_detect: no free lane"
    exit 1
fi
echo "nightly_detect: ${args[*]}, $REPEAT round(s), a comparison every $PERIOD s, from $start"
SPORE_DETECTOR=$PERIOD python3 tools/lab.py run detector "${args[@]}" --retry 0 --repeat "$REPEAT"
rc=$?
python3 tools/detector_report.py --since "$start"
echo "nightly_detect: lab exit $rc; report in out/detector/"
exit $rc
