#!/usr/bin/env bash
# Live-tails the GemPBA telemetry stream from a running benchmark.
#
# Connects to the center's TCP socket on 127.0.0.1 and prints one formatted
# line per broadcast frame: timestamp, elapsed, tasks_local_total,
# tasks_sent_total, process threads, CPU%, and process RSS (KB).
# Press Ctrl+C to stop.
#
# The gempba center always binds loopback only -- the connection target is
# therefore always 127.0.0.1, either directly when running on the same host
# or as the local end of an SSH tunnel for remote dashboards. Pass --port to
# match whatever port the gempba binary was launched with (-telemetry_port).
#
# The gempba binary must be built with -DGEMPBA_TELEMETRY=ON.
#
# Dependencies: bash 4+ (for /dev/tcp), jq, GNU date (Linux default).
#
# Usage:
#   scripts/telemetry_view.sh                       # 127.0.0.1:9000, formatted summary
#   scripts/telemetry_view.sh --port 9100 --raw     # non-default port, pretty-printed JSON
#
# Remote (SSH tunnel) example:
#   # In one shell:
#   ssh -J user@login.cluster.edu -L 9000:127.0.0.1:9000 user@compute-12
#   # In a second shell:
#   scripts/telemetry_view.sh

set -u
set -o pipefail

PORT=9000
RAW=0

usage() {
    cat <<EOF
Usage: $(basename "$0") [-p|--port <port>] [-r|--raw] [-h|--help]

  -p, --port <port>   TCP port to connect to on 127.0.0.1 (default: 9000).
  -r, --raw           Print full pretty-printed JSON for each frame instead of
                      the formatted one-line summary.
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
# the gempba center binds INADDR_LOOPBACK (IPv4 only).
IP=127.0.0.1

if ! command -v jq >/dev/null 2>&1; then
    echo "ERROR: jq is required. Install via your package manager (e.g. apt install jq, dnf install jq)." >&2
    exit 1
fi

# Open the TCP connection on fd 3 (bash built-in; no nc needed).
if ! exec 3<>"/dev/tcp/$IP/$PORT" 2>/dev/null; then
    echo "ERROR: cannot connect to $IP:$PORT" >&2
    echo "  - Is the benchmark running and the SSH tunnel (if any) still up?" >&2
    echo "  - Was the benchmark built with -DGEMPBA_TELEMETRY=ON ?" >&2
    echo "  - If the benchmark was launched with -telemetry_port <n>, pass --port <n> here too." >&2
    exit 1
fi

trap 'exec 3<&- 2>/dev/null; exec 3>&- 2>/dev/null' EXIT INT TERM

echo "Connected to $IP:$PORT - streaming frames (Ctrl+C to stop)"

if [ "$RAW" -eq 1 ]; then
    while IFS= read -r line <&3; do
        [ -z "${line//[[:space:]]/}" ] && continue
        printf '%s\n' "$line" | jq . 2>/dev/null || continue
        echo
    done
    exit 0
fi

echo
printf '%8s  %10s  %-14s  %-14s  %7s  %7s  %12s\n' \
    "time" "elapsed" "tasks_local" "tasks_remote" "threads" "cpu%" "rss(KB)"
printf -- '-%.0s' $(seq 1 95); printf '\n'

while IFS= read -r line <&3; do
    [ -z "${line//[[:space:]]/}" ] && continue

    # Extract fields; emit "SKIP" when no worker frames yet (center just
    # started, aggregator hasn't produced its first publish).
    row=$(printf '%s' "$line" | jq -r '
        if (.workers // [] | length) == 0 then
            "SKIP"
        else
            .workers[0] as $w |
            [
                (.ts // 0),
                (.elapsed_seconds // 0),
                ($w.tasks_local_total // 0),
                ($w.tasks_sent_total // 0),
                ($w.process_threads // 0),
                ($w.process_cpu_pct // 0),
                ($w.process_rss_bytes // 0)
            ] | @tsv
        end
    ' 2>/dev/null) || continue
    [ "$row" = "SKIP" ] && continue

    IFS=$'\t' read -r ts_ms elapsed_s tasks_local tasks_remote threads cpu_pct rss_bytes <<<"$row"

    ts_sec=$(( ${ts_ms%.*} / 1000 ))
    ts=$(date -d "@$ts_sec" +'%H:%M:%S' 2>/dev/null || echo '??:??:??')

    elapsed_int=${elapsed_s%.*}
    h=$(( elapsed_int / 3600 ))
    m=$(( (elapsed_int % 3600) / 60 ))
    s=$(( elapsed_int % 60 ))
    elapsed_fmt=$(printf '%d:%02d:%02d' "$h" "$m" "$s")

    rss_kb=$(awk -v b="$rss_bytes" 'BEGIN { printf "%d", b/1024 }')

    printf '%8s  %10s  %-14s  %-14s  %7s  %6.1f%%  %12d\n' \
        "$ts" "$elapsed_fmt" "$tasks_local" "$tasks_remote" "$threads" "$cpu_pct" "$rss_kb"
done
