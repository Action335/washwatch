# WashWatch firmware

2110356 Embedded Systems final project, 2026. Two washer nodes send vibration features and lid state over ESP-NOW to one gateway beside the washers. The gateway works out each washer's cycle, moves a dial and flag servo per washer, publishes to NETPIE and posts Discord alerts. The ESP-NOW ACK/retry pipeline started as AirBox (dropped 26 Oct 2026).

| Folder | What |
|---|---|
| `lib/ww_proto/` | Shared, Arduino-free code: message format, vibration features, ACK/retry, node table, washer cycle logic, NETPIE payload |
| `proto_tests/` | Unit tests for `ww_proto`, run on the PC |
| `node/` | Washer node firmware (ESP32): envs `washer1`, `washer2` |
| `gateway/` | Gateway firmware (ESP32) |
| `wokwi/` | Wokwi wiring and end-to-end test projects (see `wokwi/README.md`) |
| `dashboard/` | NETPIE schema |
| `tools/` | Helper scripts |

## How it works
- **Node** (`node/src/main.cpp`): `sample_task` reads the MPU6050 at 100 Hz; every 2 s `link_task` sends mean/std/peak-to-peak of |a|, a phase (idle/wash/spin) and the lid state. The reed switch ISR sends a lid change straight away.
- **Gateway** (`gateway/src/`): `espnow_rx` (prio 5) ACKs and feeds `WasherCycle`; `control` (4) drives the servos and reads PIR/DHT22/rain; `cloud` (2) publishes the live shadow every 30 s and uploads the training log; `notify` (1) posts Discord alerts on done, a 15-minute reminder, and collected.
- **Not yet done:** the phase is a threshold rule (`STD_WASH_MG`, `STD_SPIN_MG` in `features.h`) and minutes left is a nominal cycle length (`state.cpp`). Both are uncalibrated until real cycles are recorded; the trained classifier (E7) replaces the rule.
- **Cloud security (E5):** MQTT runs over TLS on port 1884 and the gateway only sends its NETPIE credentials after the server certificate matches one of the two pinned fingerprints (`gateway/src/certs.h`, see "NETPIE facts"). Discord webhooks are verified against Google Trust Services roots (GTS Root R1/R4, valid until 2036). No secrets are in the repository.

## Setup
1. Install PlatformIO (VS Code extension or `pip install platformio`) and a host gcc for the PC tests (Windows: `winget install BrechtSanders.WinLibs.POSIX.UCRT`, then put its `mingw64/bin` on PATH).
2. Copy `secrets.h.example` to `secrets.h` and fill it in. `secrets.h` is gitignored: never commit it. Only the gateway uses it: nodes find the gateway by sweeping channels, so switching to a phone hotspot means re-flashing the gateway only.
3. Set each washer's usual cycle length in `gateway/src/state.cpp`.

## Commands

```sh
pio test -d proto_tests -e native                 # all PC unit tests
pio test -d proto_tests -e native -f test_washer_cycle  # one suite
pio run -d node -e washer1 -t upload -t monitor
pio run -d gateway -t upload -t monitor
```

