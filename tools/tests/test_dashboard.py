"""Tests for tools/dashboard.py, the NETPIE dashboard generator.

Run: uv run --no-project --with pytest python -I -m pytest tools/tests -q
Expressions are evaluated in Node.js, the same JavaScript the dashboard runs in the browser.
"""
import copy
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import dashboard  # noqa: E402

TEMPLATE = json.loads((ROOT / "dashboard" / "template_export.json").read_text(encoding="utf-8"))
FIXTURE = json.loads((ROOT / "dashboard" / "shadow_sample.json").read_text(encoding="utf-8"))["data"]
SHADOW_PATH = re.compile(r'#\["washwatch"\]\["shadow"\]((?:\["[^"]+"\])+)')


@pytest.fixture(scope="module")
def dash():
    return dashboard.build(copy.deepcopy(TEMPLATE))


def widgets(d):
    for pane in d["panes"]:
        for w in pane["widgets"]:
            yield pane["title"], w


def find(d, pane_title, widget_title):
    for p, w in widgets(d):
        if p == pane_title and w["settings"].get("title") == widget_title:
            return w
    raise AssertionError(f"no widget {widget_title!r} in pane {pane_title!r}")


def expressions(d):
    """(pane, widget title, setting name, expression) for every setting that reads the device."""
    for p, w in widgets(d):
        for k, v in w["settings"].items():
            if isinstance(v, str) and '#["washwatch"]' in v:
                yield p, w["settings"].get("title"), k, v


def fresh(shadow):
    """A copy of shadow published just now (the fixture's ts is from 2025, so it reads as stale)."""
    s = copy.deepcopy(shadow)
    s["ts"] = int(time.time()) - 5
    return s


def evaluate(exprs, shadow, status=1):
    """Runs each expression in Node with #["washwatch"] bound to a fake datasource.
    Returns a list of (ok, value-or-error) in the same order."""
    node = shutil.which("node")
    if node is None:
        pytest.skip("node not installed")
    script = (
        "const D = {washwatch: {shadow: %s, status: %d, feed: []}};\n"
        "const exprs = %s;\n"
        "const out = exprs.map(e => { try { return [true, eval(e.replace(/#\\[/g, 'D['))]; }"
        " catch (err) { return [false, String(err)]; } });\n"
        "console.log(JSON.stringify(out));\n"
    ) % (json.dumps(shadow), status, json.dumps(exprs))
    # Script on stdin and UTF-8 both ways: Windows' default codepage would mangle the "–" dash.
    res = subprocess.run([node, "-"], input=script, capture_output=True, encoding="utf-8", timeout=30)
    assert res.returncode == 0, res.stderr
    return json.loads(res.stdout)


def test_no_sentinel_left(dash):
    text = json.dumps(dash)
    assert "@@" not in text
    for _, w in widgets(dash):
        assert w["settings"].get("min_value") != 111
        assert w["settings"].get("max_value") != 999


def test_panels(dash):
    assert [p["title"] for p in dash["panes"]] == [
        "Gateway", "Washer 1", "Washer 2", "Training log", "Vibration"]


def test_ids_unique(dash):
    ids = [p["id"] for p in dash["panes"]] + [w["id"] for _, w in widgets(dash)]
    assert len(ids) == len(set(ids))
    assert all(isinstance(i, str) and len(i) == 6 for i in ids)


def test_all_shadow_paths_exist_in_fixture(dash):
    for _, title, _, expr in expressions(dash):
        for m in SHADOW_PATH.finditer(expr):
            keys = re.findall(r'\["([^"]+)"\]', m.group(1))
            if keys[:1] == ["washers"] and len(keys) > 1:
                keys[1] = "1"  # washer 2 is unheard in the fixture; washer 1 has every key
            node = FIXTURE
            for k in keys:
                assert isinstance(node, dict) and k in node, f"{title}: shadow path {keys} not in fixture"
                node = node[k]


def test_every_shadow_value_is_null_guarded(dash):
    for _, title, k, expr in expressions(dash):
        if '["shadow"]' in expr:
            assert "== null" in expr, f"{title}.{k} reads the shadow without a null guard"


