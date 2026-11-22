"""Tests for tools/export_feed.py (no network: only the parts that don't connect).

Run: uv run --no-project --with pytest python -I -m pytest tools/tests -q
"""
import csv
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import export_feed as ef  # noqa: E402


def test_keys_cover_both_washers_and_all_fields():
    assert len(ef.KEYS) == 14
    assert "w1_vstd" in ef.KEYS and "w2_lid" in ef.KEYS
    assert not any(k.endswith("_phase") for k in ef.KEYS)


def test_missing_credentials_name_the_variables():
    with pytest.raises(SystemExit) as e:
        ef.credentials({"NETPIE_CLIENT_ID": "x"}, user_env={})
    assert "NETPIE_TOKEN" in str(e.value)


def test_credentials_fall_back_to_user_environment():
    # A shell started before the variables were set has only the per-user copy.
    assert ef.credentials({}, user_env={"NETPIE_CLIENT_ID": "id", "NETPIE_TOKEN": "tok"}) == ("id", "tok")


def test_chunks_cover_the_range_without_gaps_or_overlap():
    chunks = list(ef.chunks(0, 10_000, 3_000))
    assert chunks == [(0, 2_999), (3_000, 5_999), (6_000, 8_999), (9_000, 10_000)]


def test_query_body_asks_for_every_key_grouped_by_attr():
    body = ef.query_body("dev-id", ["w1_std", "w1_lid"], 100, 200)
    assert body["start_absolute"] == 100 and body["end_absolute"] == 200
    metric = body["metrics"][0]
    assert metric["name"] == "dev-id"
    assert metric["tags"]["attr"] == ["w1_std", "w1_lid"]
    assert metric["group_by"] == [{"name": "tag", "tags": ["attr"]}]


def test_parse_skips_empty_results():
    resp = {"queries": [{"results": [
        {"tags": {"attr": ["w1_std"]}, "values": [[1000, 3], [3000, 4]]},
        {"tags": {"attr": ["w1_hi"]}, "values": []},
        {"tags": {}, "values": [[5, 5]]},  # no attr tag: not ours
    ]}]}
    assert ef.parse(resp) == {"w1_std": [(1000, 3), (3000, 4)]}


def test_to_rows_groups_one_window_per_washer_and_time():
    points = {
        "w1_std": [(1000, 3), (3000, 4)],
        "w1_lid": [(1000, 1), (3000, 1)],
        "w2_std": [(2000, 9)],
    }
    rows = ef.to_rows(points)
    assert [(r["ts_ms"], r["washer"]) for r in rows] == [(1000, 1), (2000, 2), (3000, 1)]
    assert rows[0]["std"] == 3 and rows[0]["lid"] == 1 and rows[0]["vstd"] == ""
    assert rows[1]["std"] == 9
    assert rows[0]["time"].startswith("1970-01-01T07:00:01")  # Bangkok time, UTC+7


def test_merge_is_idempotent_and_keeps_old_rows(tmp_path):
    out = tmp_path / "feed_all.csv"
    first = ef.to_rows({"w1_std": [(1000, 3), (3000, 4)]})
    assert ef.merge_into(out, first) == (2, 2)
    # A later export overlaps the first and adds one new window; the oldest has expired on NETPIE.
    second = ef.to_rows({"w1_std": [(3000, 4), (5000, 7)]})
    assert ef.merge_into(out, second) == (1, 3)
    assert ef.merge_into(out, second) == (0, 3)
    with out.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    assert [r["ts_ms"] for r in rows] == ["1000", "3000", "5000"]
    assert list(rows[0].keys()) == ef.COLUMNS


def test_failed_run_is_logged_not_raised(tmp_path, monkeypatch):
    # The scheduled task has no console: a failure must land in the log and give exit code 1.
    monkeypatch.setattr(ef, "user_environment", lambda: {})
    for name in ef.CRED_VARS:
        monkeypatch.delenv(name, raising=False)
    log = tmp_path / "log.txt"
    assert ef.main(["--out", str(tmp_path / "f.csv"), "--log", str(log)]) == 1
    assert "FAILED" in log.read_text(encoding="utf-8")
    assert not (tmp_path / "f.csv").exists()


def test_merge_newer_value_wins_for_the_same_window(tmp_path):
    out = tmp_path / "feed_all.csv"
    ef.merge_into(out, ef.to_rows({"w1_std": [(1000, 3)]}))
    ef.merge_into(out, ef.to_rows({"w1_std": [(1000, 3)], "w1_hi": [(1000, 2)]}))
    with out.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    assert len(rows) == 1 and rows[0]["hi"] == "2"
