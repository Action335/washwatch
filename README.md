# WashWatch firmware

2110356 Embedded Systems final project, 2026. Two washer nodes send vibration features and lid state over ESP-NOW to one gateway beside the washers. The gateway works out each washer's cycle, moves a dial and flag servo per washer, publishes to NETPIE and posts Discord alerts. The ESP-NOW ACK/retry pipeline started as AirBox (dropped 26 Oct 2026).

| Folder | What |
|---|---|
| `lib/ww_proto/` | Shared, Arduino-free code: message format, vibration features, ACK/retry, node table, washer cycle logic, NETPIE payload |
| `proto_tests/` | Unit tests for `ww_proto`, run on the PC |
| `node/` | Washer node firmware (ESP32): envs `washer1`, `washer2` |
| `gateway/` | Gateway firmware (ESP32) |
| `dashboard/` | NETPIE schema |
| `tools/` | Helper scripts |

## How it works
- **Node** (`node/src/main.cpp`): `sample_task` reads the MPU6050 at 100 Hz; every 2 s `link_task` sends mean/std/peak-to-peak of |a|, a phase (idle/wash/spin) and the lid state. The reed switch ISR sends a lid change straight away.
- **Gateway** (`gateway/src/`): `espnow_rx` (prio 5) ACKs and feeds `WasherCycle`; `control` (4) drives the servos and reads PIR/DHT11/rain; `cloud` (2) publishes the live shadow every 30 s and uploads the training log; `notify` (1) posts Discord alerts on done, a 15-minute reminder, collected, and rain ("bring in laundry drying outside": the rain sensor wet for 30 s, once per rain until it has been dry for 10 min; `RAIN_WET_RAW`/`RAIN_DRY_RAW` in `lib/ww_proto/src/rain_watch.h` are uncalibrated), through a channel webhook (`DISCORD_WEBHOOK_URL` in `secrets.h`). The webhook and message texts were checked from a PC on 27 Oct 2026; the ESP32's own HTTPS post still needs a live run.
- **Task report (E1):** gateway and node print each task's priority and free stack, plus free heap, on the serial monitor 10 s after boot and every 5 min (`lib/ww_rtos/src/task_report.h`, run from `loop()`).
- **Per washer:** the two washers are different top-loader models. `gateway/src/state.cpp` holds each one's nominal cycle length (washer 2: 83 min, measured 7 Nov 2026; washer 1: 45, not measured yet) and `gateway/src/control.cpp` scales each dial (0–60 and 0–90 min). A node whose reed module reads the other way round is built with `-DLID_OPEN_LEVEL=LOW`.
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
pio run -d node -e logger -t upload -t monitor    # raw vibration logger (see "Recording a cycle")
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
Each node message (format v3, 24 bytes) carries, per 2 s window at 100 Hz: `std`/`p2p` of |a|, plus `vstd` (sqrt of the summed per-axis variances, which also sees sideways shake that |a| misses), `lo` (RMS of the 5–15 Hz band over all axes, where the ~11.7 Hz spin sits) and `hi` (RMS of the 15–40 Hz band, motor hum during wash/rinse). The bands are 4th-order band-pass filters (two RBJ biquads) in `lib/ww_proto/src/features.*`. A still sensor reads about `vstd=3 lo=1 hi=1` mg.

The gateway's `LogGate` (`lib/ww_proto/src/batch_json.*`, one per washer) logs every window while the washer is running, moving (`vstd` ≥ `LOG_MOVING_VSTD_MG` = 6 mg) or within `LOG_HOLD_MS` (10 min) of the last moving window, so the quiet soaks inside a cycle and the quiet tail after it are kept in full; otherwise one window per 30 s, plus every lid change. Feed points per record: `w<id>_std`, `w<id>_p2p`, `w<id>_vstd`, `w<id>_lo`, `w<id>_hi`, `w<id>_state` (0 idle, 1 running, 2 done) and `w<id>_lid`. `w<id>_phase` is no longer logged (the uncalibrated rule's output, recomputable from `w<id>_std`).

