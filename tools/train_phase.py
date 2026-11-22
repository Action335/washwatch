"""Trains the washer phase classifier (idle / wash / spin) from the exported Feed and your labels.

Inputs
  data/feed_all.csv  from tools/export_feed.py (one row per 2 s window and washer)
  data/labels.csv    one row per wash you watched (created with the header on first run):
                     date, washer, start, spin_start, done, lid_opened, program, load, notes
                     e.g. 2026-11-23, 1, 14:02, 14:31, 14:43, 14:49, Normal, half,
                     Times are Bangkok local, HH:MM. spin_start may be blank (that cycle's
                     running windows are then left out rather than guessed).

Labels per window of that washer: [start, spin_start) wash (soaks included: the cycle is still
running), [spin_start, done] spin, and --margin minutes before start / after done idle. Windows
outside every labelled cycle and its margins are unobserved and not used.

Features: the window's own std, p2p, vstd, lo, hi plus, over the past 1, 5 and 10 minutes, the
mean and max of vstd, the mean of lo and hi, and the fraction of windows with vstd >= 6 mg. Only
past windows are used, so the gateway can compute the same thing live. The context is what lets a
model tell a quiet soak inside a cycle from an idle washer, which no single-window threshold can.

Model: a decision tree (exported to C if/else, no library, for the ESP32 gateway: E7). It is
tested on whole cycles it did not train on (GroupKFold by cycle) and compared with the firmware's
current threshold rule on the same windows.

Outputs in data/model/: report.md, phase_model.h, and with --plot one PNG per labelled cycle.
--install also copies phase_model.h to gateway/src/.

    uv run --no-project --with pandas --with scikit-learn --with matplotlib python -I tools/train_phase.py [--plot] [--install]
"""
import argparse
import re
import shutil
import sys
from datetime import datetime
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
TZ = "Asia/Bangkok"
IDLE, WASH, SPIN, UNKNOWN = 0, 1, 2, -1  # same numbers as PHASE_* in lib/ww_proto/src/ww_msg.h
CLASS_NAMES = {IDLE: "idle", WASH: "wash", SPIN: "spin"}
MOVING_MG = 6  # LOG_MOVING_VSTD_MG in lib/ww_proto/src/batch_json.h
BASE = ["std", "p2p", "vstd", "lo", "hi"]
CONTEXT_WINDOWS = {"1m": "60s", "5m": "300s", "10m": "600s"}
FEATURES = BASE + [
    f"{name}_{w}"
    for w in CONTEXT_WINDOWS
    for name in ("vstd_mean", "vstd_max", "lo_mean", "hi_mean", "moving_frac")
]
LABEL_COLUMNS = ["date", "washer", "start", "spin_start", "done", "lid_opened", "program", "load", "notes"]


# --- data ---------------------------------------------------------------------------------------

def prepare(feed: pd.DataFrame) -> pd.DataFrame:
    """Rows with all five vibration fields (older v2 rows lack vstd/lo/hi), sorted by washer, time."""
    df = feed.copy()
    df["dt"] = pd.to_datetime(df["ts_ms"].astype("int64"), unit="ms", utc=True).dt.tz_convert(TZ)
    for c in BASE:
        df[c] = pd.to_numeric(df[c], errors="coerce")
    df["washer"] = df["washer"].astype(int)
    df = df.dropna(subset=BASE)
    return df.sort_values(["washer", "dt"]).reset_index(drop=True)


def _at(date: str, hhmm, after=None):
    if hhmm is None or (isinstance(hhmm, float) and np.isnan(hhmm)) or str(hhmm).strip() == "":
        return pd.NaT
    t = pd.Timestamp(f"{str(date).strip()} {str(hhmm).strip()}", tz=TZ)
    if after is not None and not pd.isna(after) and t < after:
        t += pd.Timedelta(days=1)  # the cycle crossed midnight
    return t


def parse_labels(raw: pd.DataFrame) -> pd.DataFrame:
    rows = []
    for _, r in raw.iterrows():
        if str(r.get("date", "")).strip() in ("", "nan") or str(r.get("start", "")).strip() in ("", "nan"):
            continue
        start = _at(r["date"], r["start"])
        spin = _at(r["date"], r.get("spin_start"), after=start)
        done = _at(r["date"], r.get("done"), after=start)
        if pd.isna(done):
            continue  # an unfinished row: nothing reliable to label
        washer = int(r["washer"])
        rows.append({"washer": washer, "start": start, "spin_start": spin, "done": done,
                     "cycle_id": f"{start:%Y-%m-%d_%H%M}_w{washer}"})
    return pd.DataFrame(rows, columns=["washer", "start", "spin_start", "done", "cycle_id"])