## NETPIE facts
Checked 26 Oct 2026 from the PC (openssl + `tools/netpie_probe.py`'s TLS check):
- MQTT over TLS is **port 1884** (1883 is plain).
- `mqtt.netpie.io` (203.150.88.229) is two brokers behind one address, each with its own certificate, both issued by NETPIE's **private** CA "NEXPIE Device Broker" (not published, not sent by the server), both valid until 12 Sep 2036:
  - `2D:56:F9:F3:AA:B9:63:9E:A9:A4:B6:4B:B1:DC:20:99:32:13:BC:0A:8A:7D:A9:9F:44:FC:FE:86:37:F0:9E:A8`
  - `91:D7:1A:00:54:40:C3:DC:95:2D:7D:5D:BB:B2:B6:94:35:F9:3F:7D:47:4E:83:AA:2C:81:B1:64:75:59:C7:46`
  
  So clients **pin both certificates** (`tools/netpie_server_certs.pem`; the gateway pins the same two by SHA-256). If NETPIE adds or rotates a broker, connections to it fail with a pin error until the new certificate is checked and added.
- `discord.com` chains to Google Trust Services (WE1 → GTS Root R4, cross-signed by GlobalSign Root CA).

Probed 26 Oct 2026 with the device credentials (`python tools/netpie_probe.py`, run with `uv run --no-project --with "paho-mqtt<2"`):
- **Batch topic:** `@shadow/batch/update`; replies arrive on `@private/shadow/batch/update/response`.
- **Success reply:** `{"deviceid":"...","ackid":1,"total":2,"mints":<first ts ms>,"maxts":<last ts ms>,"cid":null}`. There is no `errcode`.
- **Refusals are silent:** a 101-record batch (over the documented limit of 100) and malformed JSON get **no reply at all**, not the documented `errcode 429`, and the connection stays up. The gateway therefore relies on its ack timeout: an unanswered batch is resent and dropped after 6 tries (`BATCH_MAX_TIMEOUTS`), counted in `log.rejected`.
- **No validation:** a batch with a text `ts` was acked (`"mints":"oops"`), so the payload builder must stay correct on its own (it always sends integer ms).
- A 50-record batch (`LOG_BATCH_MAX`) is acked with `"total":50`.
- **The Feed only stores fields that already have a datatag** when the data arrives, and datatags are created in the device's **Feed tab → "+ Add datatag"** (Data Path = the shadow key, e.g. `w1_std`; Data Type Number; TTL), not in the Schema tab. Before that the probe reached the shadow but stored 0 Feed points. After creating them (26 Oct 2026), a gateway-shaped batch stored all 10 `w1_*`/`w2_*` fields (checked with the Data Store API, `POST https://ds.netpie.io/feed/api/v1/datapoints/query`, header `Authorization: Device <ClientID>:<Token>`).
- TTL choices are fixed: 5/15/30 min, 1/2/4/8/12 h, 1/2/3 days, **1 week**, 1/3/6 months, 1 year. There is no 14 days.
- Still to check in the portal: the usage page shows how many shadow writes one batch costs.

## Training data (for the phase classifier)
The gateway logs every window while a washer is active, one idle window per 30 s, and every lid change, as Feed points `w<id>_std`, `w<id>_p2p`, `w<id>_phase`, `w<id>_state` (0 idle, 1 running, 2 done) and `w<id>_lid`. The 10 datatags (washers 1 and 2) exist on the device with **TTL 1 week** (created 26 Oct 2026). 1 week keeps storage around 440k point-month, inside the free 1,000,000; 1 month would be about 1.9M.

To download: portal → device → Feed → **Exports** → pick the `w1_*` / `w2_*` datatags and a date range → CSV. **Export at least once a week (e.g. every Sunday)**, because points older than 1 week are deleted.

Records wait in a RAM buffer (3,000 records, about 50 min of both washers running) while the cloud is unreachable and upload oldest-first when it is back (E4). The dashboard's `log` object shows `buffered`, `dropped` (buffer overflowed) and `rejected` (NETPIE refused a batch 3 times). Limitation: the buffer is in RAM, so records still waiting are lost if the gateway reboots during an outage.

Simulation build: `pio run -d gateway -e gateway_sim` runs two simulated washer nodes on Wokwi's open Wi-Fi against the real NETPIE device in `secrets.h`. Add `-DSIM_OUTAGE` to its `build_flags` to cut the cloud off from 20 s to 50 s of uptime.

## Dashboard (NETPIE)
Portal → project WashWatch → **Console → Dashboard**. A dashboard reads the device through a **data source** added in its **Settings** tab ("Add source": Type Device, Device `washwatch-gateway`, **Alias `washwatch`**, privileges **Read Shadow + Read Feed** only). The alias is not part of an exported or imported file, so add it to any new dashboard before importing.

Widgets read the shadow as `#["washwatch"]["shadow"]["washers"]["1"]["state"]`, and VALUE fields are JavaScript expressions (checked 26 Oct 2026: a nested path and `(x == null ? "–" : x)` both render). A field published as `null` is removed from the shadow, so a guard must test `== null` (which also matches a missing field). A Gauge with a non-number value shows `NaN`, so its guard must fall back to a number.

`dashboard/template_export.json` is an export of the probe dashboard `washwatch-probe` (one Text, Indicator, Gauge and Chart widget with `@@...@@` placeholders). It is Freeboard JSON: `panes[].widgets[]` with `type` `text_widget` / `indicator` / `gauge` / `Chart` and a short random `id` on every pane and widget. The Chart's time window is a preset string (`query_range`, e.g. `"1d"`).

The full dashboard is generated: `python tools/dashboard.py` writes `dashboard/washwatch_dashboard.json`, and `uv run --no-project --with pytest python -I -m pytest tools/tests -q` checks it (every expression is run in Node against the firmware's sample shadow, an empty shadow and an offline washer). To install it: open the dashboard (the live one is **WashWatch**) → **⋯ → Import** → choose that file → **Save**.

Two NETPIE behaviours the generator works around (both seen live on 26–27 Oct 2026):
- **Indicator ON/OFF TEXT are JavaScript expressions**, so the generator writes them as string literals (a bare `closed` showed `false`: it is the browser global `window.closed`).
- **The device `status` is only read when the dashboard loads** (it stayed 0 after the device connected, and was 1 after a reload). A load-time `status` is stale either way, so the dashboard doesn't use it: the Gateway light is "online" while the shadow's `ts` is less than 90 s old, and a washer counts as online only if its own `online` is true **and** the gateway is online (otherwise its gauge shows 0 and its done light is off).
- **Expressions are only re-evaluated when data arrives** (27 Oct: after the publisher stopped, the Gateway light stayed "online" until a reload). NETPIE's data sources can only be devices, so there is no timer to force a refresh. The light and the washer states are therefore correct as of the last update or a reload; the Gateway panel's **Last update** time is what tells you on an open page that the gateway has gone quiet.
- Quirk in the portal: picking **Read Shadow** in a data source's privileges also silently adds **Write Shadow**. Check the privileges column after saving and edit it out.

Check the dashboard without hardware: `uv run --no-project --with "paho-mqtt<2" python tools/fake_shadow.py` (device credentials in env vars) publishes the sample shadow every 5 s: washer 1 counts down, after 60 s it is done (blinking light) and washer 2 goes offline.

![WashWatch dashboard during a fake_shadow.py run, 27 Oct 2026](docs/img/dashboard-live.png)
