// Gateway-side cycle logic for one washer: IDLE -> RUNNING -> DONE -> (lid opened) IDLE.
// Driven only by node messages, so a node that goes silent can't fake a finished cycle.
// Time is passed in, so it runs on the PC too.
#pragma once
#include <stdint.h>
#include "ww_msg.h"

// UNCALIBRATED: set from real cycles (the longest mid-cycle soak must stay below DONE_QUIET_MS).
// 10 min because washer 2's mid-cycle quiet gaps ran 6-10 min on 7 Nov 2026 (4 min gave a false
// "done"). Candidate replacement once MPU6050 data exists: "spin seen, then quiet >= 2 min".
constexpr uint32_t DONE_QUIET_MS = 10 * 60 * 1000;    // quiet this long after running = done
constexpr uint32_t REMIND_AFTER_MS = 15 * 60 * 1000;  // done and still not collected
constexpr uint8_t START_WINDOWS = 15;  // moving windows in a row (30 s) before a cycle starts or resumes

enum class WasherState : uint8_t { Idle, Running, Done };
enum class CycleEvent : uint8_t { None, Started, Done, Reminder, Collected };

class WasherCycle {
 public:
  // nominal_min: the washer's usual cycle length, used for minutes left until the
  // learned estimate replaces it.
  explicit WasherCycle(uint16_t nominal_min) : nominal_min_(nominal_min) {}
  CycleEvent on_message(const node_msg_t& m, uint32_t now_ms);
  WasherState state() const { return state_; }
  // 0 unless running; while running it counts down but stays at least 1.
  uint16_t minutes_left(uint32_t now_ms) const;

 private:
  uint16_t nominal_min_;
  WasherState state_ = WasherState::Idle;
  uint32_t started_ms_ = 0;
  uint32_t done_ms_ = 0;
  uint32_t quiet_since_ms_ = 0;
  bool quiet_ = false;     // the last message showed no motion
  uint8_t moving_run_ = 0;     // moving messages in a row (stops counting at 255)
  uint32_t run_start_ms_ = 0;  // time of the first message in that run
  bool reminded_ = false;  // the reminder for this Done was sent
};
