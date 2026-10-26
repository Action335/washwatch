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
