"""Check the NETPIE 2020 behaviours the WashWatch gateway relies on (cloud-path plan, Task 1).

  1. Which topic accepts a batch update: @shadow/batch/update (mqtt-api docs) or
     @shadow/data/update (device-config docs), and the exact ack it sends back.
  2. A batch over 100 records is refused with errcode 429.
  3. TLS on port 1884: NETPIE's certificates come from its private "NEXPIE Device Broker" CA,
     which is not published, so clients pin the two broker certificates seen behind
     mqtt.netpie.io (tools/netpie_server_certs.pem). The probe trusts only those.

Usage (credentials come from env vars, never from this file):
    pip install paho-mqtt
    set NETPIE_CLIENT_ID=...  &  set NETPIE_TOKEN=...  &  set NETPIE_SECRET=...
    python tools/netpie_probe.py

Before running, create datatag w9_std (number, TTL 1 day) on the device. Afterwards, check
the Feed tab for two w9_std points 30 s apart, then delete the datatag.
"""
import hashlib
import json
import os
import queue
import socket
import ssl

import paho.mqtt.client as mqtt

BROKER = "mqtt.netpie.io"
TLS_PORT = 1884
TOPICS = ["@shadow/batch/update", "@shadow/data/update"]

replies: "queue.Queue[tuple[str, str]]" = queue.Queue()


def batch(ackid: int, n: int) -> dict:
    records = [{"data": {"w9_std": 11 * (i + 1)}, "ts": -60000 + 30000 * i} for i in range(n)]
    return {"ackid": ackid, "merged": True, "batch": records}


def on_message(_client, _userdata, msg) -> None:
    replies.put((msg.topic, msg.payload.decode(errors="replace")))


def drain(seconds: float) -> list[tuple[str, str]]:
    got = []
    try:
        while True:
            got.append(replies.get(timeout=seconds))
            seconds = 0.5  # after the first reply, only wait briefly for more
    except queue.Empty:
        return got


# NETPIE's two broker certificates, pinned (gateway/src/certs.h pins the same two by SHA-256).
# They come from NETPIE's private "NEXPIE Device Broker" CA, which is not published.
PINNED_CERTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "netpie_server_certs.pem")


def pinned_context() -> ssl.SSLContext:
    """Trusts exactly the pinned certificates (partial chain), with hostname checking on."""
    ctx = ssl.create_default_context(cafile=PINNED_CERTS)
    ctx.verify_flags |= ssl.VERIFY_X509_PARTIAL_CHAIN
    return ctx


def check_tls() -> None:
    with socket.create_connection((BROKER, TLS_PORT), timeout=10) as raw:
        with pinned_context().wrap_socket(raw, server_hostname=BROKER) as tls:
            der = tls.getpeercert(binary_form=True)
    print(f"\n== TLS {BROKER}:{TLS_PORT}: pinned certificate verified, "
          f"SHA-256 {hashlib.sha256(der).hexdigest().upper()}")


def main() -> None:
    check_tls()  # raises ssl.SSLCertVerificationError if NETPIE's certificate changed

    client = mqtt.Client(client_id=os.environ["NETPIE_CLIENT_ID"])
    client.username_pw_set(os.environ["NETPIE_TOKEN"], os.environ["NETPIE_SECRET"])
    client.tls_set_context(pinned_context())  # credentials only go to the pinned server
    client.on_message = on_message
    client.connect(BROKER, TLS_PORT)
    client.subscribe("@private/#")
    client.loop_start()

    working = None
    for ackid, topic in enumerate(TOPICS, start=1):
        print(f"\n== 2-record batch, ackid {ackid}, to {topic}")
        client.publish(topic, json.dumps(batch(ackid, 2)))
        got = drain(5)
        for t, p in got:
            print(f"  reply on {t}: {p}")
        if got:
            working = topic
            print(f"  RESULT 1: batch topic = {topic}")
            break
        print("  no reply in 5 s")
    if working is None:
        print("  RESULT 1: NO topic acknowledged a batch -> stop (plan Task 1 stop rule)")
        return

    print(f"\n== 101-record batch, ackid 3, to {working} (expect errcode 429)")
    client.publish(working, json.dumps(batch(3, 101)))
    for t, p in drain(5) or [("-", "no reply in 5 s")]:
        print(f"  reply on {t}: {p}")

    client.loop_stop()
    client.disconnect()
    print("\nNow check the Feed tab for w9_std (2 points, 30 s apart), the usage page for how many")
    print("shadow writes the 2-record batch cost, record everything in README.md, delete w9_std.")


if __name__ == "__main__":
    main()