def label_windows(feed: pd.DataFrame, labels: pd.DataFrame, margin_min: float = 30):
    """(y, group): a label per window (UNKNOWN if unobserved) and the cycle it belongs to."""
    y = pd.Series(UNKNOWN, index=feed.index)
    group = pd.Series("", index=feed.index)
    margin = pd.Timedelta(minutes=margin_min)
    t = feed["dt"]
    # Margins first, so a neighbouring cycle's running windows always win over another's idle margin.
    for _, c in labels.iterrows():
        mine = feed["washer"] == c["washer"]
        idle = mine & (((t >= c["start"] - margin) & (t < c["start"])) | ((t > c["done"]) & (t <= c["done"] + margin)))
        y[idle] = IDLE
        group[idle] = c["cycle_id"]
    for _, c in labels.iterrows():
        mine = feed["washer"] == c["washer"]
        running = mine & (t >= c["start"]) & (t <= c["done"])
        group[running] = c["cycle_id"]
        if pd.isna(c["spin_start"]):
            y[running] = UNKNOWN  # wash or spin unknown: leave it out rather than guess
        else:
            y[running & (t < c["spin_start"])] = WASH
            y[running & (t >= c["spin_start"])] = SPIN
    return y, group


def add_context(feed: pd.DataFrame) -> pd.DataFrame:
    """Adds the context features: past-only, time-based windows per washer."""
    parts = []
    for _, sub in feed.groupby("washer", sort=False):
        s = sub.set_index("dt")
        moving = (s["vstd"] >= MOVING_MG).astype(float)
        out = sub.copy()
        for name, span in CONTEXT_WINDOWS.items():
            out[f"vstd_mean_{name}"] = s["vstd"].rolling(span).mean().to_numpy()
            out[f"vstd_max_{name}"] = s["vstd"].rolling(span).max().to_numpy()
            out[f"lo_mean_{name}"] = s["lo"].rolling(span).mean().to_numpy()
            out[f"hi_mean_{name}"] = s["hi"].rolling(span).mean().to_numpy()
            out[f"moving_frac_{name}"] = moving.rolling(span).mean().to_numpy()
        parts.append(out)
    return pd.concat(parts).loc[feed.index]


def firmware_rule_thresholds() -> tuple[float, float]:
    """STD_WASH_MG and STD_SPIN_MG from the firmware, so the baseline is the rule actually flashed."""
    text = (ROOT / "lib" / "ww_proto" / "src" / "features.h").read_text(encoding="utf-8")
    wash = float(re.search(r"STD_WASH_MG\s*=\s*(\d+)", text).group(1))
    spin = float(re.search(r"STD_SPIN_MG\s*=\s*(\d+)", text).group(1))
    return wash, spin


def rule_baseline(df: pd.DataFrame) -> np.ndarray:
    wash, spin = firmware_rule_thresholds()
    return np.where(df["std"] >= spin, SPIN, np.where(df["std"] >= wash, WASH, IDLE))


# --- model --------------------------------------------------------------------------------------

def scores(y_true, y_pred) -> dict:
    from sklearn.metrics import accuracy_score, confusion_matrix, precision_recall_fscore_support

    classes = [IDLE, WASH, SPIN]
    p, r, f, n = precision_recall_fscore_support(y_true, y_pred, labels=classes, zero_division=0)
    return {
        "accuracy": float(accuracy_score(y_true, y_pred)),
        "macro_f1": float(np.mean([f[i] for i in range(3) if n[i] > 0])) if n.sum() else 0.0,
        "per_class": {CLASS_NAMES[c]: {"precision": float(p[i]), "recall": float(r[i]), "f1": float(f[i]), "n": int(n[i])}
                      for i, c in enumerate(classes)},
        "confusion": confusion_matrix(y_true, y_pred, labels=classes).tolist(),
    }


