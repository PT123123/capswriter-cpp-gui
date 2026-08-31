#include "hotkey_listener.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>

#include "log.h"

namespace capswriter {

namespace {

constexpr UINT kMsgKeyEvent = WM_APP + 1;
constexpr UINT kMsgQuit = WM_APP + 2;

HotkeyListener* g_listener = nullptr;
HWND g_hwnd = nullptr;
HHOOK g_kb_hook = nullptr;
HHOOK g_mouse_hook = nullptr;

std::string ToLower(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return out;
}

std::string TrimCopy(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Returns the index of the keyboard hotkey with this VK, or -1.
int FindKeyboardDef(DWORD vk) {
  if (!g_listener) return -1;
  const auto& defs = g_listener->hotkeys_for_lookup();
  for (size_t i = 0; i < defs.size(); ++i) {
    if (defs[i].vk != 0 && defs[i].vk == static_cast<int>(vk)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Returns the index of the mouse hotkey with this XBUTTON, or -1.
int FindMouseDef(int button) {
  if (!g_listener) return -1;
  const auto& defs = g_listener->hotkeys_for_lookup();
  for (size_t i = 0; i < defs.size(); ++i) {
    if (defs[i].mouse_button != 0 && defs[i].mouse_button == button) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

LRESULT CALLBACK KeyboardHookProc(int n_code, WPARAM w_param, LPARAM l_param) {
  if (n_code == HC_ACTION && g_listener && l_param) {
    const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l_param);
    const bool injected = (kb->flags & LLKHF_INJECTED) != 0;
    if (!injected) {
      const int def = FindKeyboardDef(kb->vkCode);
      if (def >= 0) {
        const bool is_down =
            (w_param == WM_KEYDOWN || w_param == WM_SYSKEYDOWN);
        const bool is_up = (w_param == WM_KEYUP || w_param == WM_SYSKEYUP);
        if (is_down || is_up) {
          // The heavy work happens in the message loop; posting keeps the
          // hook callback well within the LowLevelHooksTimeout limit.
          PostMessageW(g_hwnd, kMsgKeyEvent, static_cast<WPARAM>(def),
                       is_down ? 1 : 0);
        }
        if (g_listener->hotkeys_for_lookup()[def].suppress) {
          return 1;  // swallow the event
        }
      }
    }
  }
  return CallNextHookEx(g_kb_hook, n_code, w_param, l_param);
}

LRESULT CALLBACK MouseHookProc(int n_code, WPARAM w_param, LPARAM l_param) {
  if (n_code == HC_ACTION && g_listener && l_param &&
      (w_param == WM_XBUTTONDOWN || w_param == WM_XBUTTONUP ||
       w_param == WM_NCXBUTTONDOWN || w_param == WM_NCXBUTTONUP)) {
    const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(l_param);
    const int button = HIWORD(ms->mouseData);  // XBUTTON1 or XBUTTON2
    const int def = FindMouseDef(button);
    if (def >= 0) {
      const bool is_down =
          (w_param == WM_XBUTTONDOWN || w_param == WM_NCXBUTTONDOWN);
      PostMessageW(g_hwnd, kMsgKeyEvent, static_cast<WPARAM>(def),
                   is_down ? 1 : 0);
      if (g_listener->hotkeys_for_lookup()[def].suppress) {
        return 1;  // swallow the event
      }
    }
  }
  return CallNextHookEx(g_mouse_hook, n_code, w_param, l_param);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM w_param,
                            LPARAM l_param) {
  return DefWindowProcW(hwnd, msg, w_param, l_param);
}

}  // namespace

bool HotkeyListener::ParseKeyName(const std::string& raw_name,
                                  HotkeyDef* out) {
  const std::string name = ToLower(TrimCopy(raw_name));
  *out = HotkeyDef{};
  out->name = name;

  auto kb = [out](int vk) {
    out->vk = vk;
    return true;
  };

  // Mouse side buttons.
  if (name == "x1" || name == "mouse_x1" || name == "mouse4") {
    out->mouse_button = 1;
    return true;
  }
  if (name == "x2" || name == "mouse_x2" || name == "mouse5") {
    out->mouse_button = 2;
    return true;
  }

  // Lock keys.
  if (name == "caps_lock" || name == "capslock" || name == "caps")
    return kb(VK_CAPITAL);
  if (name == "num_lock" || name == "numlock") return kb(VK_NUMLOCK);
  if (name == "scroll_lock" || name == "scrolllock") return kb(VK_SCROLL);

  // Modifiers.
  if (name == "ctrl" || name == "control") return kb(VK_CONTROL);
  if (name == "ctrl_l" || name == "leftctrl" || name == "lctrl")
    return kb(VK_LCONTROL);
  if (name == "ctrl_r" || name == "rightctrl" || name == "rctrl")
    return kb(VK_RCONTROL);
  if (name == "shift" || name == "shift_l" || name == "leftshift")
    return kb(VK_LSHIFT);
  if (name == "shift_r" || name == "rightshift") return kb(VK_RSHIFT);
  if (name == "alt") return kb(VK_MENU);
  if (name == "alt_l" || name == "leftalt") return kb(VK_LMENU);
  if (name == "alt_gr" || name == "alt_r" || name == "rightalt")
    return kb(VK_RMENU);
  if (name == "cmd" || name == "win" || name == "super") return kb(VK_LWIN);
  if (name == "cmd_r" || name == "win_r") return kb(VK_RWIN);

  // Common keys.
  if (name == "space") return kb(VK_SPACE);
  if (name == "enter" || name == "return") return kb(VK_RETURN);
  if (name == "tab") return kb(VK_TAB);
  if (name == "backspace") return kb(VK_BACK);
  if (name == "delete") return kb(VK_DELETE);
  if (name == "insert") return kb(VK_INSERT);
  if (name == "home") return kb(VK_HOME);
  if (name == "end") return kb(VK_END);
  if (name == "page_up" || name == "pageup") return kb(VK_PRIOR);
  if (name == "page_down" || name == "pagedown") return kb(VK_NEXT);
  if (name == "esc" || name == "escape") return kb(VK_ESCAPE);
  if (name == "print_screen" || name == "printscreen") return kb(VK_SNAPSHOT);
  if (name == "pause") return kb(VK_PAUSE);
  if (name == "menu" || name == "apps") return kb(VK_APPS);

  // Arrow keys.
  if (name == "up") return kb(VK_UP);
  if (name == "down") return kb(VK_DOWN);
  if (name == "left") return kb(VK_LEFT);
  if (name == "right") return kb(VK_RIGHT);

  // Function keys.
  if (name.size() >= 2 && name[0] == 'f') {
    int n = 0;
    bool ok = true;
    for (size_t i = 1; i < name.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
        ok = false;
        break;
      }
      n = n * 10 + (name[i] - '0');
    }
    if (ok && n >= 1 && n <= 24) return kb(VK_F1 + (n - 1));
  }

  // Letters, digits and symbol keys via the current keyboard layout.
  if (name.size() == 1) {
    char c = name[0];
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return kb(c);
    SHORT scan = VkKeyScanA(c);
    if (scan != -1) return kb(scan & 0xFF);
  }
  return false;
}

HotkeyListener::~HotkeyListener() { Stop(); }

bool HotkeyListener::Start(std::string* err) {
  if (running_) return true;
  if (hotkeys_.empty()) {
    if (err) *err = "no hotkeys configured";
    return false;
  }
  g_listener = this;
  thread_ = std::thread(&HotkeyListener::ThreadMain, this);
  for (int i = 0; i < 100 && !running_; ++i) Sleep(10);
  if (!running_) {
    if (err) *err = "failed to install the input hooks";
    return false;
  }
  return true;
}

void HotkeyListener::Stop() {
  if (g_hwnd) PostMessageW(g_hwnd, kMsgQuit, 0, 0);
  if (thread_.joinable()) thread_.join();
  running_ = false;
  g_listener = nullptr;
}

void HotkeyListener::ThreadMain() {
  const wchar_t* class_name = L"CapsWriterCppHotkey";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = class_name;
  RegisterClassExW(&wc);

  g_hwnd = CreateWindowExW(0, class_name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                           nullptr, wc.hInstance, nullptr);
  if (!g_hwnd) {
    LOG_ERROR("Failed to create hotkey message window");
    return;
  }

  bool need_kb = false, need_mouse = false;
  for (const auto& def : hotkeys_) {
    if (def.vk != 0) need_kb = true;
    if (def.mouse_button != 0) need_mouse = true;
  }

  if (need_kb) {
    g_kb_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc,
                                  GetModuleHandleW(nullptr), 0);
    if (!g_kb_hook) {
      LOG_ERROR("SetWindowsHookEx(WH_KEYBOARD_LL) failed (error %lu)",
                GetLastError());
    }
  }
  if (need_mouse) {
    g_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc,
                                     GetModuleHandleW(nullptr), 0);
    if (!g_mouse_hook) {
      LOG_ERROR("SetWindowsHookEx(WH_MOUSE_LL) failed (error %lu)",
                GetLastError());
    }
  }
  if (!g_kb_hook && !g_mouse_hook) {
    DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
    return;
  }

  running_ = true;
  LOG_INFO("Input hooks installed (keyboard=%s, mouse=%s)",
           g_kb_hook ? "yes" : "no", g_mouse_hook ? "yes" : "no");

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (msg.message == kMsgQuit) break;
    if (msg.message == kMsgKeyEvent) {
      OnKeyEvent(static_cast<int>(msg.wParam), msg.lParam != 0);
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (g_kb_hook) UnhookWindowsHookEx(g_kb_hook);
  if (g_mouse_hook) UnhookWindowsHookEx(g_mouse_hook);
  g_kb_hook = nullptr;
  g_mouse_hook = nullptr;
  DestroyWindow(g_hwnd);
  g_hwnd = nullptr;
  running_ = false;
}

void HotkeyListener::OnKeyEvent(int def_index, bool is_down) {
  if (def_index < 0 || def_index >= static_cast<int>(hotkeys_.size())) return;
  const HotkeyDef& def = hotkeys_[def_index];

  if (def.vk != 0) {
    if (is_down) {
      if (!down_keyboard_.emplace(def.vk, def_index).second) return;
    } else {
      if (down_keyboard_.erase(def.vk) == 0) return;
    }
  } else {
    if (is_down) {
      if (!down_mouse_.emplace(def.mouse_button, def_index).second) return;
    } else {
      if (down_mouse_.erase(def.mouse_button) == 0) return;
    }
  }

  if (callback_) callback_(def, is_down);
}

void HotkeyListener::EmulateHotkey(const HotkeyDef& def) {
  INPUT inputs[2]{};
  if (def.vk != 0) {
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = static_cast<WORD>(def.vk);
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = static_cast<WORD>(def.vk);
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
  } else if (def.mouse_button != 0) {
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_XDOWN;
    inputs[0].mi.mouseData =
        (def.mouse_button == 1) ? XBUTTON1 : XBUTTON2;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_XUP;
    inputs[1].mi.mouseData =
        (def.mouse_button == 1) ? XBUTTON1 : XBUTTON2;
  } else {
    return;
  }
  SendInput(2, inputs, sizeof(INPUT));
}

}  // namespace capswriter