**Datatags:** the Feed stores only keys that have a datatag. `w1_vstd`, `w1_lo`, `w1_hi`, `w2_vstd`, `w2_lo`, `w2_hi` must be added in the device's Feed tab (Data Type Number) before they are stored. The original 10 datatags have **TTL 1 week** (created 26 Oct 2026). With 7 keys per record and full-rate logging through each cycle, a week of data is roughly 0.6–1.2M points depending on how many cycles run per day, around the free 1,000,000; if the quota warning appears, shorten the TTL and export more often.

To download: `python -I tools/export_feed.py` fetches the last 8 days through the Feed query API (`POST https://ds.netpie.io/feed/api/v1/datapoints/query`, credentials from `NETPIE_CLIENT_ID`/`NETPIE_TOKEN`) and merges them into `data/feed_all.csv`, one row per window: `time` (Bangkok), `ts_ms`, `washer`, `std`, `p2p`, `vstd`, `lo`, `hi`, `state`, `lid`. Re-running never duplicates a window, and rows that have expired on NETPIE stay in the file. Each run appends a line to `data/export_log.txt`. On the PC that collects the data, a Windows scheduled task **"WashWatch Feed export"** runs it daily at 21:00 (or at the next start-up if the PC was off), so nothing is lost to the 1-week TTL as long as the PC is switched on at least every few days. `data/` is git-ignored. Manual alternative: portal → device → Feed → **Exports** → CSV.

Records wait in a RAM buffer (3,000 records, about 50 min of both washers running) while the cloud is unreachable and upload oldest-first when it is back (E4). The dashboard's `log` object shows `buffered`, `dropped` (buffer overflowed) and `rejected` (NETPIE refused a batch 3 times). Limitation: the buffer is in RAM, so records still waiting are lost if the gateway reboots during an outage.

Simulation build: `pio run -d gateway -e gateway_sim` runs two simulated washer nodes on Wokwi's open Wi-Fi against the real NETPIE device in `secrets.h`. Add `-DSIM_OUTAGE` to its `build_flags` to cut the cloud off from 20 s to 50 s of uptime.

## Training the phase classifier
`tools/train_phase.py` trains the idle / wash / spin classifier from `data/feed_all.csv` and `data/labels.csv` (one row per watched wash: `date, washer, start, spin_start, done, lid_opened, program, load, notes`, times HH:MM Bangkok; the first run creates the file with this header). It labels the windows of each cycle (start → spin start = wash, soaks included; spin start → done = spin; 30 min before and after = idle; anything else is unobserved and unused), adds past-only context features over 1, 5 and 10 min (vstd mean/max, lo and hi means, fraction of windows with vstd ≥ 6 mg), trains a small decision tree, and tests it on whole cycles it did not see (GroupKFold by cycle) against the firmware's threshold rule on the same windows.

```
uv run --no-project --with pandas --with scikit-learn --with matplotlib python -I tools/train_phase.py --plot
```

Writes `data/model/report.md` (accuracy, per-class precision/recall, confusion matrix, per-cycle results, feature importance, the tree), `data/model/phase_model.h` (the tree as plain C if/else, `phase_model_predict(const float f[20])`, for on-device inference: E7) and, with `--plot`, one PNG per cycle in `data/model/cycles/` for checking labels or reading a spin start that wasn't noted. `--install` copies the header into `gateway/src/`. The C export is checked against scikit-learn in `tools/tests/test_train_phase.py` (compiled with gcc). **Not done yet:** computing the 20 features live on the gateway and calling the model there; that waits for a model trained on real cycles.

## Recording a cycle
The `logger` env (`node/logger/main.cpp`) streams every 100 Hz MPU6050 sample over USB as CSV: `seq,ms,ax,ay,az,lid` (raw counts, 8192 per g; `lid` 1 = open, and reads 1 when no reed switch is wired). It is for calibrating `STD_WASH_MG`, `STD_SPIN_MG`, `DONE_QUIET_MS`, `START_WINDOWS` and choosing richer features, so mount the board exactly as the node will be.

