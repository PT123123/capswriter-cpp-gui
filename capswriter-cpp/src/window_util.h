#pragma once

#include <string>

#include <windows.h>

namespace capswriter {

// Lower-cased executable name of the foreground window, e.g. "weixin.exe".
// Returns an empty string on failure.
std::string GetForegroundProcessName();

// True when the foreground window currently has an active text-input
// context: a focused native edit control, an editable document (Chrome,
// Office, ...), or a visible caret. Used to decide whether recognized text
// should be typed into the focused box or copied to the clipboard instead.
bool HasTextInputFocus();

// Screen-space rectangle (in physical pixels) of the caret of the focused
// text input. Returns false when no usable caret is present.
bool GetCaretScreenRect(RECT* out);

// Semantic unit count, mirroring count_semantic_units() in
// core/client/output/text_output.py: one CJK char = 1, one run of ASCII
// letters = 1, one run of digits = 1.
int CountSemanticUnits(const std::string& utf8);

// Removes the last character of `text` when it is contained in the UTF-8
// character set `punc_set` (e.g. "，。,.").
std::string StripTrailingPunc(const std::string& text,
                              const std::string& punc_set);

}  // namespace capswriter
