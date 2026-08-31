#include "window_util.h"

#include <windows.h>

#include <vector>

namespace capswriter {

namespace {

std::string WideToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr,
                              nullptr);
  std::string s(n ? n - 1 : 0, '\0');
  if (n > 1) {
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr,
                        nullptr);
  }
  return s;
}

// Decodes one UTF-8 code point at position i; advances i.
char32_t NextCodePoint(const std::string& s, size_t& i) {
  const unsigned char c = static_cast<unsigned char>(s[i]);
  char32_t cp = 0;
  int extra = 0;
  if (c < 0x80) {
    cp = c;
  } else if ((c & 0xE0) == 0xC0) {
    cp = c & 0x1F;
    extra = 1;
  } else if ((c & 0xF0) == 0xE0) {
    cp = c & 0x0F;
    extra = 2;
  } else if ((c & 0xF8) == 0xF0) {
    cp = c & 0x07;
    extra = 3;
  } else {
    ++i;
    return 0xFFFD;
  }
  ++i;
  for (int k = 0; k < extra && i < s.size(); ++k, ++i) {
    const unsigned char b = static_cast<unsigned char>(s[i]);
    if ((b & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | (b & 0x3F);
  }
  return cp;
}

}  // namespace

std::string GetForegroundProcessName() {
  HWND hwnd = GetForegroundWindow();
  if (!hwnd) return {};
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == 0) return {};

  HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!proc) return {};
  wchar_t buf[MAX_PATH];
  DWORD len = MAX_PATH;
  std::string name;
  if (QueryFullProcessImageNameW(proc, 0, buf, &len)) {
    std::wstring path(buf, len);
    size_t pos = path.find_last_of(L"\\/");
    name = WideToUtf8(pos == std::string::npos ? path
                                               : path.substr(pos + 1));
  }
  CloseHandle(proc);

  for (char& c : name) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return name;
}

int CountSemanticUnits(const std::string& utf8) {
  int units = 0;
  bool in_ascii_letters = false;
  bool in_digits = false;

  size_t i = 0;
  while (i < utf8.size()) {
    const char32_t cp = NextCodePoint(utf8, i);
    const bool ascii_letter =
        (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
    const bool digit = cp >= '0' && cp <= '9';

    if (ascii_letter) {
      if (!in_ascii_letters) ++units;
      in_ascii_letters = true;
      in_digits = false;
    } else if (digit) {
      if (!in_digits) ++units;
      in_digits = true;
      in_ascii_letters = false;
    } else {
      in_ascii_letters = false;
      in_digits = false;
      // CJK and other scripts: each code point is one unit
      // (covers the ranges used by the original regex).
      if ((cp >= 0x4E00 && cp <= 0x9FFF) ||    // CJK unified
          (cp >= 0x3400 && cp <= 0x4DBF) ||    // extension A
          (cp >= 0xF900 && cp <= 0xFAFF)) {    // compatibility ideographs
        ++units;
      }
    }
  }
  return units;
}

std::string StripTrailingPunc(const std::string& text,
                              const std::string& punc_set) {
  if (text.empty() || punc_set.empty()) return text;

  // Build the set of punctuation code points.
  std::vector<char32_t> puncs;
  {
    size_t i = 0;
    while (i < punc_set.size()) puncs.push_back(NextCodePoint(punc_set, i));
  }

  // Find the last code point of text.
  size_t last_start = 0;
  size_t i = 0;
  char32_t last_cp = 0;
  while (i < text.size()) {
    last_start = i;
    last_cp = NextCodePoint(text, i);
  }
  if (last_start == 0 && text.empty()) return text;

  for (char32_t p : puncs) {
    if (p == last_cp) return text.substr(0, last_start);
  }
  return text;
}

}  // namespace capswriter
