#pragma once

// Shared hotkey press/release state machine used by both the console
// frontend (src/main.cpp) and the Qt tray frontend (gui/main.cpp).
// Mirrors ShortcutEventHandler of the Python client:
//  - hold mode: press starts the recording; release finishes it, or
//    cancels + re-emits the key when the press was shorter than
//    config.threshold
//  - click mode: a tap shorter than threshold * 0.8 toggles the
//    recording; holding longer than the tap window aborts it

#include <chrono>

#include <windows.h>

#include "config.h"
#include "dictation.h"
#include "hotkey_listener.h"
#include "log.h"

namespace capswriter {

// A click-mode press counts as a "tap" when released within
// 0.8 * threshold (see ShortcutEventHandler._manage_task).
inline constexpr double kClickWindowFactor = 0.8;

inline bool IsToggleKey(const HotkeyDef& def) {
  return def.vk == VK_CAPITAL || def.vk == VK_NUMLOCK || def.vk == VK_SCROLL;
}

class HotkeyDispatch {
 public:
  HotkeyDispatch(const Config& config, DictationController* controller)
      : config_(config), controller_(controller) {}

  // Called from the low-level hook thread with (def, is_down) events of
  // the configured hotkeys.
  void Handle(const HotkeyDef& def, bool is_down) {
    if (is_down) {
      press_started_recording_ = controller_->StartRecording();
      press_time_ = std::chrono::steady_clock::now();
      if (!def.hold_mode && controller_->recording() &&
          !press_started_recording_) {
        LOG_INFO("Click mode: second tap will stop the recording.");
      }
      return;
    }

    const double press_elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      press_time_)
            .count();

    if (def.hold_mode) {
      if (!controller_->recording()) return;

      // Duration is measured from the recording start, like the Python
      // client (task.recording_start_time).
      const double duration = controller_->recording_duration();
      if (duration < config_.threshold) {
        // Short press: cancel recognition; in suppress mode re-emit the
        // key so its normal function (e.g. toggling caps) still works.
        controller_->CancelRecording();
        if (def.suppress) HotkeyListener::EmulateHotkey(def);
        LOG_INFO("Short press (%.2fs < %.1fs), key re-emitted.", duration,
                 config_.threshold);
      } else {
        controller_->FinishRecording();
        // Non-suppressed toggle keys (caps/num/scroll lock) were toggled
        // by the press; re-emit to restore the previous state, like the
        // Python client does for its non-blocking mode.
        if (!def.suppress && IsToggleKey(def)) {
          HotkeyListener::EmulateHotkey(def);
        }
      }
      return;
    }

    // Click mode.
    const double window = config_.threshold * kClickWindowFactor;
    if (press_started_recording_) {
      // This press began the recording: a quick tap keeps it running
      // (dictation continues), holding too long aborts it.
      if (press_elapsed >= window) {
        controller_->CancelRecording();
        LOG_INFO("Click mode: press held too long (%.2fs), recording aborted.",
                 press_elapsed);
      }
    } else if (controller_->recording()) {
      // A press while already recording: a quick tap stops and recognizes.
      if (press_elapsed < window) {
        controller_->FinishRecording();
        if (!def.suppress && IsToggleKey(def)) {
          HotkeyListener::EmulateHotkey(def);
        }
      }
    }
    press_started_recording_ = false;
  }

 private:
  const Config& config_;
  DictationController* controller_;

  // Hook-thread-only state (the callback always runs on the listener
  // thread), so plain members are fine.
  bool press_started_recording_ = false;
  std::chrono::steady_clock::time_point press_time_{};
};

}  // namespace capswriter
