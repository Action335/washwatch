// WashWatch washer node: samples vibration at 100 Hz and sends one feature window every 2 s
// to the gateway over ESP-NOW. A lid change is sent straight away (reed switch ISR).
//
// Tasks:  sample_task (prio 4) -> q_events -> link_task (prio 3) -> ESP-NOW
//         lid ISR ---------------^
#include <Arduino.h>
#include <esp_task_wdt.h>

#include "features.h"
#include "imu.h"
#include "link.h"
#include "task_report.h"
#include "ww_msg.h"

#ifndef NODE_ID
#error "Build with -DNODE_ID=<1..254> (see platformio.ini envs)"
#endif

constexpr int PIN_LID = 25;  // reed switch to GND, internal pull-up: HIGH = lid open
constexpr uint32_t SAMPLE_PERIOD_MS = 10;  // 100 Hz
constexpr uint32_t SAMPLES_PER_WINDOW = SEND_PERIOD_MS / SAMPLE_PERIOD_MS;
constexpr uint32_t LID_DEBOUNCE_MS = 30;
constexpr int WDT_TIMEOUT_S = 10;  // longest link_send (with a channel sweep) is about 1 s

constexpr UBaseType_t SAMPLE_TASK_PRIO = 4;  // fixed-rate sampling must not slip
constexpr UBaseType_t LINK_TASK_PRIO = 3;    // may block on retries; sampling carries on
constexpr uint32_t TASK_STACK = 4096;

enum class EventKind : uint8_t { Window, Lid };
struct Event {
  EventKind kind;
  uint16_t mean_mg, std_mg, p2p_mg;
  bool imu_fault;
};

static QueueHandle_t q_events;

static void IRAM_ATTR on_lid_change() {
  Event e = {EventKind::Lid, 0, 0, 0, false};
  BaseType_t woken = pdFALSE;
  xQueueSendFromISR(q_events, &e, &woken);  // a full queue drops it; the next window carries the lid state
  if (woken) portYIELD_FROM_ISR();
}

static void sample_task(void*) {
  esp_task_wdt_add(nullptr);
  VibrationWindow window;
  bool fault = false;     // a read failed in this window
  uint32_t samples = 0;   // sample periods so far in this window, read or not
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    esp_task_wdt_reset();
    float mag;
    if (imu_read_magnitude(mag)) {
      window.add(mag);
    } else {
      fault = true;
    }
    if (++samples < SAMPLES_PER_WINDOW) continue;
    samples = 0;

    node_msg_t stats = {};
    window.fill(stats);
    Event e = {EventKind::Window, stats.mean_mg, stats.std_mg, stats.p2p_mg, fault || window.count() == 0};
    xQueueSend(q_events, &e, 0);  // full queue = the link is stuck; drop rather than stall sampling
    window.reset();
    fault = false;
  }
}

static void link_task(void*) {
  esp_task_wdt_add(nullptr);
  uint32_t seq = 0;
  Event last = {EventKind::Window, 0, 0, 0, true};  // until the first window: no vibration data
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
    m.mean_mg = last.mean_mg;
    m.std_mg = last.std_mg;
    m.p2p_mg = last.p2p_mg;
    m.phase = last.imu_fault ? PHASE_IDLE : phase_from_std(last.std_mg);
    m.flags = (last.imu_fault ? FLAG_IMU_FAULT : 0) | (digitalRead(PIN_LID) == HIGH ? FLAG_LID_OPEN : 0);
    bool acked = link_send(m);

    Serial.printf("%s seq=%lu mean=%u std=%u p2p=%u phase=%u lid=%s%s ch=%u %s\n",
                  e.kind == EventKind::Lid ? "lid" : "win", (unsigned long)m.seq, m.mean_mg, m.std_mg,
                  m.p2p_mg, m.phase, m.flags & FLAG_LID_OPEN ? "open" : "closed",
                  m.flags & FLAG_IMU_FAULT ? " IMU_FAULT" : "", link_channel(), acked ? "acked" : "gave_up");
  }
}

void setup() {
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
