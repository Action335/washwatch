"""Exports the WashWatch training log from the NETPIE Feed into data/feed_all.csv.

The Feed deletes points older than the datatag TTL (1 week), so run this at least every few days;
the Windows scheduled task "WashWatch Feed export" runs it daily. Each run fetches the last
--days days and merges them into the CSV, so overlapping runs never duplicate a window and rows
that have already expired on NETPIE stay in the file.

One row per (time, washer): time (Bangkok), ts_ms, washer, std, p2p, vstd, lo, hi, state, lid.

    python -I tools/export_feed.py            # last 8 days into data/feed_all.csv
    python -I tools/export_feed.py --days 2

Credentials: the NETPIE_CLIENT_ID and NETPIE_TOKEN environment variables (read from the per-user
environment too, so a shell opened before they were set still works). They only go to NETPIE.
"""
import argparse
import csv
import json
import os
import sys
import time
import urllib.request
from datetime import datetime, timedelta, timezone
from pathlib import Path

QUERY_URL = "https://ds.netpie.io/feed/api/v1/datapoints/query"
FIELDS = ["std", "p2p", "vstd", "lo", "hi", "state", "lid"]
WASHERS = [1, 2]
KEYS = [f"w{w}_{f}" for w in WASHERS for f in FIELDS]
COLUMNS = ["time", "ts_ms", "washer"] + FIELDS
CRED_VARS = ("NETPIE_CLIENT_ID", "NETPIE_TOKEN")
BANGKOK = timezone(timedelta(hours=7))
CHUNK_MS = 6 * 3600 * 1000  # full-rate logging is ~10,800 points per key in 6 h
DEFAULT_OUT = Path(__file__).resolve().parents[1] / "data" / "feed_all.csv"


def user_environment() -> dict:
    """The per-user environment variables (HKCU\\Environment) on Windows; {} elsewhere."""
    try:
        import winreg
    except ImportError:
        return {}
    out = {}
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment") as key:
            for name in CRED_VARS:
                try:
                    out[name] = winreg.QueryValueEx(key, name)[0]
                except OSError:
                    pass
    except OSError:
        pass
    return out


def credentials(env, user_env=None) -> tuple[str, str]:
    if user_env is None:
        user_env = user_environment()
    values = [env.get(n) or user_env.get(n) for n in CRED_VARS]
    missing = [n for n, v in zip(CRED_VARS, values) if not v]
    if missing:
        raise SystemExit("Set " + " and ".join(missing) + " (the gateway device's Client ID and Token).")
    return values[0], values[1]


def chunks(start_ms: int, end_ms: int, size_ms: int):
    """Inclusive [from, to] ranges covering start..end with no overlap."""
    t = start_ms
    while t <= end_ms:
        yield t, min(t + size_ms - 1, end_ms)
        t += size_ms


def query_body(client_id: str, keys, start_ms: int, end_ms: int) -> dict:
    return {
        "start_absolute": start_ms,
        "end_absolute": end_ms,
        "metrics": [{"name": client_id, "tags": {"attr": list(keys)}, "group_by": [{"name": "tag", "tags": ["attr"]}]}],
    }


def parse(resp: dict) -> dict:
    """{key: [(ts_ms, value), ...]} for every key that has points."""
    out = {}
    for q in resp.get("queries", []):
        for r in q.get("results", []):
            attr = r.get("tags", {}).get("attr")
            values = r.get("values") or []
            if not attr or not values:
                continue
            out.setdefault(attr[0], []).extend((int(ts), v) for ts, v in values)
    return out


def fetch(client_id: str, token: str, start_ms: int, end_ms: int) -> dict:
    points = {}
    for a, b in chunks(start_ms, end_ms, CHUNK_MS):
        body = json.dumps(query_body(client_id, KEYS, a, b)).encode()
        req = urllib.request.Request(QUERY_URL, data=body, method="POST", headers={
            "Authorization": f"Device {client_id}:{token}",
            "Content-Type": "application/json",
        })
        with urllib.request.urlopen(req, timeout=60) as resp:
            for k, v in parse(json.load(resp)).items():
                points.setdefault(k, []).extend(v)
    return points


def to_rows(points: dict) -> list[dict]:
    """One row per (ts_ms, washer); fields with no point in that window are ''."""
    rows = {}
    for key, values in points.items():
        washer, field = key[1:].split("_", 1)
        if field not in FIELDS:
            continue
        for ts, v in values:
            row = rows.setdefault((ts, int(washer)), None)
            if row is None:
                row = {c: "" for c in COLUMNS}
                row["time"] = datetime.fromtimestamp(ts / 1000, BANGKOK).isoformat(timespec="milliseconds")
                row["ts_ms"] = ts
                row["washer"] = int(washer)
                rows[(ts, int(washer))] = row
            row[field] = v
    return [rows[k] for k in sorted(rows)]


def merge_into(path: Path, new_rows) -> tuple[int, int]:
    """Merges rows into the CSV at path (newer non-empty values win). Returns (added, total)."""
    existing = {}
    if path.exists():
        with path.open(newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                existing[(int(r["ts_ms"]), int(r["washer"]))] = r
    added = 0
    for r in new_rows:
        k = (int(r["ts_ms"]), int(r["washer"]))
        old = existing.get(k)
        if old is None:
            existing[k] = {c: r[c] for c in COLUMNS}
            added += 1
        else:
            for c in FIELDS:
                if r[c] != "":
                    old[c] = r[c]
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp")
    with tmp.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        for k in sorted(existing):
            w.writerow(existing[k])
    tmp.replace(path)  # a crash mid-write never leaves a half-written feed_all.csv
    return added, len(existing)


def main(argv=None) -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--days", type=float, default=8, help="how far back to fetch (default 8: covers the 1-week TTL)")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--log", type=Path, default=DEFAULT_OUT.with_name("export_log.txt"),
                    help="every run appends one line here (the scheduled task has no console)")
    args = ap.parse_args(argv)
    stamp = datetime.now(BANGKOK).strftime("%Y-%m-%d %H:%M")
    try:
        client_id, token = credentials(os.environ)
        end = int(time.time() * 1000)
        start = end - int(args.days * 86400 * 1000)
        rows = to_rows(fetch(client_id, token, start, end))
        added, total = merge_into(args.out, rows)
        line = f"{stamp} ok: fetched {len(rows)} windows, {added} new, {total} in {args.out.name}"
        code = 0
    except (Exception, SystemExit) as e:  # the log is the only place a scheduled run can report
        line = f"{stamp} FAILED: {type(e).__name__}: {e}"
        code = 1
    print(line)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    with args.log.open("a", encoding="utf-8") as f:
        f.write(line + "\n")
    return code


if __name__ == "__main__":
    sys.exit(main())