def run(feed_raw: pd.DataFrame, labels_raw: pd.DataFrame, max_depth: int = 5, margin_min: float = 30) -> dict:
    from sklearn.model_selection import GroupKFold, cross_val_predict
    from sklearn.tree import DecisionTreeClassifier

    labels = parse_labels(labels_raw)
    if labels.empty:
        raise SystemExit("No labelled cycles: add one row per watched wash to data/labels.csv "
                         "(date, washer, start, spin_start, done, ...).")
    feed = add_context(prepare(feed_raw))
    y, group = label_windows(feed, labels, margin_min)
    keep = (y != UNKNOWN).to_numpy()
    X, yk, gk = feed.loc[keep, FEATURES], y[keep].to_numpy(), group[keep].to_numpy()
    if len(yk) == 0:
        raise SystemExit("The labelled cycles have no matching windows in feed_all.csv "
                         "(wrong washer or date, or the export is older than the labels?).")
    X = X.fillna(0.0)
    cycles = sorted(set(gk))

    def make():
        return DecisionTreeClassifier(max_depth=max_depth, min_samples_leaf=10, class_weight="balanced", random_state=0)

    model = make().fit(X, yk)
    res = {"model": model, "X": X, "y": yk, "groups": gk, "n_cycles": len(cycles), "n_windows": int(len(yk)),
           "label_counts": {CLASS_NAMES[c]: int((yk == c).sum()) for c in (IDLE, WASH, SPIN)},
           "unlabelled_running": int(((y == UNKNOWN) & (group != "")).sum()),
           "baseline": scores(yk, rule_baseline(feed.loc[keep])), "cv": None, "per_cycle": {}, "note": ""}
    if len(cycles) < 2:
        res["note"] = ("Only one labelled cycle: need at least 2 to test on a cycle the model has not seen. "
                       "The model below is trained but not evaluated.")
        return res
    pred = cross_val_predict(make(), X, yk, groups=gk, cv=GroupKFold(n_splits=min(5, len(cycles))))
    res["cv"] = scores(yk, pred)
    res["per_cycle"] = {c: float((pred[gk == c] == yk[gk == c]).mean()) for c in cycles}
    return res


def _float_le_threshold(thr: float) -> np.float32:
    """Largest float32 t with t <= thr: for float32 x, (x <= thr) == (x <= t), so C can compare in float."""
    t = np.float32(thr)
    if float(t) > thr:
        t = np.nextafter(t, np.float32(-np.inf))
    return t


def export_c(model, features) -> str:
    tree = model.tree_
    lines = []

    def emit(node, depth):
        pad = "  " * depth
        if tree.children_left[node] == -1:
            cls = int(model.classes_[int(np.argmax(tree.value[node][0]))])
            lines.append(f"{pad}return {cls};  // {CLASS_NAMES.get(cls, cls)}")
            return
        f = int(tree.feature[node])
        t = _float_le_threshold(float(tree.threshold[node]))
        lit = f"{float(t):.9g}"  # 9 digits round-trip a float32
        if not any(ch in lit for ch in ".en"):
            lit += ".0"  # "10f" is not a valid C literal
        lines.append(f"{pad}if (f[{f}] <= {lit}f) {{  // {features[f]}")
        emit(tree.children_left[node], depth + 1)
        lines.append(f"{pad}}} else {{")
        emit(tree.children_right[node], depth + 1)
        lines.append(f"{pad}}}")

    emit(0, 1)
    order = "\n".join(f"//   f[{i}] {name}" for i, name in enumerate(features))
    return f"""// GENERATED by tools/train_phase.py on {datetime.now():%Y-%m-%d %H:%M}. Do not edit; retrain instead.
// Washer phase classifier: decision tree, depth {model.get_depth()}, {model.get_n_leaves()} leaves.
// Returns 0 idle, 1 wash, 2 spin (PHASE_* in ww_msg.h). Inputs, in this order:
{order}
#pragma once
#include <stdint.h>

#define PHASE_MODEL_N_FEATURES {len(features)}

static inline uint8_t phase_model_predict(const float f[PHASE_MODEL_N_FEATURES]) {{
{chr(10).join(lines)}
}}
"""


# --- report -------------------------------------------------------------------------------------

def _metrics_table(s: dict) -> str:
    rows = ["| class | precision | recall | F1 | windows |", "|---|---|---|---|---|"]
    for name, m in s["per_class"].items():
        rows.append(f"| {name} | {m['precision']:.3f} | {m['recall']:.3f} | {m['f1']:.3f} | {m['n']} |")
    cm = s["confusion"]
    rows += ["", "Confusion (rows = true, columns = predicted: idle, wash, spin):", "",
             "| | idle | wash | spin |", "|---|---|---|---|"]
    rows += [f"| {n} | {r[0]} | {r[1]} | {r[2]} |" for n, r in zip(("idle", "wash", "spin"), cm)]
    return "\n".join(rows)


