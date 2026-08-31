#include "window_util.h"

#include <windows.h>
// WIN32_LEAN_AND_MEAN 下 windows.h 不会引入 COM 头，导致 uiautomation.h
// 依赖的 `interface` 宏未定义（UIAutomationCore.h 用 typedef interface ...）。
// combaseapi.h 是 interface / IUnknown 的正式定义处，必须先于 uiautomation.h 包含。
#include <combaseapi.h>
#include <uiautomation.h>

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

namespace {

// Window classes whose focused control is (almost always) a text input.
// Case-insensitive; entries ending in a '.' also match longer dotted
// variants (e.g. "WindowsForms10.EDIT.app.0.378734a").
bool IsKnownEditClass(const std::wstring& cls) {
  static const wchar_t* kKnown[] = {
      L"Edit",            // Notepad, dialogs, old apps
      L"RichEdit",        L"RichEdit20A", L"RichEdit20W", L"RichEdit50W",
      L"RICHEDIT",        // WordPad / IME-adjacent rich text
      L"Scintilla",       // Notepad++, some editors
      L"QLineEdit",       L"QTextEdit", L"QPlainTextEdit", L"QTextBrowser",
      L"Qt6QLineEdit",    L"Qt6QTextEdit", L"Qt6QPlainTextEdit",
      L"Qt6QTextBrowser", // Qt apps
      L"ConsoleWindowClass",                // legacy conhost
      L"CASCADIA_HOSTING_WINDOW_CLASS",     // Windows Terminal
      L"Chrome_RenderWidgetHostHWND",       // Chromium web content
      L"Chrome_WidgetWin_1",                // Chromium top-level frames
      L"MozillaWindowClass", L"MozillaWindowClass2",  // Firefox
      L"WindowsForms10.EDIT.", L"WindowsForms10.RichEdit.",  // WinForms
  };
  for (const wchar_t* k : kKnown) {
    const size_t len = wcslen(k);
    if (wcsncmp(cls.c_str(), k, len) != 0) continue;
    if (len > 0 && k[len - 1] == L'.') return true;   // dotted prefix match
    if (cls.size() == len) return true;               // exact match
  }
  return false;
}

// Window classes that are definitely NOT text inputs; short-circuits the
// more expensive fallbacks (caret / UI Automation).
bool IsNonEditClass(const std::wstring& cls) {
  static const wchar_t* kNonEdit[] = {
      L"Button",         L"Static",         L"ScrollBar",
      L"ToolbarWindow32", L"msctls_statusbar32", L"msctls_trackbar32",
      L"SysListView32",  L"SysTreeView32",  L"SysHeader32",
      L"tooltips_class32", L"IME",           L"Shell_TrayWnd",
      L"Progman",        L"WorkerW",        L"#32770",
  };
  for (const wchar_t* k : kNonEdit) {
    if (_wcsicmp(cls.c_str(), k) == 0) return true;
  }
  return false;
}

// Shared "is this UIA element a text input?" check. Treats Edit and Document
// controls as editable; for shells (ComboBox/Custom/Pane/Group/TabItem/List)
// it requires a Value pattern that is not read-only — read-only fields also
// expose Value, but IsReadOnly tells them apart.
bool IsElementEditable(IUIAutomationElement* el) {
  CONTROLTYPEID type =
      0;  // UIA_UnknownControlTypeId (removed in SDK 10.0.26100+)
  if (FAILED(el->get_CurrentControlType(&type))) return false;
  if (type == UIA_EditControlTypeId || type == UIA_DocumentControlTypeId) {
    return true;
  }
  if (type == UIA_ComboBoxControlTypeId || type == UIA_CustomControlTypeId ||
      type == UIA_PaneControlTypeId || type == UIA_GroupControlTypeId ||
      type == UIA_TabItemControlTypeId || type == UIA_ListControlTypeId) {
    IUnknown* pattern = nullptr;
    if (SUCCEEDED(el->GetCurrentPattern(UIA_ValuePatternId, &pattern)) &&
        pattern) {
      IUIAutomationValuePattern* vp = nullptr;
      const bool has_vp =
          SUCCEEDED(pattern->QueryInterface(IID_IUIAutomationValuePattern,
                                            reinterpret_cast<void**>(&vp))) &&
          vp;
      pattern->Release();
      if (has_vp) {
        BOOL read_only = TRUE;
        vp->get_CurrentIsReadOnly(&read_only);
        const bool editable = !read_only;
        vp->Release();
        return editable;
      }
    }
  }
  return false;
}

// UI Automation fallback for custom / shell-rendered controls (Chrome,
// Edge, VS Code, Word, WeChat, ...). Checks the element under `hwnd`.
//
// Note: COM may already be initialized on this thread in another apartment
// mode (CoInitializeEx returns RPC_E_CHANGED_MODE). That does NOT make UIA
// unusable — the process-wide automation server adapts — so we only skip
// CoUninitialize in that case instead of bailing out.
bool IsEditableByUia(HWND hwnd) {
  bool editable = false;
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  // RPC_E_CHANGED_MODE / S_FALSE: COM already initialized here; usable.
  const bool need_uninit = (hr == S_OK);

  IUIAutomation* uia = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                 reinterpret_cast<void**>(&uia)))) {
    IUIAutomationElement* el = nullptr;
    if (SUCCEEDED(uia->ElementFromHandle(hwnd, &el)) && el) {
      editable = IsElementEditable(el);
      el->Release();
    }
    uia->Release();
  }
  if (need_uninit) CoUninitialize();
  return editable;
}

