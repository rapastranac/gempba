#!/usr/bin/env bash
# Open an SSH tunnel to a remote gempba center and live-tail its telemetry.
#
# Wraps an ssh -L forward plus telemetry_view.sh into one command: brings the
# tunnel up, waits until the local end carries data, then runs the viewer
# against it. With --jump-host the forward is nested so the final hop
# originates on the login node. Ctrl+C, or the viewer returning, tears it down.
#
# On a cluster, find the node running rank 0 yourself (`squeue` on the login
# node -- the first node in the list), then pass that node as --ssh-host and
# the login node as --jump-host.
#
# Usage:
#   scripts/telemetry_tunnel.sh --ssh-host me@my-server
#   scripts/telemetry_tunnel.sh --ssh-host me@compute-node --jump-host me@login-node
#   scripts/telemetry_tunnel.sh --ssh-host me@server --local-port 9100 --remote-port 9100

set -u
set -o pipefail

SSH_HOST=""
JUMP_HOST=""
LOCAL_PORT=9000
REMOTE_PORT=9000

usage() {
    cat <<EOF
Usage: $(basename "$0") --ssh-host <host> [--jump-host <host>] [--local-port <port>] [--remote-port <port>]

  -s, --ssh-host <host>     FINAL destination -- the host actually running gempba. On a
                            cluster that is the compute node where rank 0 runs; at home
                            it is just your server. (required)
  -j, --jump-host <host>    Optional. The host you ssh into FIRST to reach --ssh-host
                            (e.g. cluster login / bastion). Omit when --ssh-host is
                            reachable directly.
  -l, --local-port <port>   Local port the viewer connects to (default: 9000).
  -r, --remote-port <port>  Remote loopback port gempba is listening on (default: 9000).
                            Match this to whatever -telemetry_port the binary was
                            launched with.
  -h, --help                Show this help.

Examples:
  $(basename "$0") --ssh-host me@my-server
  $(basename "$0") --ssh-host me@compute-node --jump-host me@login-node
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -s|--ssh-host)
            [ $# -lt 2 ] && { echo "ERROR: $1 requires a value" >&2; exit 2; }
            SSH_HOST="$2"; shift 2
            ;;
        -j|--jump-host)
            [ $# -lt 2 ] && { echo "ERROR: $1 requires a value" >&2; exit 2; }
            JUMP_HOST="$2"; shift 2
            ;;
        -l|--local-port)
            [ $# -lt 2 ] && { echo "ERROR: $1 requires a value" >&2; exit 2; }
            LOCAL_PORT="$2"; shift 2
            ;;
        -r|--remote-port)
            [ $# -lt 2 ] && { echo "ERROR: $1 requires a value" >&2; exit 2; }
            REMOTE_PORT="$2"; shift 2
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

if [ -z "$SSH_HOST" ]; then
    echo "ERROR: --ssh-host is required." >&2
    usage >&2
    exit 2
fi

if ! command -v ssh >/dev/null 2>&1; then
    echo "ERROR: ssh client not found in PATH. Install openssh-client." >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VIEWER="$SCRIPT_DIR/telemetry_view.sh"
if [ ! -x "$VIEWER" ] && [ ! -r "$VIEWER" ]; then
    echo "ERROR: telemetry_view.sh not found next to this script ($VIEWER)." >&2
    exit 1
fi

# Compute nodes only trust login-originated ssh, so nest instead of -J.
if [ -n "$JUMP_HOST" ]; then
    MID=$(( (RANDOM % 40000) + 20000 ))
    # Run the inner ssh directly as the remote command (the form proven to bind
    # the forward). It authenticates by key and binds <mid> on the login node,
    # forwarding to the compute node's gempba port; the outer ssh stays connected
    # as long as this inner ssh runs. Forcing publickey skips the cluster's
    # hostbased default, which hangs when run non-interactively.
    SSH_ARGS=(-o StrictHostKeyChecking=accept-new -o ExitOnForwardFailure=yes
              -L "${LOCAL_PORT}:localhost:${MID}"
              "$JUMP_HOST"
              ssh -N -o ExitOnForwardFailure=yes -o StrictHostKeyChecking=accept-new
              -o PreferredAuthentications=publickey
              -L "${MID}:127.0.0.1:${REMOTE_PORT}" "$SSH_HOST")
    echo "Tunnel: $SSH_HOST via $JUMP_HOST (login-originated hop)"
    DEADLINE_SECS=120
else
    SSH_ARGS=(-N -o ExitOnForwardFailure=yes -L "${LOCAL_PORT}:127.0.0.1:${REMOTE_PORT}" "$SSH_HOST")
    echo "Tunnel: $SSH_HOST"
    DEADLINE_SECS=15
fi

SSH_PID=""        # set in direct mode (bash-backgrounded with '&')
CLEAN_PAT=""      # set in jump mode (ssh -f forks; match it by its unique -L)
cleanup() {
    if [ -n "$SSH_PID" ] && kill -0 "$SSH_PID" 2>/dev/null; then
        kill "$SSH_PID" 2>/dev/null
        wait "$SSH_PID" 2>/dev/null
    fi
    [ -n "$CLEAN_PAT" ] && pkill -f "$CLEAN_PAT" 2>/dev/null
}
trap cleanup EXIT INT TERM

tunnel_alive() {
    if [ -n "$SSH_PID" ]; then
        kill -0 "$SSH_PID" 2>/dev/null
    else
        pgrep -f "$CLEAN_PAT" >/dev/null 2>&1
    fi
}

# Jump mode uses 'ssh -f' instead of '&': ssh must background ITSELF after the
# forward is up, because a bash-backgrounded ssh running a remote command exits
# before the inner forward binds. -f forks, so we reap it by its unique
# local-forward spec rather than a PID. Duo happens in the foreground here,
# before -f returns. Direct mode has no remote command, so plain '&' is fine.
if [ -n "$JUMP_HOST" ]; then
    CLEAN_PAT="${LOCAL_PORT}:localhost:${MID}"
    ssh -f "${SSH_ARGS[@]}"
else
    ssh "${SSH_ARGS[@]}" &
    SSH_PID=$!
fi

# Require a readable line, not just a connect: the local listener accepts before
# the inner hop is up. Bail early if ssh dies first.
DEADLINE=$(( $(date +%s) + DEADLINE_SECS ))
READY=0
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
    if ! tunnel_alive; then
        echo "ERROR: ssh exited before the tunnel was ready." >&2
        SSH_PID=""
        exit 1
    fi
    if { exec 6<>"/dev/tcp/127.0.0.1/$LOCAL_PORT"; } 2>/dev/null; then
        if IFS= read -r -t 3 -u 6 _line; then
            { exec 6<&-; } 2>/dev/null
            READY=1
            break
        fi
        { exec 6<&-; } 2>/dev/null
    fi
    sleep 0.2
done

if [ "$READY" -ne 1 ]; then
    echo "ERROR: tunnel did not become ready on 127.0.0.1:$LOCAL_PORT within ${DEADLINE_SECS}s." >&2
    exit 1
fi

bash "$VIEWER" --port "$LOCAL_PORT"
