"""Check three NETPIE 2020 behaviours the gateway's payload relies on, before Task 8.

  1. Feed stores nested fields (nodes.1.pm25).
  2. A shadow update with "pm25": null is accepted (the gateway sends null for a faulted sensor).
  3. A shadow update merges into the stored shadow instead of replacing it.

Usage (credentials come from env vars, never from this file):
    pip install paho-mqtt
    set NETPIE_CLIENT_ID=...  &  set NETPIE_TOKEN=...  &  set NETPIE_SECRET=...
    python tools/netpie_probe.py

After each step the script reads the shadow back and prints it. For check 1, apply
dashboard/netpie_schema.json first and look at the device's Feed tab afterwards.
"""
import json
import os
import queue
import time

import paho.mqtt.client as mqtt

BROKER = "mqtt.netpie.io"
PORT = 1883
UPDATE_TOPIC = "@shadow/data/update"
GET_TOPIC = "@shadow/data/get"
GET_RESPONSE_TOPIC = "@private/shadow/data/get/response"

replies: "queue.Queue[str]" = queue.Queue()


def full_payload(pm25: int) -> dict:
    return {
        "data": {
            "gw": "airbox-01",
            "ts": int(time.time()),
            "nodes": {
                "1": {"pm1": 30, "pm25": pm25, "pm10": 55, "t": 31.2, "rh": 68.0, "mq": 812,
                      "flags": 0, "seq": 10422, "age_s": 1, "online": True, "pdr": 0.998},
                "2": {"t": 33.0, "rh": 72.0, "lux": 800, "flags": 0, "seq": 10398, "age_s": 2,
                      "online": True, "pdr": 0.993},
            },
        }
    }


def on_message(_client, _userdata, msg) -> None:
    replies.put(msg.payload.decode())


def read_shadow(client: mqtt.Client) -> dict | None:
    while not replies.empty():
        replies.get()
    client.publish(GET_TOPIC, "")
    try:
        return json.loads(replies.get(timeout=5)).get("data")
    except queue.Empty:
        print("  (no shadow reply in 5 s: check the Shadow tab in the portal instead)")
        return None


def step(client: mqtt.Client, title: str, payload: dict) -> dict | None:
    print(f"\n== {title}\n  sent: {json.dumps(payload)}")
    client.publish(UPDATE_TOPIC, json.dumps(payload)).wait_for_publish()
    time.sleep(2)
    shadow = read_shadow(client)
    print(f"  shadow now: {json.dumps(shadow)}")
    return shadow


def node(shadow: dict | None, node_id: str) -> dict:
    return ((shadow or {}).get("nodes") or {}).get(node_id) or {}


def main() -> None:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=os.environ["NETPIE_CLIENT_ID"])
    client.username_pw_set(os.environ["NETPIE_TOKEN"], os.environ["NETPIE_SECRET"])
    client.on_message = on_message
    client.connect(BROKER, PORT)
    client.subscribe(GET_RESPONSE_TOPIC)
    client.loop_start()

    # Check 1: five full updates, 10 s apart, so Feed has points to plot.
    for i in range(5):
        step(client, f"full update {i + 1}/5", full_payload(20 + 10 * i))
        time.sleep(8)

    # Check 2: pm25 null for node 1 (a PMS fault).
    nulled = full_payload(0)
    nulled["data"]["nodes"]["1"]["pm25"] = None
    shadow = step(client, "node 1 pm25 = null", nulled)
    n1 = node(shadow, "1")
    if shadow is None:
        print("  RESULT 2: unknown")
    elif "pm25" in n1 and n1["pm25"] is None:
        print("  RESULT 2: null accepted and stored -> keep sending null for faults")
    elif n1.get("t") == 31.2 and n1.get("pm25") == 60:
        print("  RESULT 2: update REJECTED (old values kept) -> stop sending null; omit the field "
              "and let the dashboard hide values whose fault flag is set")
    else:
        print(f"  RESULT 2: null dropped the key (node 1 = {n1}) -> fine, the stale value is gone")

    # Check 3: an update with only node 2 in it.
    partial = {"data": {"nodes": {"2": {"t": 34.0, "lux": 900}}}}
    shadow = step(client, "partial update (node 2 only)", partial)
    if shadow is None:
        print("  RESULT 3: unknown")
    elif node(shadow, "1"):
        print("  RESULT 3: merge -> fields the gateway leaves out keep their old value")
    else:
        print("  RESULT 3: replace -> each publish must carry every node (it does)")

    client.loop_stop()
    client.disconnect()
    print("\nNow check the Feed tab for nodes.1.pm25 (check 1) and record all three results in README.md.")


if __name__ == "__main__":
    main()
