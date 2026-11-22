// WashWatch washer node: samples vibration at 100 Hz and sends one feature window every 2 s
// to the gateway over ESP-NOW. A lid change is sent straight away (reed switch ISR).
//
// Tasks:  sample_task (prio 4) -> q_events -> link_task (prio 3) -> ESP-NOW
//         lid ISR ---------------^
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <soc/rtc_cntl_reg.h>

#include "features.h"
#include "imu.h"
#include "imu_health.h"
#include "link.h"
#include "task_report.h"
#include "ww_msg.h"

#ifndef NODE_ID
#error "Build with -DNODE_ID=<1..254> (see platformio.ini envs)"
#endif

// Lid input level that means "open". Default HIGH: a plain reed switch (KY-021) to GND with the
// pull-up reads HIGH when the magnet is away. Comparator modules (KY-025) can be the other way round:
// if closing the lid prints "open", add -DLID_OPEN_LEVEL=LOW to that washer's env.
#ifndef LID_OPEN_LEVEL
#define LID_OPEN_LEVEL HIGH
#endif
constexpr int PIN_LID = 25;  // reed switch to GND, internal pull-up
constexpr uint32_t SAMPLE_PERIOD_MS = 10;  // 100 Hz
constexpr uint32_t SAMPLES_PER_WINDOW = SEND_PERIOD_MS / SAMPLE_PERIOD_MS;
static_assert(SAMPLES_PER_WINDOW == FEATURE_SAMPLES_PER_WINDOW, "the band filters are designed for this rate");
constexpr uint32_t LID_DEBOUNCE_MS = 30;
constexpr int WDT_TIMEOUT_S = 10;  // longest link_send (with a channel sweep) is about 1 s

constexpr UBaseType_t SAMPLE_TASK_PRIO = 4;  // fixed-rate sampling must not slip
constexpr UBaseType_t LINK_TASK_PRIO = 3;    // may block on retries; sampling carries on
constexpr uint32_t TASK_STACK = 4096;

enum class EventKind : uint8_t { Window, Lid };
struct Event {
  EventKind kind;
  node_msg_t stats;  // only the vibration fields (mean/std/p2p/vstd/lo/hi) are used
  bool imu_fault;
};

static QueueHandle_t q_events;

static void IRAM_ATTR on_lid_change() {
  Event e = {EventKind::Lid, {}, false};
  BaseType_t woken = pdFALSE;
  xQueueSendFromISR(q_events, &e, &woken);  // a full queue drops it; the next window carries the lid state
  if (woken) portYIELD_FROM_ISR();
}

static void sample_task(void*) {
  esp_task_wdt_add(nullptr);
  VibrationWindow window;  // |a| stats (mean, std, p2p) for the phase rule
  AxisFeatures axes;       // per-axis features (vstd, band RMS) for training the classifier
  bool fault = false;      // a read failed in this window
  ImuRecovery recovery;    // re-wakes a sensor that stopped giving data
  uint32_t samples = 0;    // sample periods so far in this window, read or not
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    esp_task_wdt_reset();
    float a[3];
    bool ok = imu_read_g(a);
    if (ok) {
      window.add(sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]));
      axes.add(a[0], a[1], a[2]);
    } else {
      fault = true;
    }
    if (recovery.update(ok)) Serial.printf("imu: no data, re-wake %s\n", imu_wake() ? "sent" : "failed");
    if (++samples < SAMPLES_PER_WINDOW) continue;
    samples = 0;

    Event e = {};
    e.kind = EventKind::Window;
    window.fill(e.stats);
    axes.fill(e.stats);
    e.imu_fault = fault || window.count() == 0;
    xQueueSend(q_events, &e, 0);  // full queue = the link is stuck; drop rather than stall sampling
    window.reset();
    axes.reset_window();
    fault = false;
  }
}

static void link_task(void*) {
  esp_task_wdt_add(nullptr);
  uint32_t seq = 0;
  Event last = {EventKind::Window, {}, true};  // until the first window: no vibration data
  for (;;) {
    esp_task_wdt_reset();
    Event e;
    if (xQueueReceive(q_events, &e, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
    if (e.kind == EventKind::Lid) {
      vTaskDelay(pdMS_TO_TICKS(LID_DEBOUNCE_MS));  // let the reed contact settle, then read it
    } else {
      last = e;
    }

    node_msg_t m = {};
    m.ver = MSG_VERSION;
    m.node_id = NODE_ID;
    m.seq = seq++;
    m.uptime_ms = millis();
    m.mean_mg = last.stats.mean_mg;
    m.std_mg = last.stats.std_mg;
    m.p2p_mg = last.stats.p2p_mg;
    m.vstd_mg = last.stats.vstd_mg;
    m.lo_mg = last.stats.lo_mg;
    m.hi_mg = last.stats.hi_mg;
    m.phase = last.imu_fault ? PHASE_IDLE : phase_from_std(last.stats.std_mg);
    m.flags = (last.imu_fault ? FLAG_IMU_FAULT : 0) | (digitalRead(PIN_LID) == LID_OPEN_LEVEL ? FLAG_LID_OPEN : 0);
    bool acked = link_send(m);

    Serial.printf("%s seq=%lu mean=%u std=%u p2p=%u vstd=%u lo=%u hi=%u phase=%u lid=%s%s ch=%u %s\n",
                  e.kind == EventKind::Lid ? "lid" : "win", (unsigned long)m.seq, m.mean_mg, m.std_mg,
                  m.p2p_mg, m.vstd_mg, m.lo_mg, m.hi_mg, m.phase, m.flags & FLAG_LID_OPEN ? "open" : "closed",
                  m.flags & FLAG_IMU_FAULT ? " IMU_FAULT" : "", link_channel(), acked ? "acked" : "gave_up");
  }
}

void setup() {
#ifdef NO_BROWNOUT
  // Some cheap DevKits dip just under the brownout threshold when the radio starts.
  // Only for boards that otherwise reboot forever; a real undervoltage then shows as other crashes.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif
  Serial.begin(115200);
  Serial.printf("WashWatch node %u starting\n", NODE_ID);
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  if (!imu_begin()) Serial.println("imu: MPU6050 not found (messages will carry IMU_FAULT)");
  q_events = xQueueCreate(4, sizeof(Event));
  pinMode(PIN_LID, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_LID), on_lid_change, CHANGE);
  link_begin();
  xTaskCreate(sample_task, "sample", TASK_STACK, nullptr, SAMPLE_TASK_PRIO, nullptr);
  xTaskCreate(link_task, "link", TASK_STACK, nullptr, LINK_TASK_PRIO, nullptr);
}

// All work happens in sample_task and link_task; loop only prints their stack use (E1).
static const char* const TASKS[] = {"sample", "link"};
void loop() { task_report_loop(TASKS, sizeof(TASKS) / sizeof(TASKS[0])); }
