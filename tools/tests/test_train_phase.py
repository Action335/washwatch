"""Tests for tools/train_phase.py on synthetic cycles (no NETPIE, no real labels needed).

Run: uv run --no-project --with pytest --with pandas --with scikit-learn --with matplotlib python -I -m pytest tools/tests -q
"""
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import train_phase as tp  # noqa: E402

T0 = pd.Timestamp("2026-11-23 08:00", tz=tp.TZ)


def synth_feed(cycles, washer=1, seed=0, hours=10):
    """2 s windows, shaped like the 7 Nov recording: quiet idle, wash with a quiet soak, loud spin."""
    rng = np.random.default_rng(seed)
    t = pd.date_range(T0, T0 + pd.Timedelta(hours=hours), freq="2s")
    n = len(t)
    vstd = rng.normal(3, 0.5, n)
    lo = rng.normal(1, 0.3, n)
    hi = rng.normal(1, 0.3, n)
    for start, spin, done in cycles:
        wash = (t >= start) & (t < spin)
        # Agitation comes in bursts of 40 s with 20 s pauses, and a 7-minute soak in the middle.
        sec = (t - start).total_seconds()
        soak = wash & (sec > 900) & (sec < 1320)
        burst = wash & ~soak & ((sec % 60) < 40)
        vstd[burst] = rng.normal(9, 2, burst.sum())
        hi[burst] = rng.normal(6, 1.5, burst.sum())
        lo[burst] = rng.normal(2, 0.5, burst.sum())
        spinning = (t >= spin) & (t <= done)
        vstd[spinning] = rng.normal(40, 6, spinning.sum())
        lo[spinning] = rng.normal(30, 5, spinning.sum())
        hi[spinning] = rng.normal(8, 2, spinning.sum())
    vstd, lo, hi = (np.clip(np.round(x), 0, None) for x in (vstd, lo, hi))
    return pd.DataFrame({
        "time": t.map(lambda x: x.isoformat()),
        # Not t.asi8: its unit depends on the pandas version's default resolution.
        "ts_ms": (t - pd.Timestamp(0, tz="UTC")) // pd.Timedelta(milliseconds=1),
        "washer": washer,
        "std": np.round(vstd / 2),
        "p2p": np.round(vstd * 4),
        "vstd": vstd, "lo": lo, "hi": hi,
        "state": 0, "lid": 0,
    })


def cycle_times(k, washer_offset_h=0.0):
    """k cycles, 90 min apart, each 40 min wash + 8 min spin."""
    out = []
    for i in range(k):
        s = T0 + pd.Timedelta(minutes=40 + 90 * i) + pd.Timedelta(hours=washer_offset_h)
        out.append((s, s + pd.Timedelta(minutes=40), s + pd.Timedelta(minutes=48)))
    return out


def labels_for(cycles, washer=1):
    rows = []
    for s, spin, done in cycles:
        rows.append({"date": s.strftime("%Y-%m-%d"), "washer": washer, "start": s.strftime("%H:%M"),
                     "spin_start": spin.strftime("%H:%M"), "done": done.strftime("%H:%M"),
                     "lid_opened": "", "program": "", "load": "", "notes": ""})
    return pd.DataFrame(rows)


def test_parse_labels_builds_timestamps_and_ids():
    lab = tp.parse_labels(pd.DataFrame([
        {"date": "2026-11-23", "washer": 2, "start": "14:02", "spin_start": "14:31", "done": "14:43", "lid_opened": "14:49"},
        {"date": "2026-11-23", "washer": 1, "start": "23:50", "spin_start": "", "done": "00:40", "lid_opened": ""},
    ]))
    assert lab.loc[0, "start"] == pd.Timestamp("2026-11-23 14:02", tz=tp.TZ)
    assert lab.loc[0, "cycle_id"] != lab.loc[1, "cycle_id"]
    assert pd.isna(lab.loc[1, "spin_start"])
    assert lab.loc[1, "done"] == pd.Timestamp("2026-11-24 00:40", tz=tp.TZ)  # crosses midnight


