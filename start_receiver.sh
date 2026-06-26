#!/usr/bin/env bash
# Usage: ./run_rdma_receivers.sh <num_hosts>

set -euo pipefail

# ── Configuration ────────────────────────────────────────────────────────────
TARGET_IP="192.168.12.155"
BASE_PORT=53100
DEVICE="mlx5_1"
IB_PORT=3
MAX_MSG=20000
MSG_SIZE=2048
BANDWIDTH=40960000
WORKERS=4
SPEED=90
JITTER=75
SLEEP_DURATION=20
# ─────────────────────────────────────────────────────────────────────────────

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <num_hosts>"
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
        echo "Stopping background receiver processes (PIDs: ${PIDS[*]})..."
        for pid in "${PIDS[@]}"; do
            kill "$pid" 2>/dev/null && echo "  Killed PID $pid" || echo "  PID $pid already exited"
        done
    fi
}

trap cleanup EXIT

echo "Starting $NUM_HOSTS RDMA receiver(s) targeting $TARGET_IP from port $BASE_PORT..."

for (( i=0; i<NUM_HOSTS; i++ )); do
    PORT=$(( BASE_PORT + i ))
    echo "  Launching receiver on port $PORT..."
    ./rdma_read_receiver_sw_stream_check_tcpip_scheduled \
        "$TARGET_IP" "$PORT" \
        -d "$DEVICE" \
        -i "$IB_PORT" \
        -M "$MAX_MSG" \
        -S "$MSG_SIZE" \
        -B "$BANDWIDTH" \
        -w "$WORKERS" \
        -s "$SPEED" \
        -j "$JITTER" &
    PIDS+=($!)
    echo "    └─ PID ${PIDS[-1]}"
done

echo ""
echo "All $NUM_HOSTS receiver(s) running. Waiting ${SLEEP_DURATION}s..."
sleep "$SLEEP_DURATION"

echo "Sleep done, shutting down receivers."

# cleanup is called automatically via the EXIT trap