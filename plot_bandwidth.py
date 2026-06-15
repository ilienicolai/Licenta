#!/usr/bin/env python3
"""
Plot RDMA effective bandwidth from `t1` log lines.

The RDMA-read receiver emits one `t1` line per completed cycle (chunk):

    t1:<client_id>:<wall_ns>:<cpu_delta_ns>:<chunk_bytes>
       |           |         |              |
       |           |         |              +- bytes moved this cycle
       |           |         +- thread-CPU ns spent issuing/polling the reads
       |           +- wall-clock ns since start, captured at the cycle START
       +- client id

Effective (goodput) bandwidth for cycle i is the chunk size divided by the
wall-clock time between the start of cycle i-1 and cycle i.  Because the wall
clock includes any time spent stalled on backpressure, this is the *effective*
throughput the receiver actually sustained:

    bandwidth = chunk_bytes / (wall_ns[i] - wall_ns[i-1])

A neat unit identity makes the conversion trivial:

    bytes / nanosecond  ==  gigabytes / second   (because 1e9 / 1e9 == 1)

so `chunk_bytes / delta_ns` is already in GB/s.

`s1` lines (backpressure stall samples) are ignored for the bandwidth plot.

Usage
-----
    python3 plot_bandwidth.py run.log
    python3 plot_bandwidth.py hw_receiver.log sw_receiver.log -o compare.png
    python3 plot_bandwidth.py *.log --rolling 5 --bits --show

Each input file may contain several client ids; every (file, client) pair is
drawn as its own line.  When a file holds more than one client id the legend
label becomes "<filename>_client_<n>", which reproduces labels such as
"hw_receiver_client_1" when the file is named `hw_receiver.log`.
"""

import argparse
import os
import sys
from collections import deque


# Colour palette ordered to match the reference Grafana plot
# (green, gold, blue, orange) then a few extras for additional lines.
PALETTE = [
    "#5E9F4D",  # green
    "#DDB010",  # gold
    "#5B8CFA",  # blue
    "#FA6400",  # orange
    "#A24DB3",  # purple
    "#46B3A9",  # teal
    "#C0504D",  # brick
    "#8C8C8C",  # grey
]


def parse_t1(path):
    """Parse a log file, returning {client_id: [(wall_ns, chunk_bytes), ...]}."""
    data = {}
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line.startswith("t1:"):
                continue
            parts = line.split(":")
            if len(parts) != 5:
                continue
            try:
                client_id = int(parts[1])
                wall_ns = int(parts[2])
                # parts[3] is the CPU-time delta, not needed for wall-clock BW.
                chunk_bytes = int(parts[4])
            except ValueError:
                continue
            data.setdefault(client_id, []).append((wall_ns, chunk_bytes))

    for client_id in data:
        data[client_id].sort()  # sort by wall_ns
    return data


def compute_bandwidth(samples, bits=False, max_seconds=None):
    """Convert sorted (wall_ns, chunk_bytes) samples into (seconds, GB/s)."""
    xs, ys = [], []
    # wall_ns of the very first sample is the t=0 reference
    origin_ns = samples[0][0] if samples else 0
    cutoff_ns = (origin_ns + max_seconds * 1e9) if max_seconds is not None else None

    for i in range(1, len(samples)):
        prev_ns, _ = samples[i - 1]
        cur_ns, chunk = samples[i]
        if cutoff_ns is not None and cur_ns > cutoff_ns:
            break
        delta_ns = cur_ns - prev_ns
        if delta_ns <= 0:
            continue
        value = chunk / delta_ns  # bytes/ns == GB/s
        if bits:
            value *= 8.0          # GB/s -> Gb/s
        xs.append(cur_ns / 1e9)   # ns -> seconds (elapsed)
        ys.append(value)
    return xs, ys


def rolling_mean(values, window):
    """Simple trailing rolling average (pure Python, no numpy needed)."""
    if window <= 1:
        return values
    out = []
    buf = deque()
    running = 0.0
    for v in values:
        buf.append(v)
        running += v
        if len(buf) > window:
            running -= buf.popleft()
        out.append(running / len(buf))
    return out


def hms_formatter(x, _pos=None):
    """Format an x-axis value (seconds) as H:MM:SS or MM:SS, Grafana-style."""
    x = int(round(x))
    if x < 0:
        x = 0
    hours, rem = divmod(x, 3600)
    minutes, seconds = divmod(rem, 60)
    if hours:
        return f"{hours:d}:{minutes:02d}:{seconds:02d}"
    return f"{minutes:02d}:{seconds:02d}"