def test_label_windows_rules():
    cycles = cycle_times(1)
    feed = tp.prepare(synth_feed(cycles))
    lab = tp.parse_labels(labels_for(cycles))
    y, group = tp.label_windows(feed, lab, margin_min=30)
    s, spin, done = cycles[0]
    at = lambda ts: feed.index[feed["dt"].searchsorted(ts)]  # noqa: E731
    assert y[at(s + pd.Timedelta(minutes=5))] == tp.WASH
    assert y[at(s + pd.Timedelta(minutes=18))] == tp.WASH  # quiet soak is still "running"
    assert y[at(spin + pd.Timedelta(minutes=2))] == tp.SPIN
    assert y[at(s - pd.Timedelta(minutes=10))] == tp.IDLE
    assert y[at(done + pd.Timedelta(minutes=10))] == tp.IDLE
    assert y[at(s - pd.Timedelta(minutes=35))] == tp.UNKNOWN  # outside the margin: unobserved
    assert group[at(s + pd.Timedelta(minutes=5))] == group[at(s - pd.Timedelta(minutes=10))]


def test_cycle_without_spin_time_is_not_guessed():
    cycles = cycle_times(1)
    feed = tp.prepare(synth_feed(cycles))
    lab = labels_for(cycles)
    lab.loc[0, "spin_start"] = ""
    y, _ = tp.label_windows(feed, tp.parse_labels(lab), margin_min=30)
    s, spin, done = cycles[0]
    i = feed.index[feed["dt"].searchsorted(spin + pd.Timedelta(minutes=2))]
    assert y[i] == tp.UNKNOWN


def test_other_washer_is_not_labelled():
    cycles = cycle_times(1)
    feed = tp.prepare(pd.concat([synth_feed(cycles, washer=1), synth_feed([], washer=2, seed=1)]))
    y, _ = tp.label_windows(feed, tp.parse_labels(labels_for(cycles, washer=1)), margin_min=30)
    assert (y[feed["washer"] == 2] == tp.UNKNOWN).all()


def test_context_features_are_causal():
    feed = tp.prepare(synth_feed(cycle_times(1)))
    a = tp.add_context(feed)
    later = feed.copy()
    later.loc[later.index[-100:], "vstd"] = 999  # change only the future
    b = tp.add_context(later)
    cut = a.index[:-100]
    pd.testing.assert_frame_equal(a.loc[cut, tp.FEATURES], b.loc[cut, tp.FEATURES])


def test_end_to_end_beats_the_threshold_rule_on_unseen_cycles():
    cycles = cycle_times(6)
    feed = synth_feed(cycles, hours=10)
    res = tp.run(feed, labels_for(cycles), max_depth=5)
    assert res["n_cycles"] == 6
    assert res["cv"]["accuracy"] > 0.9
    assert res["cv"]["accuracy"] > res["baseline"]["accuracy"]


def test_too_few_cycles_is_reported_not_crashed():
    cycles = cycle_times(1)
    res = tp.run(synth_feed(cycles), labels_for(cycles), max_depth=5)
    assert res["cv"] is None and "at least 2" in res["note"]


def test_no_labels_says_what_to_do():
    with pytest.raises(SystemExit) as e:
        tp.run(synth_feed([]), labels_for([]), max_depth=5)
    assert "labels" in str(e.value)


@pytest.mark.skipif(shutil.which("gcc") is None, reason="needs gcc")
def test_exported_c_matches_sklearn(tmp_path):
    cycles = cycle_times(4)
    res = tp.run(synth_feed(cycles, hours=7), labels_for(cycles), max_depth=6)
    header = tmp_path / "phase_model.h"
    header.write_text(tp.export_c(res["model"], tp.FEATURES), encoding="utf-8")
    X = res["X"].to_numpy(dtype=np.float32)
    X = X[np.random.default_rng(0).choice(len(X), 400, replace=False)]
    rows = ",\n".join("{" + ",".join(f"(float){v:.9g}" for v in r) + "}" for r in X)
    main = tmp_path / "main.c"
    main.write_text(f"""#include <stdio.h>
#include <stdint.h>
#include "phase_model.h"
static const float X[][{len(tp.FEATURES)}] = {{{rows}}};
int main(void) {{ for (unsigned i = 0; i < sizeof X / sizeof X[0]; i++) printf("%u\\n", phase_model_predict(X[i])); return 0; }}
""", encoding="utf-8")
    exe = tmp_path / "m.exe"
    subprocess.run(["gcc", "-std=c99", "-Wall", "-Werror", "-o", str(exe), str(main)], check=True)
    got = [int(x) for x in subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout.split()]
    want = res["model"].predict(pd.DataFrame(X, columns=tp.FEATURES)).tolist()
    assert got == want
