# WashWatch firmware (from the AirBox pipeline)

2110356 Embedded Systems final project, 2026. This code started as AirBox (dropped 26 Oct 2026). The node→gateway→cloud pipeline is kept for WashWatch; the message format and node sensors are still AirBox-shaped and get replaced when the WashWatch node is written.

| Folder | What |
|---|---|
| `lib/airbox_proto/` | Shared, Arduino-free protocol code: message format, ACK/retry, node table, NETPIE payload |
| `proto_tests/` | Unit tests for `airbox_proto`, run on the PC |
| `node/` | Sensor node firmware (ESP32): envs `indoor`, `outdoor`, `sim` |
| `gateway/` | Gateway firmware |
| `dashboard/` | NETPIE schema and Freeboard export |
| `tools/` | Helper scripts |

## Setup

1. Install PlatformIO (VS Code extension or `pip install platformio`) and a host gcc for the PC tests (Windows: `winget install BrechtSanders.WinLibs.POSIX.UCRT`, then put its `mingw64/bin` on PATH).
2. Copy `secrets.h.example` to `secrets.h` and fill it in. `secrets.h` is gitignored: never commit it. Only the gateway uses it: nodes find the gateway by sweeping channels, so switching to a phone hotspot means re-flashing the gateway only.
3. Before building the dashboard, run `python tools/netpie_probe.py` once and write its three results here.

## Commands

```sh
pio test -d proto_tests -e native            # all PC unit tests
pio test -d proto_tests -e native -f test_sender  # one suite
pio run -d node -e indoor -t upload -t monitor
pio run -d gateway -t upload -t monitor
```
