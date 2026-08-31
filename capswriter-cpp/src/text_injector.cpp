#include "text_injector.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "log.h"

namespace capswriter {

namespace {

std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                              static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  if (n > 0) {
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                        static_cast<int>(utf8.size()), w.data(), n);
  }
  return w;
}

bool GetClipboardText(std::wstring* out) {
  if (!OpenClipboard(nullptr)) return false;
  bool ok = false;
  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  if (h) {
    const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
    if (p) {
      *out = p;
      GlobalUnlock(h);
      ok = true;
    }
  }
  CloseClipboard();
  return ok;
}

bool SetClipboardText(const std::wstring& text) {
  if (!OpenClipboard(nullptr)) return false;
  bool ok = false;
  if (EmptyClipboard()) {
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HANDLE h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
      wchar_t* p = static_cast<wchar_t*>(GlobalLock(h));
      if (p) {
        memcpy(p, text.c_str(), bytes);
        GlobalUnlock(h);
        ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
      }
      if (!ok) GlobalFree(h);
    }
  }
  CloseClipboard();
  return ok;
}

void SendCtrlV() {
  INPUT inputs[4]{};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = VK_CONTROL;
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = 'V';
  inputs[2].type = INPUT_KEYBOARD;
  inputs[2].ki.wVk = 'V';
  inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
  inputs[3].type = INPUT_KEYBOARD;
  inputs[3].ki.wVk = VK_CONTROL;
  inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(4, inputs, sizeof(INPUT));
}

}  // namespace

bool InjectUtf8Text(const std::string& utf8) {
  if (utf8.empty()) return true;
  const std::wstring wide = Utf8ToWide(utf8);

  // Two INPUT records (down + up) per UTF-16 code unit.
  std::vector<INPUT> inputs;
  inputs.reserve(wide.size() * 2);
  for (wchar_t c : wide) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wScan = c;
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    inputs.push_back(down);

    INPUT up = down;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    inputs.push_back(up);
  }

  const UINT kBatch = 128;
  size_t sent = 0;
  while (sent < inputs.size()) {
    UINT n = static_cast<UINT>(std::min<size_t>(kBatch, inputs.size() - sent));
    UINT ok = SendInput(n, inputs.data() + sent, sizeof(INPUT));
    if (ok != n) {
      LOG_ERROR("SendInput sent %u of %u (error %lu)", ok, n, GetLastError());
      return false;
    }
    sent += n;
  }
  return true;
}

bool PasteUtf8Text(const std::string& utf8, bool restore_clip) {
  if (utf8.empty()) return true;

  std::wstring saved;
  const bool had_clipboard = GetClipboardText(&saved);

  if (!SetClipboardText(Utf8ToWide(utf8))) {
    LOG_ERROR("Failed to write the result to the clipboard");
    return false;
  }
  SendCtrlV();

  if (restore_clip) {
    Sleep(100);  // give the target app time to read the clipboard
    if (had_clipboard) {
      SetClipboardText(saved);
    }
  }
  return true;
}

bool CopyUtf8Text(const std::string& utf8) {
  if (utf8.empty()) return true;
  return SetClipboardText(Utf8ToWide(utf8));
}

void SendEnterKey() {
  INPUT inputs[2]{};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = VK_RETURN;
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = VK_RETURN;
  inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(2, inputs, sizeof(INPUT));
}

}  // namespace capswriter
