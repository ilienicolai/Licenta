#!/bin/bash
# Run on 192.168.12.155 (receiver machine)
# Opens 3 receivers in separate gnome-terminal tabs (or xterm if gnome-terminal unavailable)

BIN="./rdma_read_receiver_sw_stream_check_tcpip_scheduled"
OPTS="-d mlx5_1 -i 3 -M 20000 -S 2048 -B 40960000 -w 4 -s 90 -j 75"
LOCAL_IP="192.168.12.155"

SESSION="receivers"

if command -v tmux &>/dev/null; then
    # Kill existing session if any
    tmux kill-session -t "$SESSION" 2>/dev/null

    tmux new-session -d -s "$SESSION" -x 220 -y 50 -n "R53100" \
        "bash -c '$BIN $LOCAL_IP 53100 $OPTS; echo \"[DONE] Press Enter\"; read'"
    tmux new-window -t "$SESSION" -n "R53101" \
        "bash -c '$BIN $LOCAL_IP 53101 $OPTS; echo \"[DONE] Press Enter\"; read'"
    tmux new-window -t "$SESSION" -n "R53102" \
        "bash -c '$BIN $LOCAL_IP 53102 $OPTS; echo \"[DONE] Press Enter\"; read'"

    echo "All 3 receivers launched in tmux session '$SESSION'."
    echo "Attach with: tmux attach -t $SESSION"
    echo "Switch windows: Ctrl+b then 0/1/2"
else
    echo "tmux not found. Starting receivers in background (logs to receiver_PORT.log)..."
    $BIN $LOCAL_IP 53100 $OPTS > receiver_53100.log 2>&1 &
    $BIN $LOCAL_IP 53101 $OPTS > receiver_53101.log 2>&1 &
    $BIN $LOCAL_IP 53102 $OPTS > receiver_53102.log 2>&1 &
    echo "PIDs: $(pgrep -f rdma_read_receiver_sw_stream_check_tcpip_scheduled | tr '\n' ' ')"
fi
