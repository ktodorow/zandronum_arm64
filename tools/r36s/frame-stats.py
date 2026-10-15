#!/usr/bin/env python3
"""Check diagnostic frame CSVs; scenes/active gameplay still require validation."""
import argparse
import bisect
import csv
import hashlib
import json
import math
from pathlib import Path


def percentile(values, fraction):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)]


def read_frames(path):
    rows = []
    with path.open(newline="") as stream:
        reader = csv.DictReader(stream)
        expected = ["timestamp_ns", "interval_ns", "work_ns", "swap_ns", "cpu_ns"]
        if reader.fieldnames != expected:
            raise ValueError("unexpected CSV header")
        previous = None
        for row in reader:
            if None in row or any(row.get(key) is None for key in expected):
                raise ValueError("incomplete/overflowed recording")
            values = {key: int(row[key]) for key in expected}
            timestamp, interval = values["timestamp_ns"], values["interval_ns"]
            if interval <= 0 or timestamp < interval or min(values.values()) < 0:
                raise ValueError("invalid frame interval")
            if previous is not None and timestamp - previous != interval:
                raise ValueError("non-contiguous frame timestamps")
            if values["work_ns"] + values["swap_ns"] != interval:
                raise ValueError("inconsistent work/swap interval")
            rows.append(values)
            previous = timestamp
    if not rows:
        raise ValueError("empty recording")
    return rows


def windows(ends, width):
    step = 100_000_000
    count = (ends[-1] - width) // step + 1
    if count <= 0:
        raise ValueError("recording shorter than rolling window")
    # Frame completions in (start, end]; no average-of-instantaneous-FPS bias.
    return [(bisect.bisect_right(ends, i * step + width)
             - bisect.bisect_right(ends, i * step)) * 1e9 / width
            for i in range(count)]


def analyse(rows, min_duration=60.0, target=30.0):
    origin = rows[0]["timestamp_ns"] - rows[0]["interval_ns"]
    ends = [row["timestamp_ns"] - origin for row in rows]
    duration = ends[-1] / 1e9
    one = windows(ends, 1_000_000_000)
    five = windows(ends, 5_000_000_000)
    bad = [fps < target for fps in one]
    longest = consecutive = 0
    low_runs = []
    run_start = None
    for value in bad:
        consecutive = consecutive + 1 if value else 0
        longest = max(longest, consecutive)
    for index, value in enumerate(bad + [False]):
        if value and run_start is None:
            run_start = index
        elif not value and run_start is not None:
            low_runs.append({
                "first_window_end_s": round(1 + run_start * 0.1, 3),
                "last_window_end_s": round(1 + (index - 1) * 0.1, 3),
                "sample_run_s": round((index - run_start) * 0.1, 3),
                "min_fps": min(one[run_start:index]),
            })
            run_start = None
    frame_ms = [row["interval_ns"] / 1e6 for row in rows]
    slow_episodes = []
    episode = 0.0
    worst = 0.0
    for index, interval in enumerate(frame_ms + [0.0]):
        if interval > 1000.0 / target:
            episode += interval
            worst = max(worst, interval)
        elif episode:
            slow_episodes.append({
                "end_s": round(ends[index - 1] / 1e9, 6),
                "duration_ms": round(episode, 3),
                "worst_frame_ms": round(worst, 3),
            })
            episode = 0.0
            worst = 0.0
    bad_fraction = sum(bad) / len(bad)
    # Declare policy before measuring: every rolling 5 s >= target, low 1 s
    # samples <=5%, no run of those samples lasting >1 s (100 ms grid).
    passed = (duration >= min_duration and min(five) >= target
              and bad_fraction <= 0.05 and longest <= 10)
    return {
        "frames": len(rows),
        "duration_s": round(duration, 6),
        "average_fps": round(len(rows) / duration, 3),
        "rolling_step_s": 0.1,
        "min_1s_fps": min(one),
        "min_5s_fps": min(five),
        "p05_1s_fps": percentile(one, 0.05),
        "below_target_1s_percent": round(100 * bad_fraction, 3),
        "longest_below_target_1s_sample_run_s": round(longest * 0.1, 3),
        "frame_ms_p50": round(percentile(frame_ms, 0.50), 3),
        "frame_ms_p95": round(percentile(frame_ms, 0.95), 3),
        "frame_ms_p99": round(percentile(frame_ms, 0.99), 3),
        "frame_ms_max": round(max(frame_ms), 3),
        "slow_frame_count": sum(ms > 1000.0 / target for ms in frame_ms),
        "longest_slow_frame_episode_ms": max((s["duration_ms"] for s in slow_episodes), default=0),
        "longest_slow_frame_episodes": sorted(slow_episodes, key=lambda s: s["duration_ms"], reverse=True)[:10],
        "below_target_1s_runs": low_runs,
        "mean_main_thread_cpu_ms": round(sum(r["cpu_ns"] for r in rows) / len(rows) / 1e6, 3),
        "mean_swap_ms": round(sum(r["swap_ns"] for r in rows) / len(rows) / 1e6, 3),
        "target_fps": target,
        "minimum_duration_s": min_duration,
        "timing_gate_pass": passed,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--min-duration", type=float, default=60.0)
    parser.add_argument("--target", type=float, default=30.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.min_duration < 5 or not math.isfinite(args.min_duration):
        parser.error("minimum duration must be finite and >=5 seconds")
    if args.target <= 0 or not math.isfinite(args.target):
        parser.error("target must be finite and positive")
    try:
        result = analyse(read_frames(args.csv), args.min_duration, args.target)
    except (ValueError, KeyError) as error:
        parser.error(str(error))
    result["csv_sha256"] = hashlib.sha256(args.csv.read_bytes()).hexdigest()
    output = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(output)
    print(output, end="")
    return 0 if result["timing_gate_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
