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
#include <atomic>
#include <functional>

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
  HotkeyDispatch(const Config& config, DictationController* controller,
                 std::atomic<bool>* ready = nullptr)
      : config_(config), controller_(controller), ready_(ready) {}

  // Optional: returns a human-readable reason the engine is not ready yet
  // (e.g. "loading model" vs "init failed"). Used to enrich the ignored-hotkey
  // log line and any UI feedback.
  void SetReadyReason(std::function<std::string()> fn) {
    ready_reason_ = std::move(fn);
  }

  // Optional: invoked (on the hook thread) whenever a hotkey arrives while the
  // engine is not ready, so the frontend can surface a hint to the user instead
  // of leaving the keypress with no visible effect.
  void SetOnIgnored(std::function<void(const std::string&)> fn) {
    on_ignored_ = std::move(fn);
  }

  // Called from the low-level hook thread with (def, is_down) events of
  // the configured hotkeys.
  void Handle(const HotkeyDef& def, bool is_down) {
    // Belt-and-suspenders: never act on a hotkey before the engine is ready.
    // The hook is installed before the (slower) model load in AppRuntime::Init,
    // so a Caps press during the ~3s load reaches this guard and is reported
    // (via SetOnIgnored) instead of silently doing nothing.
    if (ready_ && !ready_->load()) {
      std::string reason = ready_reason_ ? ready_reason_() : std::string();
      if (reason.empty()) reason = "语音引擎尚未就绪";
      LOG_WARN("快捷键被忽略（引擎未就绪）：%s。", reason.c_str());
      if (on_ignored_) on_ignored_(reason);
      return;
    }
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
  // When non-null, hotkeys are ignored until this flips to true (model ready).
  std::atomic<bool>* ready_ = nullptr;

  // Optional diagnostics hooks (see setters above).
  std::function<std::string()> ready_reason_;
  std::function<void(const std::string&)> on_ignored_;

  // Hook-thread-only state (the callback always runs on the listener
  // thread), so plain members are fine.
  bool press_started_recording_ = false;
  std::chrono::steady_clock::time_point press_time_{};
};

}  // namespace capswriter