// Asks UIA directly which element owns the keyboard focus. More reliable
// than the class/caret heuristics for self-drawn UIs (Electron, Qt Quick,
// modern IMEs, ...) whose focused control is not a classic edit class and
// does not blink the system caret: GetFocusedElement returns the inner text
// field instead of the top-level HWND.
bool IsUiaFocusedElementEditable() {
  bool editable = false;
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool need_uninit = (hr == S_OK);

  IUIAutomation* uia = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                 reinterpret_cast<void**>(&uia)))) {
    IUIAutomationElement* el = nullptr;
    if (SUCCEEDED(uia->GetFocusedElement(&el)) && el) {
      editable = IsElementEditable(el);
      el->Release();
    }
    uia->Release();
  }
  if (need_uninit) CoUninitialize();
  return editable;
}

}  // namespace

bool HasTextInputFocus() {
  HWND fg = GetForegroundWindow();
  if (!fg) return false;

  // Fast path: inspect the focused control of the foreground thread. Covers
  // classic Win32 edit controls (and known shells) without touching UIA.
  const DWORD tid = GetWindowThreadProcessId(fg, nullptr);
  if (tid) {
    GUITHREADINFO gti{sizeof(GUITHREADINFO)};
    if (GetGUIThreadInfo(tid, &gti) && gti.hwndFocus) {
      wchar_t cls_buf[128]{};
      GetClassNameW(gti.hwndFocus, cls_buf, 128);
      const std::wstring cls(cls_buf);

      if (IsKnownEditClass(cls)) return true;
      if (IsNonEditClass(cls)) return false;
      // A visible blinking caret is a strong "text is being edited" signal
      // and covers custom grids (Excel) and shells that lack a
      // recognizable class.
      if (gti.flags & GUI_CARETBLINKING) return true;
      // Unrecognized class: ask UIA, first about the focused control, then
      // about the UIA focused element (self-drawn UIs report the inner text
      // field, not the top-level HWND).
      if (IsEditableByUia(gti.hwndFocus)) return true;
      return IsUiaFocusedElementEditable();
    }
  }

  // No usable GUI-thread info (UWP, remote sessions, some odd apps): ask
  // UIA which element owns the keyboard focus.
  return IsUiaFocusedElementEditable();
}

bool GetCaretScreenRect(RECT* out) {
  if (!out) return false;
  HWND fg = GetForegroundWindow();
  if (!fg) return false;
  const DWORD tid = GetWindowThreadProcessId(fg, nullptr);
  GUITHREADINFO gti{sizeof(GUITHREADINFO)};
  if (!tid || !GetGUIThreadInfo(tid, &gti)) return false;
  // The caret must be owned by the focused thread and have a nonzero size.
  if (!gti.hwndCaret && !(gti.flags & GUI_CARETBLINKING)) return false;
  if (gti.rcCaret.right <= gti.rcCaret.left ||
      gti.rcCaret.bottom <= gti.rcCaret.top) {
    return false;
  }
  *out = gti.rcCaret;  // GUITHREADINFO.rcCaret is in screen coordinates
  return true;
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
