"""Publish a sample AirBox shadow update to NETPIE 2020, to check that Feed stores nested fields.

Usage (credentials come from env vars, never from this file):
    pip install paho-mqtt
    set NETPIE_CLIENT_ID=...  &  set NETPIE_TOKEN=...  &  set NETPIE_SECRET=...
    python tools/netpie_probe.py

Then open the device in the NETPIE portal: the Shadow tab should show `nodes`, and with
dashboard/netpie_schema.json applied, the Feed tab should plot nodes.1.pm25.
"""
import json
import os
import random
import time

import paho.mqtt.client as mqtt

BROKER = "mqtt.netpie.io"
PORT = 1883
TOPIC = "@shadow/data/update"


def sample_payload() -> dict:
    return {
        "data": {
            "gw": "airbox-01",
            "ts": int(time.time()),
            "nodes": {
                "1": {"pm1": 30, "pm25": random.randint(20, 60), "pm10": 55, "t": 31.2, "rh": 68.0,
                      "mq": 812, "seq": 10422, "age_s": 1, "online": True, "pdr": 0.998},
                "2": {"t": 33.0, "rh": 72.0, "lux": 800, "seq": 10398, "age_s": 2, "online": True,
                      "pdr": 0.993},
            },
        }
    }


def main() -> None:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=os.environ["NETPIE_CLIENT_ID"])
    client.username_pw_set(os.environ["NETPIE_TOKEN"], os.environ["NETPIE_SECRET"])
    client.connect(BROKER, PORT)
    client.loop_start()
    for i in range(5):
        payload = json.dumps(sample_payload())
        client.publish(TOPIC, payload).wait_for_publish()
        print(f"published {i + 1}/5: {payload}")
        time.sleep(10)
    client.loop_stop()
    client.disconnect()


if __name__ == "__main__":
    main()
