#!/usr/bin/env bash
# Live-tails the GemPBA telemetry stream of a run on this machine.
#
# Connects to the aggregator's TCP socket on 127.0.0.1 and prints, for each
# broadcast, one row per rank: its role, tasks started, sent and received,
# thread count, CPU% and RSS (KB). A paused stream prints a "paused" row per
# broadcast, and each run marker is printed once, when it first appears.
# Press Ctrl+C to stop.
#
# The gempba aggregator binds loopback only, so the viewer connects to
# 127.0.0.1. Pass --port to match the port the program set with
# gempba::telemetry::configure_port (default 9000). To watch a run on another
# machine, use the GemPBA Dashboard, which opens the SSH tunnel itself.
#
# Dependencies: bash 4+ (for /dev/tcp) and jq 1.6+.
#
# Usage:
#   scripts/telemetry_view.sh                       # 127.0.0.1:9000, one row per rank
#   scripts/telemetry_view.sh --port 9100 --raw     # non-default port, pretty-printed JSON

set -u
set -o pipefail

PORT=9000
RAW=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [-p|--port <port>] [-r|--raw] [-h|--help]

  -p, --port <port>   TCP port to connect to on 127.0.0.1 (default: 9000).
  -r, --raw           Print full pretty-printed JSON for each broadcast instead
                      of one row per rank.
  -h, --help          Show this help.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -p|--port)
            [ $# -lt 2 ] && { echo "ERROR: $1 requires a value" >&2; exit 2; }
            PORT="$2"; shift 2
            ;;
        -r|--raw)
            RAW=1; shift
            ;;
        -h|--help)
            usage; exit 0
            ;;
        *)
            echo "ERROR: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

# 127.0.0.1 (not "localhost"): some resolvers prefer ::1 (IPv6) first while
# the gempba aggregator binds INADDR_LOOPBACK (IPv4 only).
IP=127.0.0.1

if ! command -v jq >/dev/null 2>&1; then
    echo "ERROR: jq is required. Install via your package manager (e.g. apt install jq, dnf install jq, brew install jq)." >&2
    exit 1
fi

# Open the TCP connection on fd 3 (bash built-in; no nc needed).
if ! { exec 3<>"/dev/tcp/$IP/$PORT"; } 2>/dev/null; then
    echo "ERROR: cannot connect to $IP:$PORT" >&2
    echo "  - Is the gempba program running on this machine?" >&2
    echo "  - If it set another port with gempba::telemetry::configure_port, pass --port <n> here too." >&2
    exit 1
fi

trap 'exec 3<&- 2>/dev/null; exec 3>&- 2>/dev/null' EXIT INT TERM

echo "Connected to $IP:$PORT - streaming broadcasts (Ctrl+C to stop)"

if [ "$RAW" -eq 1 ]; then
    while IFS= read -r line <&3; do
        [ -z "${line//[[:space:]]/}" ] && continue
        printf '%s\n' "$line" | jq . 2>/dev/null || continue
        echo
    done
    exit 0
fi

# One broadcast in, tab-separated records out: S (run id and marker count),
# M (a marker not printed yet), P (a paused line) and R (one rank's row).
# $run and $seen say which markers this run has printed already.
SUMMARY=$(cat <<'JQ'
def pad2: tostring | if length < 2 then "0" + . else . end;
def clock: . / 1000 | floor | strflocaltime("%H:%M:%S");
def hms: floor as $s | "\($s / 3600 | floor):\($s % 3600 / 60 | floor | pad2):\($s % 60 | pad2)";
(.ts // 0 | clock) as $time
| (.elapsed_seconds // 0 | hms) as $elapsed
| if .paused == true and .ranks == null then
    ["P", $time, $elapsed] | @tsv
  else
    ((.topology.identities // []) | map({key: (.rank | tostring), value: (.role // "-")}) | from_entries) as $roles
    | (.topology.run.markers // []) as $markers
    | (if (.run_id // "") == $run then $seen else 0 end) as $from
    | (["S", (.run_id // ""), ($markers | length)] | @tsv),
      ($markers[$from:][] | ["M", .elapsed_ms, .label, .source] | @tsv),
      ((.ranks // []) | sort_by(.rank)[]
        | ["R", $time, $elapsed, .rank, ($roles[.rank | tostring] // "-"), .tasks_started_total, .tasks_sent_total,
           .tasks_recv_total, .process_threads, .process_cpu_pct, ((.process_rss_bytes // 0) / 1024 | floor)] | @tsv)
  end
JQ
)

ms_to_hms() {
    local ms=$1 s
    s=$(( ms / 1000 ))
    printf '%d:%02d:%02d.%03d' $(( s / 3600 )) $(( s % 3600 / 60 )) $(( s % 60 )) $(( ms % 1000 ))
}

print_header() {
    printf '%8s  %12s  %4s  %-6s  %13s  %10s  %10s  %7s  %6s  %10s\n' "$@"
}

print_row() {
    printf '%8s  %12s  %4s  %-6s  %13s  %10s  %10s  %7s  %5.1f%%  %10s\n' "$@"
}

echo
print_header "time" "elapsed" "rank" "role" "tasks_started" "tasks_sent" "tasks_recv" "threads" "cpu%" "rss(KB)"
printf -- '-%.0s' $(seq 1 104); printf '\n'

RUN_ID=""
SEEN=0
while IFS= read -r line <&3; do
    [ -z "${line//[[:space:]]/}" ] && continue
    records=$(printf '%s' "$line" | jq -r --arg run "$RUN_ID" --argjson seen "$SEEN" "$SUMMARY" 2>/dev/null) || continue
    # A native Windows jq ends its lines with CRLF.
    records=${records//$'\r'/}
    first=1
    while IFS=$'\t' read -r kind a b c d e f g h i j; do
        case "$kind" in
            S)
                RUN_ID=$a
                SEEN=$b
                ;;
            M)
                printf '%8s  %12s  marker: %s (%s)\n' "" "$(ms_to_hms "$a")" "$b" "$c"
                ;;
            P)
                printf '%8s  %12s  paused\n' "$a" "$b"
                ;;
            R)
                if [ "$first" -eq 1 ]; then
                    first=0
                else
                    a=""; b=""
                fi
                print_row "$a" "$b" "$c" "$d" "$e" "$f" "$g" "$h" "$i" "$j"
                ;;
        esac
    done <<<"$records"
done