def test_expressions_run_on_the_real_shadow(dash):
    exprs = [e for *_, e in expressions(dash) if '["feed"]' not in e]
    for ok, val in evaluate(exprs, FIXTURE):
        assert ok, val
        assert val not in (None, "undefined", "NaN") and "undefined" not in str(val) and "NaN" not in str(val)


def test_expressions_survive_an_empty_shadow(dash):
    # Before the gateway has ever published: nothing may throw or print undefined/NaN.
    exprs = [e for *_, e in expressions(dash) if '["feed"]' not in e]
    for ok, val in evaluate(exprs, {}):
        assert ok, val
        assert "undefined" not in str(val) and "NaN" not in str(val)


def test_state_text_says_offline_when_not_online(dash):
    w = find(dash, "Washer 1", "State")
    offline = fresh(FIXTURE)
    offline["washers"]["1"]["online"] = False  # last state is still "running"
    [(ok, val)] = evaluate([w["settings"]["value"]], offline)
    assert ok and val == "offline"


def test_state_text_for_each_state(dash):
    expr = find(dash, "Washer 1", "State")["settings"]["value"]
    cases = {"idle": "free", "done": "done – waiting to be collected"}
    for state, want in cases.items():
        s = fresh(FIXTURE)
        s["washers"]["1"].update(state=state, online=True)
        [(ok, val)] = evaluate([expr], s)
        assert ok and val == want
    s = fresh(FIXTURE)
    s["washers"]["1"].update(state="running", online=True, min_left=12)
    [(ok, val)] = evaluate([expr], s)
    assert ok and val == "running · 12 min left"


def test_state_text_unknown_state_is_a_dash(dash):
    # An online washer whose state is missing or unexpected must not look "free".
    expr = find(dash, "Washer 1", "State")["settings"]["value"]
    s = fresh(FIXTURE)
    s["washers"]["1"]["online"] = True
    del s["washers"]["1"]["state"]
    t = fresh(FIXTURE)
    t["washers"]["1"].update(online=True, state="spinning")
    [(ok1, a)], [(ok2, b)] = evaluate([expr], s), evaluate([expr], t)
    assert (ok1, ok2, a, b) == (True, True, "–", "–")


def test_imu_fault_light(dash):
    # With a dead IMU, running/done detection is wrong while everything else looks healthy.
    expr = find(dash, "Washer 1", "Motion sensor fault")["settings"]["value"]
    assert find(dash, "Washer 2", "Motion sensor fault")
    got = []
    for online, fault in ((True, True), (True, False), (False, True)):
        s = fresh(FIXTURE)
        s["washers"]["1"].update(online=online, imu_fault=fault)
        got.append(evaluate([expr], s)[0])
    got.append(evaluate([expr], {})[0])
    assert got == [[True, True], [True, False], [True, False], [True, False]]


def test_gauge_falls_back_to_a_number(dash):
    w = find(dash, "Washer 2", "Minutes left")
    assert w["type"] == "gauge"
    assert (w["settings"]["min_value"], w["settings"]["max_value"]) == (0, 60)
    [(ok, val)] = evaluate([w["settings"]["value"]], {})
    assert ok and val == 0


def test_gateway_online_only_from_a_fresh_shadow(dash):
    # NETPIE's device status is only read when the dashboard loads (seen live), so it is stale in
    # both directions and not used: the gateway is online while its shadow is under 90 s old.
    v = find(dash, "Gateway", "Gateway")["settings"]["value"]
    assert '["status"]' not in v
    stale = copy.deepcopy(FIXTURE)          # ts from 2025: stale
    unsynced = copy.deepcopy(FIXTURE)
    unsynced["ts"] = 15                     # before NTP sync, ts is seconds since boot
    [(ok1, a)] = evaluate([v], stale, status=1)
    [(ok2, b)] = evaluate([v], unsynced, status=1)
    [(ok3, c)] = evaluate([v], fresh(FIXTURE), status=0)
    assert (ok1, ok2, ok3) == (True, True, True)
    assert (a, b, c) == (False, False, True)


