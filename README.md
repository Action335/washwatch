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
- **Gateway** (`gateway/src/`): `espnow_rx` (prio 5) ACKs and feeds `WasherCycle`; `control` (4) drives the servos and reads PIR/DHT22/rain; `cloud` (2) publishes every 10 s; `notify` (1) posts Discord alerts on done, a 15-minute reminder, and collected.
- **Not yet done:** the phase is a threshold rule (`STD_WASH_MG`, `STD_SPIN_MG` in `features.h`) and minutes left is a nominal cycle length (`state.cpp`). Both are uncalibrated until real cycles are recorded; the trained classifier (E7) replaces the rule. MQTT over TLS and Discord certificate checks (E5) and offline buffering (E4 store-and-forward) are still to do.

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

Still to fill in after running `python tools/netpie_probe.py` with the device credentials:
- Batch topic that acknowledges: TODO
- Success ack JSON: TODO
- 101-record batch reply (expect errcode 429): TODO
- Shadow writes counted for one 2-record batch: TODO

## Training data (for the phase classifier)
The gateway logs every window while a washer is active, one idle window per 30 s, and every lid change, as Feed points `w<id>_std`, `w<id>_p2p`, `w<id>_phase`, `w<id>_state` (0 idle, 1 running, 2 done) and `w<id>_lid`. In the NETPIE portal, create those 10 datatags (washers 1 and 2) with **TTL 14 days**: that keeps storage inside the free 1,000,000 point-month.

To download: portal → device → Feed → **Exports** → pick the `w1_*` / `w2_*` datatags and a date range → CSV. **Export at least once a week**, because points older than 14 days are deleted.

Records wait in a RAM buffer (3,000 records, about 50 min of both washers running) while the cloud is unreachable and upload oldest-first when it is back (E4). The dashboard's `log` object shows `buffered`, `dropped` (buffer overflowed) and `rejected` (NETPIE refused a batch 3 times). Limitation: the buffer is in RAM, so records still waiting are lost if the gateway reboots during an outage.

Simulation build: `pio run -d gateway -e gateway_sim` runs two simulated washer nodes on Wokwi's open Wi-Fi against the real NETPIE device in `secrets.h`. Add `-DSIM_OUTAGE` to its `build_flags` to cut the cloud off from 20 s to 50 s of uptime.
