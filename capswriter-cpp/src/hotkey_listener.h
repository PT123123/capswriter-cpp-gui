#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace capswriter {

// One configured hotkey, mirroring an entry of ClientConfig.shortcuts.
struct HotkeyDef {
  std::string name;          // config name, e.g. "caps_lock", "x2"
  int vk = 0;                // keyboard virtual key code (0 for mouse keys)
  int mouse_button = 0;      // 1 = x1, 2 = x2
  bool suppress = true;      // swallow events; short presses are re-emitted
  bool hold_mode = true;
};

// (def, is_down). Invoked on the listener's own thread, so handlers may
// take their time (recognition, typing) without blocking the OS hooks.
using KeyEventCallback = std::function<void(const HotkeyDef&, bool is_down)>;

// Global low-level keyboard + mouse hooks. Events belonging to configured
// hotkeys are optionally suppressed (swallowed); re-emitted events from
// EmulateHotkey() are detected as injected and ignored.
class HotkeyListener {
 public:
  HotkeyListener() = default;
  ~HotkeyListener();

  HotkeyListener(const HotkeyListener&) = delete;
  HotkeyListener& operator=(const HotkeyListener&) = delete;

  // Maps config key names ("caps_lock", "ctrl_r", "f12", "x2", "a", ...)
  // onto a HotkeyDef. Returns false for unknown names.
  static bool ParseKeyName(const std::string& name, HotkeyDef* out);

  void SetHotkeys(std::vector<HotkeyDef> defs) { hotkeys_ = std::move(defs); }
  void SetCallback(KeyEventCallback cb) { callback_ = std::move(cb); }

  bool Start(std::string* err);
  void Stop();

  // Re-emits a suppressed hotkey (press + release) via SendInput.
  static void EmulateHotkey(const HotkeyDef& def);

  // Read-only access for the hook callbacks (same translation unit).
  const std::vector<HotkeyDef>& hotkeys_for_lookup() const { return hotkeys_; }

 private:
  void ThreadMain();
  void OnKeyEvent(int def_index, bool is_down);

  std::vector<HotkeyDef> hotkeys_;
  KeyEventCallback callback_;
  std::thread thread_;
  std::atomic<bool> running_{false};

  // Hook-thread-only state.
  std::map<int, int> down_keyboard_;  // vk -> def index
  std::map<int, int> down_mouse_;     // button -> def index
};

}  // namespace capswriter
