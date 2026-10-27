"""Builds dashboard/washwatch_dashboard.json, the WashWatch NETPIE dashboard (M5).

NETPIE's export format is undocumented, so the widgets are copied from an exported probe
(dashboard/template_export.json: one Text, Indicator, Gauge and Chart widget with @@...@@
placeholders) and only their placeholders are replaced.

    python tools/dashboard.py        # regenerate after changing this file
    uv run --no-project --with pytest python -I -m pytest tools/tests -q

Import: portal -> Console -> Dashboard -> (dashboard) -> Settings: add the data source with
alias "washwatch" (Read Shadow + Read Feed) -> ... -> Import the generated file -> Save.
"""
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE_PATH = ROOT / "dashboard" / "template_export.json"
OUT_PATH = ROOT / "dashboard" / "washwatch_dashboard.json"

ALIAS = "washwatch"
WASHERS = (1, 2)
DASH = "–"  # shown for a missing value


def shadow(*keys) -> str:
    return f'#["{ALIAS}"]["shadow"]' + "".join(f'["{k}"]' for k in keys)


def guarded(keys, fallback: str) -> str:
    """The shadow value at keys, or fallback (a JS literal) if it or any parent is missing.
    A field published as null is removed from the shadow, and `== null` also matches undefined."""
    checks = " || ".join(f"{shadow(*keys[:i])} == null" for i in range(len(keys) + 1))
    return f"(({checks}) ? {fallback} : {shadow(*keys)})"


def shown(keys) -> str:
    return guarded(keys, json.dumps(DASH, ensure_ascii=False))


class Builder:
    def __init__(self, template: dict):
        self.template = template
        self.by_type = {w["type"]: w for p in template["panes"] for w in p["widgets"]}
        self.next_id = 0

    def new_id(self) -> str:
        self.next_id += 1
        return f"ww{self.next_id:04d}"  # stable, so the generated file only changes when the design does

    def widget(self, kind: str, fill: dict, **settings) -> dict:
        w = copy.deepcopy(self.by_type[kind])
        w["id"] = self.new_id()
        s = w["settings"]
        for key, value in s.items():
            if isinstance(value, str) and value in fill:
                s[key] = fill[value]
        s.update(settings)
        if "id" in s:
            s["id"] = w["id"]
        return w

    def text(self, title, value, units=""):
        return self.widget("text_widget", {"@@TITLE@@": title, "@@VALUE@@": value, "@@UNITS@@": units})

    def light(self, title, value, on, off, blink=False):
        # ON/OFF TEXT are JavaScript expressions in NETPIE, so they must be string literals:
        # a bare word is evaluated (e.g. `closed` is the browser global window.closed = false).
        def lit(s):
            return json.dumps(s, ensure_ascii=False)
        return self.widget("indicator", {"@@TITLE@@": title, "@@VALUE@@": value, "@@ON@@": lit(on),
                                         "@@OFF@@": lit(off), "@@BLINK@@": "true" if blink else "false"})

    def gauge(self, title, value, units, lo, hi):
        return self.widget("gauge", {"@@TITLE@@": title, "@@VALUE@@": value, "@@UNITS@@": units},
                           min_value=lo, max_value=hi, decimal=0)

    def chart(self, title, filter_):
        # query_range is a preset; "1d" is the one value confirmed valid by the probe export.
        return self.widget("Chart", {"@@TITLE@@": title, "@@SOURCE@@": f'#["{ALIAS}"]["feed"]',
                                     "@@FILTER@@": filter_}, query_range="1d")

    def pane(self, title, col, widgets):
        p = copy.deepcopy(self.template["panes"][0])
        p.update(id=self.new_id(), title=title, row={"5": 1}, col={"5": col}, widgets=widgets)
        return p


FRESH_S = 90  # the gateway publishes its shadow every 30 s


def clock_synced() -> str:
    # Before NTP sync the gateway's ts is seconds since boot, not a date.
    ts = guarded(("ts",), "null")
    return f"({ts} != null && {ts} >= 1e9)"


def gateway_online() -> str:
    # NETPIE's device status is only read when the dashboard loads (stale either way), so the
    # gateway counts as online while its shadow is under FRESH_S old.
    return f'({clock_synced()} && Date.now() / 1000 - {shadow("ts")} < {FRESH_S})'


