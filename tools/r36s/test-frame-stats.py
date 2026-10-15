#!/usr/bin/env python3
"""Guard performance gate against hidden slow scenes and malformed captures."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("frame_stats", Path(__file__).with_name("frame-stats.py"))
stats = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stats)


def frames(parts):
    rows = []
    timestamp = 1_000_000_000
    for fps, seconds in parts:
        interval = round(1e9 / fps)
        for _ in range(round(fps * seconds)):
            timestamp += interval
            rows.append(dict(timestamp_ns=timestamp, interval_ns=interval,
                             work_ns=interval - 1, swap_ns=1, cpu_ns=interval // 2))
    return rows


class FrameStatsTests(unittest.TestCase):
    def test_constant_30_passes_boundary(self):
        result = stats.analyse(frames([(30, 61)]))
        self.assertTrue(result["timing_gate_pass"])
        self.assertEqual(result["min_5s_fps"], 30)

    def test_fast_run_cannot_hide_slow_scene(self):
        result = stats.analyse(frames([(60, 30), (20, 10), (60, 30)]))
        self.assertGreater(result["average_fps"], 30)
        self.assertEqual(result["min_5s_fps"], 20)
        self.assertFalse(result["timing_gate_pass"])

    def test_boundary_crossing_dip_detected(self):
        result = stats.analyse(frames([(60, 29.5), (15, 2), (60, 30.5)]))
        self.assertGreater(result["min_5s_fps"], 30)
        self.assertGreater(result["longest_below_target_1s_sample_run_s"], 1)
        self.assertFalse(result["timing_gate_pass"])

    def test_single_brief_stall_recorded(self):
        result = stats.analyse(frames([(60, 30), (2, 0.5), (60, 30)]))
        self.assertEqual(result["frame_ms_max"], 500)
        self.assertTrue(result["timing_gate_pass"])

    def test_short_capture_fails(self):
        self.assertFalse(stats.analyse(frames([(60, 10)]))["timing_gate_pass"])

    def test_invalid_capture_rejected(self):
        header = "timestamp_ns,interval_ns,work_ns,swap_ns,cpu_ns\n"
        for body in ["", "overflow\n", "10,0,0,0,0\n", "10,5,4,0,1\n",
                     "10,5,4,1,1\n20,5,4,1,1\n"]:
            with self.subTest(body=body), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "frames.csv"
                path.write_text(header + body)
                with self.assertRaises(ValueError):
                    stats.read_frames(path)


if __name__ == "__main__":
    unittest.main()