def report(res: dict, args) -> str:
    from sklearn.tree import export_text

    m = res["model"]
    imp = sorted(zip(FEATURES, m.feature_importances_), key=lambda x: -x[1])
    lines = [f"# Phase classifier report ({datetime.now():%Y-%m-%d %H:%M})", "",
             f"- Labelled cycles: **{res['n_cycles']}**; labelled windows: {res['n_windows']} "
             f"({', '.join(f'{k} {v}' for k, v in res['label_counts'].items())})",
             f"- Running windows left out (cycle without spin_start): {res['unlabelled_running']}",
             f"- Model: decision tree, max depth {args.max_depth}, actual depth {m.get_depth()}, {m.get_n_leaves()} leaves; "
             f"idle margin {args.margin} min", ""]
    if res["note"]:
        lines += [f"> {res['note']}", ""]
    if res["cv"]:
        cv, base = res["cv"], res["baseline"]
        lines += ["## Tested on unseen cycles (GroupKFold by cycle)", "",
                  f"**Accuracy {cv['accuracy']:.3f}, macro F1 {cv['macro_f1']:.3f}** "
                  f"vs the firmware threshold rule on the same windows: accuracy {base['accuracy']:.3f}, "
                  f"macro F1 {base['macro_f1']:.3f}.", "", _metrics_table(cv), "",
                  "Per cycle (accuracy when that cycle was held out): " + ", ".join(
                      f"{c} {a:.2f}" for c, a in res["per_cycle"].items()) + "", "",
                  "## Firmware threshold rule (baseline)", "", _metrics_table(base), ""]
    lines += ["## Feature importance", "", "| feature | importance |", "|---|---|"]
    lines += [f"| {n} | {v:.3f} |" for n, v in imp if v > 0]
    lines += ["", "## Tree", "", "```", export_text(m, feature_names=FEATURES, decimals=2), "```"]
    return "\n".join(lines) + "\n"


def plot_cycles(feed_raw, labels_raw, out_dir: Path, margin_min: float) -> int:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    feed, labels = prepare(feed_raw), parse_labels(labels_raw)
    out_dir.mkdir(parents=True, exist_ok=True)
    margin = pd.Timedelta(minutes=margin_min)
    for _, c in labels.iterrows():
        sub = feed[(feed["washer"] == c["washer"]) & (feed["dt"] >= c["start"] - margin) & (feed["dt"] <= c["done"] + margin)]
        fig, ax = plt.subplots(figsize=(12, 4))
        for col in ("vstd", "lo", "hi"):
            ax.plot(sub["dt"], sub[col], lw=0.8, label=col)
        ax.axvspan(c["start"], c["done"], color="0.9", zorder=0, label="cycle")
        if not pd.isna(c["spin_start"]):
            ax.axvline(c["spin_start"], color="red", lw=1, label="spin_start")
        ax.set_title(f"{c['cycle_id']}  (mg; shaded = start..done)")
        ax.set_yscale("symlog", linthresh=10)
        ax.legend(loc="upper left", fontsize=8)
        fig.autofmt_xdate()
        fig.tight_layout()
        fig.savefig(out_dir / f"{c['cycle_id']}.png", dpi=110)
        plt.close(fig)
    return len(labels)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--feed", type=Path, default=DATA / "feed_all.csv")
    ap.add_argument("--labels", type=Path, default=DATA / "labels.csv")
    ap.add_argument("--out", type=Path, default=DATA / "model")
    ap.add_argument("--max-depth", type=int, default=5, help="small trees generalise better and fit the ESP32 easily")
    ap.add_argument("--margin", type=float, default=30, help="minutes before start / after done labelled idle")
    ap.add_argument("--plot", action="store_true", help="one PNG per labelled cycle, to check labels or read spin start")
    ap.add_argument("--install", action="store_true", help="copy phase_model.h into gateway/src/")
    args = ap.parse_args(argv)

    if not args.labels.exists():
        args.labels.parent.mkdir(parents=True, exist_ok=True)
        pd.DataFrame(columns=LABEL_COLUMNS).to_csv(args.labels, index=False)
        raise SystemExit(f"Created {args.labels} with the column names: add one row per watched wash, then run again.")
    feed_raw = pd.read_csv(args.feed)
    labels_raw = pd.read_csv(args.labels, dtype=str, keep_default_na=False)
    if args.plot:
        print(f"plotted {plot_cycles(feed_raw, labels_raw, args.out / 'cycles', args.margin)} cycles into {args.out / 'cycles'}")
    res = run(feed_raw, labels_raw, args.max_depth, args.margin)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "report.md").write_text(report(res, args), encoding="utf-8")
    header = args.out / "phase_model.h"
    header.write_text(export_c(res["model"], FEATURES), encoding="utf-8")
    if res["cv"]:
        print(f"{res['n_cycles']} cycles: accuracy {res['cv']['accuracy']:.3f} on unseen cycles "
              f"(threshold rule {res['baseline']['accuracy']:.3f})")
    else:
        print(res["note"])
    print(f"report: {args.out / 'report.md'}  model: {header}")
    if args.install:
        shutil.copy(header, ROOT / "gateway" / "src" / "phase_model.h")
        print("installed gateway/src/phase_model.h")
    return 0


if __name__ == "__main__":
    sys.exit(main())
