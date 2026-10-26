#include "washer_cycle.h"

CycleEvent WasherCycle::on_message(const node_msg_t& m, uint32_t now_ms) {
  const bool lid_open = m.flags & FLAG_LID_OPEN;

  if (m.flags & FLAG_IMU_FAULT) {
    // No vibration data: only the lid can still tell us the laundry was collected.
    if (state_ == WasherState::Done && lid_open) {
      state_ = WasherState::Idle;
      return CycleEvent::Collected;
    }
    return CycleEvent::None;
  }

  const bool moving = m.phase != PHASE_IDLE;
  // Quiet is measured from the first quiet message we actually received, so a silent
  // (offline) node never counts as quiet time.
  if (moving) {
    quiet_ = false;
  } else if (!quiet_) {
    quiet_ = true;
    quiet_since_ms_ = now_ms;
  }
  // Only sustained motion counts as a washer running: a bump or the washer next door spinning
  // gives a few moving windows, a real cycle many in a row.
  if (!moving) {
    moving_run_ = 0;
  } else if (moving_run_ == 0) {
    moving_run_ = 1;
    run_start_ms_ = now_ms;
  } else if (moving_run_ < UINT8_MAX) {
    moving_run_++;
  }
  const bool sustained = moving_run_ >= START_WINDOWS;

  switch (state_) {
    case WasherState::Idle:
      if (sustained) {
        state_ = WasherState::Running;
        started_ms_ = run_start_ms_;  // the cycle began at the first moving window
        return CycleEvent::Started;
      }
      return CycleEvent::None;

    case WasherState::Running:
      if (quiet_ && now_ms - quiet_since_ms_ >= DONE_QUIET_MS) {
        state_ = WasherState::Done;
        done_ms_ = now_ms;
        reminded_ = false;
        return CycleEvent::Done;
      }
      return CycleEvent::None;

    case WasherState::Done:
      if (lid_open) {
        state_ = WasherState::Idle;
        return CycleEvent::Collected;
      }
      if (sustained) {
        state_ = WasherState::Running;  // a long soak, not the end; keep the start time
        return CycleEvent::None;
      }
      if (!reminded_ && now_ms - done_ms_ >= REMIND_AFTER_MS) {
        reminded_ = true;
        return CycleEvent::Reminder;
      }
      return CycleEvent::None;
  }
  return CycleEvent::None;
}

uint16_t WasherCycle::minutes_left(uint32_t now_ms) const {
  if (state_ != WasherState::Running) return 0;
  uint32_t elapsed_min = (now_ms - started_ms_) / 60000;
  return elapsed_min + 1 > nominal_min_ ? 1 : nominal_min_ - elapsed_min;
}
