"""Publishes a fake WashWatch shadow to NETPIE so the dashboard can be checked without hardware.

Every 5 s it publishes dashboard/shadow_sample.json with washer 1 running and its minutes left
counting down from 40; after 60 s washer 1 is done (the blinking light) and washer 2 goes offline.
Ctrl+C stops it.

It writes the real device shadow: don't run it while the real gateway is publishing (they
overwrite each other), and the shadow stays in the fake state until the gateway next publishes.

    set NETPIE_CLIENT_ID=...  &  set NETPIE_TOKEN=...  &  set NETPIE_SECRET=...
    uv run --no-project --with "paho-mqtt<2" python tools/fake_shadow.py
"""
import copy
import json
import os
import time
from pathlib import Path

FIXTURE = Path(__file__).resolve().parents[1] / "dashboard" / "shadow_sample.json"
PERIOD_S = 5
DONE_AFTER_S = 60


def shadow_at(base: dict, elapsed_s: float) -> dict:
    d = copy.deepcopy(base)
    w1, w2 = d["washers"]["1"], d["washers"]["2"]
    w1.update(online=True, age_s=1, lid_open=False)
    w2["online"] = True
    if elapsed_s < DONE_AFTER_S:
        w1.update(state="running", min_left=max(1, 40 - int(elapsed_s // PERIOD_S)))
    else:
        w1.update(state="done", min_left=0, phase="idle", std_mg=2, p2p_mg=8)
        w2["online"] = False
    d["ts"] = int(time.time())
    return d


CRED_VARS = ("NETPIE_CLIENT_ID", "NETPIE_TOKEN", "NETPIE_SECRET")


def credentials(env) -> tuple[str, str, str]:
    missing = [v for v in CRED_VARS if not env.get(v)]
    if missing:
        raise SystemExit(f"set the device credentials first: {', '.join(missing)} not set")
    return tuple(env[v] for v in CRED_VARS)


def main() -> None:
    # Imported here (both need paho), so the tests can import this file without it.
    import paho.mqtt.client as mqtt
    from netpie_probe import BROKER, TLS_PORT, pinned_context

    client_id, token, secret = credentials(os.environ)
    base = json.loads(FIXTURE.read_text(encoding="utf-8"))["data"]
    client = mqtt.Client(client_id=client_id)
    client.username_pw_set(token, secret)
    client.tls_set_context(pinned_context())  # credentials only go to the pinned NETPIE broker
    client.connect(BROKER, TLS_PORT)
    client.loop_start()
    start = time.time()
    try:
        while True:
            elapsed = time.time() - start
            d = shadow_at(base, elapsed)
            client.publish("@shadow/data/update", json.dumps({"data": d})).wait_for_publish()
            w1 = d["washers"]["1"]
            print(f"{elapsed:5.0f} s  washer 1 {w1['state']} {w1['min_left']} min, "
                  f"washer 2 online={d['washers']['2']['online']}", flush=True)
            time.sleep(PERIOD_S)
    except KeyboardInterrupt:
        pass
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
