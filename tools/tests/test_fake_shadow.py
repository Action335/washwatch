"""Tests for tools/fake_shadow.py (no network: only the parts that don't connect).

Run: uv run --no-project --with pytest python -I -m pytest tools/tests -q
"""
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import fake_shadow  # noqa: E402


def test_missing_credentials_name_the_variables():
    with pytest.raises(SystemExit) as e:
        fake_shadow.credentials({"NETPIE_CLIENT_ID": "x"})
    msg = str(e.value)
    assert "NETPIE_TOKEN" in msg and "NETPIE_SECRET" in msg and "NETPIE_CLIENT_ID" not in msg


def test_credentials_from_env():
    env = {"NETPIE_CLIENT_ID": "c", "NETPIE_TOKEN": "t", "NETPIE_SECRET": "s"}
    assert fake_shadow.credentials(env) == ("c", "t", "s")