def build_label(stem, client_id, multi_client):
    """Legend label: '<stem>' or '<stem>_client_<n>' when several clients."""
    if multi_client:
        return f"{stem}_client_{client_id + 1}"
    return stem


def parse_args(argv):
    p = argparse.ArgumentParser(
        description="Plot effective RDMA bandwidth from t1 log lines.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("files", nargs="+", help="One or more log files to plot.")
    p.add_argument("-o", "--output", default=None,
                   help="Path to save the figure (PNG/PDF/SVG). "
                        "Defaults to '<first-log>_bandwidth.png'.")
    p.add_argument("--bits", action="store_true",
                   help="Plot in gigabits/s (Gbps) instead of gigabytes/s (GBps).")
    p.add_argument("--rolling", type=int, default=1, metavar="N",
                   help="Smooth each line with an N-sample rolling average.")
    p.add_argument("--title", default=None, help="Optional plot title.")
    p.add_argument("--labels", nargs="+", default=None, metavar="LABEL",
                   help="Custom legend labels, one per file "
                        "(e.g. --labels 'workers=1' 'workers=2' 'workers=4'). "
                        "Defaults to the filename stem.")
    p.add_argument("--duration", type=float, default=None, metavar="SECONDS",
                   help="Only use the first SECONDS of data from each file.")
    p.add_argument("--figsize", default="12x4", metavar="WxH",
                   help="Figure size in inches, e.g. 12x4.")
    p.add_argument("--show", action="store_true",
                   help="Open an interactive window instead of only saving.")
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(argv if argv is not None else sys.argv[1:])

    # Choose a non-interactive backend when we are only saving (headless-safe).
    import matplotlib
    if not args.show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter

    try:
        w_str, h_str = args.figsize.lower().split("x")
        figsize = (float(w_str), float(h_str))
    except ValueError:
        print(f"Invalid --figsize '{args.figsize}', using 12x4.", file=sys.stderr)
        figsize = (12.0, 4.0)

    unit = "Gbps" if args.bits else "GBps"

    fig, ax = plt.subplots(figsize=figsize)
    colour_idx = 0
    plotted_any = False

    for file_idx, path in enumerate(args.files):
        if not os.path.isfile(path):
            print(f"Skipping '{path}': not a file.", file=sys.stderr)
            continue

        data = parse_t1(path)
        if not data:
            print(f"Skipping '{path}': no valid t1 lines found.", file=sys.stderr)
            continue

        stem = (args.labels[file_idx]
                if args.labels and file_idx < len(args.labels)
                else os.path.splitext(os.path.basename(path))[0])
        multi_client = len(data) > 1

        for client_id in sorted(data):
            xs, ys = compute_bandwidth(data[client_id], bits=args.bits,
                                       max_seconds=args.duration)
            if not xs:
                print(f"'{path}' client {client_id}: not enough samples "
                      f"to compute bandwidth.", file=sys.stderr)
                continue

            ys = rolling_mean(ys, args.rolling)
            label = build_label(stem, client_id, multi_client)
            colour = PALETTE[colour_idx % len(PALETTE)]
            colour_idx += 1

            ax.plot(xs, ys, marker=".", markersize=6, linewidth=1,
                    color=colour, label=label)
            plotted_any = True

            mean_bw = sum(ys) / len(ys)
            print(f"{label}: {len(ys)} samples, "
                  f"mean = {mean_bw:.3f} {unit}, "
                  f"max = {max(ys):.3f} {unit}, "
                  f"min = {min(ys):.3f} {unit}")

    if not plotted_any:
        print("Nothing to plot.", file=sys.stderr)
        return 1

    ax.set_ylabel(unit)
    ax.xaxis.set_major_formatter(FuncFormatter(hms_formatter))
    ax.grid(True, alpha=0.3, color="lightgrey")

    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.spines["left"].set_color("lightgrey")
    ax.spines["bottom"].set_color("lightgrey")

    if args.title:
        ax.set_title(args.title)

    ncol = max(1, min(colour_idx, 4))
    ax.legend(loc="upper left", bbox_to_anchor=(0, -0.15),
              ncol=ncol, frameon=False)

    fig.tight_layout()

    if args.show:
        plt.show()
    else:
        output = args.output
        if output is None:
            first_stem = os.path.splitext(os.path.basename(args.files[0]))[0]
            output = os.path.join(os.path.dirname(args.files[0]) or ".",
                                  f"{first_stem}_bandwidth.png")
        fig.savefig(output, dpi=150, bbox_inches="tight")
        print(f"Saved figure to {output}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