def test_last_update_time_is_shown(dash):
    # NETPIE only re-evaluates expressions when data arrives (seen live: the Gateway light stayed
    # "online" with no reload after the publisher stopped), so the absolute time of the last
    # shadow is the one thing that stays true on a frozen page.
    v = find(dash, "Gateway", "Last update")["settings"]["value"]
    unsynced = copy.deepcopy(FIXTURE)
    unsynced["ts"] = 15
    [(ok1, a), (ok2, b), (ok3, c)] = [evaluate([v], s)[0] for s in (FIXTURE, unsynced, {})]
    assert (ok1, ok2, ok3) == (True, True, True)
    assert re.fullmatch(r"\d{1,2} \w{3} 2025, \d{2}:\d{2}:\d{2}", a), a
    assert (b, c) == ("clock not synced", "–")


def test_washer_pane_goes_offline_with_the_gateway(dash):
    # A dead gateway leaves the last shadow (washer 1 "running", online) behind: the pane must not
    # show a frozen countdown or a "collect now" light.
    for state in ("running", "done"):
        s = copy.deepcopy(FIXTURE)          # ts stale
        s["washers"]["1"].update(state=state, online=True, min_left=12)
        [(ok1, text), (ok2, mins), (ok3, done)] = evaluate(
            [find(dash, "Washer 1", t)["settings"]["value"]
             for t in ("State", "Minutes left", "Done – collect laundry")], s)
        assert (ok1, ok2, ok3) == (True, True, True)
        assert (text, mins, done) == ("offline", 0, False), state


def test_offline_washer_shows_no_countdown_or_done(dash):
    # min_left keeps counting down on the gateway after a node goes quiet; the gauge and the
    # done light must not contradict the "offline" state text.
    s = fresh(FIXTURE)
    s["washers"]["1"].update(state="done", online=False, min_left=12)
    [(ok1, mins), (ok2, done)] = evaluate(
        [find(dash, "Washer 1", t)["settings"]["value"] for t in ("Minutes left", "Done – collect laundry")], s)
    assert (ok1, ok2) == (True, True)
    assert (mins, done) == (0, False)


def test_done_light_blinks(dash):
    w = find(dash, "Washer 1", "Done – collect laundry")
    assert w["settings"]["blinking"] == "true"
    assert find(dash, "Washer 1", "Lid open")["settings"]["blinking"] == "false"


def test_indicator_texts_are_string_literals(dash):
    # ON/OFF TEXT are JavaScript expressions in NETPIE: a bare word is evaluated, and some are
    # browser globals (`closed` showed "false" on the live dashboard, `open` is a function).
    lights = [(p, w["settings"]) for p, w in widgets(dash) if w["type"] == "indicator"]
    exprs = [s[k] for _, s in lights for k in ("on_text", "off_text")]
    shown_texts = evaluate(exprs, FIXTURE)
    expected = {("Washer 1", "Lid open"): ("open", "closed"), ("Gateway", "Gateway"): ("online", "offline")}
    for i, (p, s) in enumerate(lights):
        (ok_on, on), (ok_off, off) = shown_texts[2 * i], shown_texts[2 * i + 1]
        assert ok_on and ok_off and isinstance(on, str) and isinstance(off, str), (p, s["title"], on, off)
        if (p, s["title"]) in expected:
            assert (on, off) == expected[(p, s["title"])]


def test_charts_use_feed_datatags(dash):
    for n in (1, 2):
        w = find(dash, "Vibration", f"Washer {n} vibration (mg)")
        assert w["type"] == "Chart"
        assert w["settings"]["datasource"] == '#["washwatch"]["feed"]'
        assert w["settings"]["filter"] == f"w{n}_std,w{n}_p2p"


def test_generated_file_is_up_to_date(dash):
    path = ROOT / "dashboard" / "washwatch_dashboard.json"
    assert path.exists(), "run: python tools/dashboard.py"
    assert json.loads(path.read_text(encoding="utf-8")) == dash
