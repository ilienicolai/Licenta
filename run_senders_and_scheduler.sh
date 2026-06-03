#!/bin/bash
# Run on 192.168.12.153 (sender machine)
# Opens 3 senders in separate terminals, then runs the scheduler

BIN="./rdma_read_sender_sw_stream_check_tcpip_scheduled"
OPTS="-d mlx5_0 -i 3 -M 20000 -S 2048 -B 40960000"
LOCAL_IP="192.168.12.153"

SESSION="senders"

if command -v tmux &>/dev/null; then
    tmux kill-session -t "$SESSION" 2>/dev/null

    tmux new-session -d -s "$SESSION" -x 220 -y 50 -n "S53100" \
        "bash -c '$BIN $LOCAL_IP 53100 $OPTS; echo \"[DONE] Press Enter\"; read'"
    tmux new-window -t "$SESSION" -n "S53101" \
        "bash -c '$BIN $LOCAL_IP 53101 $OPTS; echo \"[DONE] Press Enter\"; read'"
    tmux new-window -t "$SESSION" -n "S53102" \
        "bash -c '$BIN $LOCAL_IP 53102 $OPTS; echo \"[DONE] Press Enter\"; read'"

    echo "All 3 senders launched in tmux session '$SESSION'."
    echo "Attach with: tmux attach -t $SESSION"
else
    echo "tmux not found. Starting senders in background (logs to sender_PORT.log)..."
    $BIN $LOCAL_IP 53100 $OPTS > sender_53100.log 2>&1 &
    $BIN $LOCAL_IP 53101 $OPTS > sender_53101.log 2>&1 &
    $BIN $LOCAL_IP 53102 $OPTS > sender_53102.log 2>&1 &
    echo "PIDs: $(pgrep -f rdma_read_sender_sw_stream_check_tcpip_scheduled | tr '\n' ' ')"
fi

echo "Waiting 2s before starting scheduler..."
sleep 2
echo "Running scheduler..."
./scheduler input.txt
