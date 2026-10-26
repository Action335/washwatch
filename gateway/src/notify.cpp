#include "notify.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "secrets.h"
#include "state.h"

constexpr int MAX_TRIES = 3;
constexpr uint32_t RETRY_MS = 10000;
constexpr UBaseType_t NOTIFY_TASK_PRIO = 1;  // lowest: a slow HTTPS call must not delay anything
constexpr uint32_t NOTIFY_TASK_STACK = 8192;  // TLS needs the room

static const char* text_for(CycleEvent e) {
  switch (e) {
    case CycleEvent::Done: return "is done. Please collect your laundry.";
    case CycleEvent::Reminder: return "finished 15 minutes ago and the laundry is still inside.";
    case CycleEvent::Collected: return "is free now.";
    default: return nullptr;
  }
}

static bool post_discord(const char* content) {
  if (WiFi.status() != WL_CONNECTED || DISCORD_WEBHOOK_URL[0] == '\0') return false;
  WiFiClientSecure tls;
  // TODO(E5): pin Discord's root CA instead of skipping certificate checks.
  tls.setInsecure();
  HTTPClient http;
  if (!http.begin(tls, DISCORD_WEBHOOK_URL)) return false;
  http.addHeader("Content-Type", "application/json");
  char body[200];
  snprintf(body, sizeof(body), "{\"content\":\"%s\"}", content);  // content is our own text: no escaping needed
  int code = http.POST((uint8_t*)body, strlen(body));
  http.end();
  return code >= 200 && code < 300;  // Discord answers 204
}

static void notify_task(void*) {
  Notice n;
  for (;;) {
    if (xQueueReceive(q_notices, &n, portMAX_DELAY) != pdTRUE) continue;
    const char* what = text_for(n.event);
    if (what == nullptr) continue;
    char msg[150];
    snprintf(msg, sizeof(msg), "Washer %u %s", (unsigned)n.node_id, what);
    bool ok = false;
    for (int i = 0; i < MAX_TRIES && !ok; i++) {
      if (i > 0) vTaskDelay(pdMS_TO_TICKS(RETRY_MS));
      ok = post_discord(msg);
    }
    Serial.printf("notify: \"%s\" %s\n", msg, ok ? "sent" : "failed");
  }
}

void notify_start() {
  xTaskCreate(notify_task, "notify", NOTIFY_TASK_STACK, nullptr, NOTIFY_TASK_PRIO, nullptr);
}