def last_update() -> str:
    # NETPIE only re-evaluates expressions when data arrives, so the light above can freeze on
    # "online" after the gateway dies; the absolute time of the last shadow stays true.
    ts = guarded(("ts",), "null")
    fmt = '{day: "numeric", month: "short", year: "numeric", hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false}'
    return (f'({ts} == null) ? "{DASH}" : (!{clock_synced()} ? "clock not synced" : '
            f'new Date({ts} * 1000).toLocaleString("en-GB", {fmt}))')


def washer_online(n: int) -> str:
    # A dead gateway leaves the washer's last "online" in the shadow, so it also needs a fresh
    # shadow; before NTP sync freshness can't be judged, so the washer's own flag is trusted.
    return (f'({guarded(("washers", str(n), "online"), "false")} && '
            f'(!{clock_synced()} || {gateway_online()}))')


def washer_state(n: int) -> str:
    w = ("washers", str(n))
    state = guarded(w + ("state",), "null")
    mins = shown(w + ("min_left",))
    return (f'(!{washer_online(n)}) ? "offline" : ({state} == "done" ? "done {DASH} waiting to be collected" : '
            f'({state} == "running" ? "running · " + {mins} + " min left" : '
            f'({state} == "idle" ? "free" : "{DASH}")))')


def washer_link(n: int) -> str:
    # age_s is the node's age when the gateway published, so show the absolute time it was heard:
    # "N s ago" would be off by up to a publish period, and frozen on an open page.
    w = ("washers", str(n))
    pdr, age = guarded(w + ("pdr",), "null"), guarded(w + ("age_s",), "0")
    heard = f'new Date(({shadow("ts")} - {age}) * 1000).toLocaleTimeString("en-GB", {{hour12: false}})'
    return (f'(!{washer_online(n)} || {pdr} == null) ? "{DASH}" : '
            f'Math.round({pdr} * 1000) / 10 + " %" + ({clock_synced()} ? " · heard " + {heard} : "")')


def build(template: dict) -> dict:
    b = Builder(template)
    panes = [b.pane("Gateway", 1, [
        b.light("Gateway", gateway_online(), "online", "offline"),
        b.text("Last update", last_update()),
        b.text("Temperature", shown(("env", "t")), "°C"),
        b.text("Humidity", shown(("env", "rh")), "%"),
        b.text("Rain sensor", shown(("env", "rain"))),
        b.light("Someone at the washers", guarded(("env", "pir"), "false"), "yes", "no"),
    ])]
    for col, n in enumerate(WASHERS, start=2):
        w = ("washers", str(n))
        panes.append(b.pane(f"Washer {n}", col, [
            b.text("State", washer_state(n)),
            # min_left keeps counting on the gateway after a node goes quiet: hide it when offline.
            b.gauge("Minutes left", f'({washer_online(n)} ? {guarded(w + ("min_left",), "0")} : 0)', "min", 0, 60),
            b.light(f"Done {DASH} collect laundry",
                    f'({washer_online(n)} && {guarded(w + ("state",), "null")} == "done")',
                    "collect now", DASH, blink=True),
            # A washer not heard since a gateway reboot keeps its old lid_open in the (merged) shadow.
            b.light("Lid open", f'({washer_online(n)} && {guarded(w + ("lid_open",), "false")})', "open", "closed"),
            # With a dead IMU the running/done detection is wrong while everything else looks fine.
            b.light("Motion sensor fault", f'({washer_online(n)} && {guarded(w + ("imu_fault",), "false")})',
                    "fault", "ok"),
            b.text("Link", washer_link(n)),
        ]))
    panes.append(b.pane("Training log", 4, [
        b.text("Waiting to upload", shown(("log", "buffered")), "records"),
        b.text("Lost (buffer full)", shown(("log", "dropped")), "records"),
        b.text("Refused by NETPIE", shown(("log", "rejected")), "records"),
    ]))
    panes.append(b.pane("Vibration", 5, [
        b.chart(f"Washer {n} vibration (mg)", f"w{n}_std,w{n}_p2p") for n in WASHERS
    ]))
    out = {k: v for k, v in template.items() if k != "panes"}
    out["panes"] = panes
    return out


def main() -> None:
    template = json.loads(TEMPLATE_PATH.read_text(encoding="utf-8"))
    OUT_PATH.write_text(json.dumps(build(template), indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {OUT_PATH.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
