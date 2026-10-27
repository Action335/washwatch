// Prints each task's priority and free stack, plus the free heap, on the serial monitor:
// evidence for E1 and a check that no stack is close to overflowing. ESP32 only.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

constexpr uint32_t TASK_REPORT_FIRST_MS = 10000;    // once the tasks have run a little
constexpr uint32_t TASK_REPORT_PERIOD_MS = 300000;  // then every 5 minutes

// names: the tasks to report; a name with no running task is skipped (e.g. sim-only tasks).
inline void print_task_report(const char* const names[], size_t count) {
  Serial.printf("tasks: %u running, free heap %u bytes\n", (unsigned)uxTaskGetNumberOfTasks(),
                (unsigned)ESP.getFreeHeap());
  for (size_t i = 0; i < count; i++) {
    TaskHandle_t h = xTaskGetHandle(names[i]);
    if (h == nullptr) continue;
    // On ESP32 stack sizes are in bytes, so the high-water mark is too.
    Serial.printf("  %-10s prio %u  stack free %u bytes\n", names[i], (unsigned)uxTaskPriorityGet(h),
                  (unsigned)uxTaskGetStackHighWaterMark(h));
  }
}

// Call from loop(): waits, then prints the report.
inline void task_report_loop(const char* const names[], size_t count) {
  static bool first = true;
  vTaskDelay(pdMS_TO_TICKS(first ? TASK_REPORT_FIRST_MS : TASK_REPORT_PERIOD_MS));
  first = false;
  print_task_report(names, count);
}
