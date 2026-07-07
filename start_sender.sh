#!/usr/bin/env bash
# Usage: ./run_rdma_test.sh <num_hosts>

set -euo pipefail

# ── Configuration ────────────────────────────────────────────────────────────
TARGET_IP="192.168.12.153"
BASE_PORT=53100
DEVICE="mlx5_0"
IB_PORT=3
MAX_MSG=20000
MSG_SIZE=2048
BANDWIDTH=40960000
SCHEDULER_INPUT="input.txt"
# ─────────────────────────────────────────────────────────────────────────────

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <num_hosts> <scheduler_input_file>"
    exit 1
fi

NUM_HOSTS="$1"

if ! [[ "$NUM_HOSTS" =~ ^[1-9][0-9]*$ ]]; then
    echo "Error: num_hosts must be a positive integer, got: '$NUM_HOSTS'"
    exit 1
fi

PIDS=()

cleanup() {
    if [[ ${#PIDS[@]} -gt 0 ]]; then
        echo ""
        echo "Stopping background sender processes (PIDs: ${PIDS[*]})..."
        for pid in "${PIDS[@]}"; do
            kill "$pid" 2>/dev/null && echo "  Killed PID $pid" || echo "  PID $pid already exited"
        done
    fi
}

trap cleanup EXIT

echo "Starting $NUM_HOSTS RDMA sender(s) targeting $TARGET_IP from port $BASE_PORT..."

for (( i=0; i<NUM_HOSTS; i++ )); do
    PORT=$(( BASE_PORT + i ))
    echo "  Launching sender on port $PORT (PID will follow)..."
    ./rdma_read_sender_sw_stream_check_tcpip_scheduled \
        "$TARGET_IP" "$PORT" \
        -d "$DEVICE" \
        -i "$IB_PORT" \
        -M "$MAX_MSG" \
        -S "$MSG_SIZE" \
        -B "$BANDWIDTH" &
    PIDS+=($!)
    echo "    └─ PID ${PIDS[-1]}"
done

echo ""
echo "All $NUM_HOSTS sender(s) running. Launching scheduler..."
echo ""

sleep 10  # Give senders a moment to initialize
./scheduler "$2"
SCHEDULER_EXIT=$?

echo ""
echo "Scheduler finished with exit code $SCHEDULER_EXIT."

# cleanup is called automatically via the EXIT trap
exit $SCHEDULER_EXIT