1. Wire the MPU6050 as on the node (SDA 21, SCL 22, AD0 to GND), reed switch on GPIO 25 to GND if you have it.
2. Mount it on the washer, plug it into a laptop and run `pio run -d node -e logger -t upload -t monitor`. The `log2file` filter saves everything to `node/platformio-device-monitor-*.log` (about 2.5 kB/s, roughly 9 MB per hour).
3. Start the monitor before the washer, run one full cycle, stop with Ctrl+C a few minutes after it ends. Note the program and load.

Gaps in `seq` mean dropped lines; an empty `ax,ay,az` means a failed read.

## Dashboard (NETPIE)
Portal → project WashWatch → **Console → Dashboard**. A dashboard reads the device through a **data source** added in its **Settings** tab ("Add source": Type Device, Device `washwatch-gateway`, **Alias `washwatch`**, privileges **Read Shadow + Read Feed** only). The alias is not part of an exported or imported file, so add it to any new dashboard before importing.

Widgets read the shadow as `#["washwatch"]["shadow"]["washers"]["1"]["state"]`, and VALUE fields are JavaScript expressions (checked 26 Oct 2026: a nested path and `(x == null ? "–" : x)` both render). A field published as `null` is removed from the shadow, so a guard must test `== null` (which also matches a missing field). A Gauge with a non-number value shows `NaN`, so its guard must fall back to a number.

`dashboard/template_export.json` is an export of the probe dashboard `washwatch-probe` (one Text, Indicator, Gauge and Chart widget with `@@...@@` placeholders). It is Freeboard JSON: `panes[].widgets[]` with `type` `text_widget` / `indicator` / `gauge` / `Chart` and a short random `id` on every pane and widget. The Chart's time window is a preset string (`query_range`, e.g. `"1d"`).

The full dashboard is generated: `python tools/dashboard.py` writes `dashboard/washwatch_dashboard.json`, and `uv run --no-project --with pytest python -I -m pytest tools/tests -q` checks it (every expression is run in Node against the firmware's sample shadow, an empty shadow and an offline washer). To install it: open the dashboard (the live one is **WashWatch**) → **⋯ → Import** → choose that file → **Save**.

NETPIE behaviours the generator works around (all seen live on 26–27 Oct 2026):
- **Indicator ON/OFF TEXT are JavaScript expressions**, so the generator writes them as string literals (a bare `closed` showed `false`: it is the browser global `window.closed`).
- **The device `status` is only read when the dashboard loads** (it stayed 0 after the device connected, and was 1 after a reload). A load-time `status` is stale either way, so the dashboard doesn't use it: the Gateway light is "online" while the shadow's `ts` is less than 90 s old, and a washer counts as online only if its own `online` is true **and** the gateway is online (otherwise its gauge shows 0, its lights are off and its Link shows –). Before the gateway's clock syncs (`ts` is seconds since boot, shown as "clock not synced") freshness can't be judged, so the washers' own `online` is trusted. Link shows the absolute time a node was last heard (`ts − age_s`), since `age_s` is only its age at publish time.
- **Expressions are only re-evaluated when data arrives** (27 Oct: after the publisher stopped, the Gateway light stayed "online" until a reload). NETPIE's data sources can only be devices, so there is no timer to force a refresh. The light and the washer states are therefore correct as of the last update or a reload; the Gateway panel's **Last update** time is what tells you on an open page that the gateway has gone quiet.
- Quirk in the portal: picking **Read Shadow** in a data source's privileges also silently adds **Write Shadow**. Check the privileges column after saving and edit it out.

Check the dashboard without hardware: `uv run --no-project --with "paho-mqtt<2" python tools/fake_shadow.py` (device credentials in env vars) publishes the sample shadow every 5 s: washer 1 counts down, after 60 s it is done (blinking light) and washer 2 goes offline. It writes the real device shadow, so don't run it while the real gateway is publishing.

