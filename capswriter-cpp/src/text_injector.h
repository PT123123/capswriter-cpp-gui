#pragma once

#include <string>

namespace capswriter {

// Types UTF-8 text into the foreground window using SendInput with
// KEYEVENTF_UNICODE. Works for CJK text without touching the clipboard.
bool InjectUtf8Text(const std::string& utf8);

// Copies UTF-8 text to the clipboard, simulates Ctrl+V, and optionally
// restores the previous clipboard content (mirrors TextOutput._paste_text).
bool PasteUtf8Text(const std::string& utf8, bool restore_clip);

// Writes UTF-8 text to the clipboard without pasting. Used as the fallback
// when the hotkey fires outside a text input (no box to type into).
bool CopyUtf8Text(const std::string& utf8);

// Presses and releases the Enter key (mirrors _auto_enter).
void SendEnterKey();

}  // namespace capswriter
