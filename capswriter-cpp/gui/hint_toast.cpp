#include "hint_toast.h"

#include <windows.h>

#include <cstring>

#include <QFont>
#include <QFontMetrics>
#include <QPainter>

#include "../src/log.h"
#include "../src/window_util.h"

namespace capswriter {

namespace {

const wchar_t kToastClass[] = L"CapsWriterHintToast";

// Vertical gap between the caret and the hint bar.
constexpr int kCaretGap = 6;
// Offset of the hint bar relative to the mouse cursor when no caret exists.
constexpr int kCursorOffsetX = 12;
constexpr int kCursorOffsetY = 18;

// Registers the toast window class once per process.
void EnsureClassRegistered() {
  static bool done = [] {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;  // layered windows need no painting
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kToastClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    return true;
  }();
  (void)done;
}

// Per-monitor DPI scale relative to 96 DPI (= 100%). Requires the process to
// be per-monitor DPI aware for the true value (Qt6 enables this); otherwise
// GetDpiForMonitor reports the system DPI and we still fall back gracefully.
// Resolved via GetProcAddress so we don't hard-link shcore.dll.
qreal DpiScaleForMonitor(HMONITOR mon) {
  if (mon) {
    using GetDpiForMonitorFn =
        HRESULT(WINAPI*)(HMONITOR, int /*MONITOR_DPI_TYPE*/, UINT*, UINT*);
    static const auto fn = []() -> GetDpiForMonitorFn {
      HMODULE shcore = GetModuleHandleW(L"shcore.dll");
      return shcore ? reinterpret_cast<GetDpiForMonitorFn>(
                          GetProcAddress(shcore, "GetDpiForMonitor"))
                    : nullptr;
    }();
    UINT dpi_x = 96, dpi_y = 96;
    if (fn && fn(mon, /*MDT_EFFECTIVE_DPI*/ 0, &dpi_x, &dpi_y) == S_OK &&
        dpi_x > 0) {
      return dpi_x / 96.0;
    }
  }
  // Fallback: system DPI (the old behavior).
  HDC dc = GetDC(nullptr);
  const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
  if (dc) ReleaseDC(nullptr, dc);
  return dpi > 0 ? dpi / 96.0 : 1.0;
}

// Resolution-aware scale so the hint isn't a postage stamp on dense panels.
// A 1080p display at 100% scaling is the 1.0 baseline; taller (physically
// higher-pixel) screens get a proportionally larger toast, clamped to a sane
// range so 4K/8K panels don't blow it up unnecessarily.
qreal ResolutionScaleForMonitor(HMONITOR mon) {
  MONITORINFOEXW mi{};
  mi.cbSize = sizeof(mi);
  if (!mon || !GetMonitorInfoW(mon, &mi)) return 1.0;
  const int height_px = mi.rcMonitor.bottom - mi.rcMonitor.top;
  if (height_px <= 0) return 1.0;
  const qreal k = static_cast<qreal>(height_px) / 1080.0;
  return qBound(1.0, k, 2.0);
}

}  // namespace

HintToast::HintToast(QObject* parent) : QObject(parent) {
  timer_.setInterval(100);
  timer_.setSingleShot(false);
  connect(&timer_, &QTimer::timeout, this, &HintToast::Tick);
}

HintToast::~HintToast() {
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

bool HintToast::EnsureWindow() {
  if (hwnd_) return true;
  EnsureClassRegistered();
  // WS_EX_LAYERED + WS_EX_TOPMOST + WS_EX_TOOLWINDOW: floating hint that is
  // never in the taskbar; WS_EX_NOACTIVATE: never steals focus from the
  // target app; WS_EX_TRANSPARENT: mouse clicks pass straight through.
  hwnd_ = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
          WS_EX_TRANSPARENT,
      kToastClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
      GetModuleHandleW(nullptr), nullptr);
  return hwnd_ != nullptr;
}

void HintToast::Show(const QString& text, int duration_ms) {
  if (!EnsureWindow()) {
    LOG_ERROR("HintToast: EnsureWindow failed (CreateWindowExW returned null)");
    return;
  }
  Render(text);
  if (image_.isNull()) {
    LOG_ERROR("HintToast: Render produced null image for text='%s'",
              text.toUtf8().constData());
    return;
  }
  pos_ = ComputePosition();
  ShowWindow(hwnd_, SW_SHOWNA);  // show without activating
  PushFrame();
  visible_ = true;
  remain_ms_ = duration_ms;
  timer_.start();
  LOG_INFO("HintToast: Show hwnd=%p text='%s' size=%dx%d pos=(%d,%d) duration=%dms",
           hwnd_, text.toUtf8().constData(), image_.width(), image_.height(),
           pos_.x(), pos_.y(), duration_ms);
}

void HintToast::Hide() {
  timer_.stop();
  remain_ms_ = 0;
  visible_ = false;
  if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void HintToast::Tick() {
  if (!visible_) return;
  // The hint is anchored once at Show() time; the timer only handles
  // auto-hide. (Recomputing the position every 100 ms made the toast chase
  // the mouse during dictation, which users found distracting.)
  if (remain_ms_ > 0) {
    remain_ms_ -= timer_.interval();
    if (remain_ms_ <= 0) Hide();
  }
}

void HintToast::Render(const QString& text) {
  // Size the toast for the monitor it will actually appear on (the caret, or
  // the cursor as a fallback). We measure that point's monitor and scale the
  // font/padding/radius so the hint is a comfortable, consistent size on
  // every display — instead of always using the primary monitor's DPI, which
  // made it look tiny on high-DPI / high-resolution secondary screens.
  const QPoint ref = ReferencePoint();
  const qreal scale = ToastScaleForPoint(ref);

  QFont font;
  font.setPixelSize(qRound(14 * scale));
  const QFontMetrics fm(font);
  const int pad_x = qRound(14 * scale);
  const int pad_y = qRound(8 * scale);
  const int border_w = qMax(1, qRound(1 * scale));
  const int radius = qRound(8 * scale);

  const int w = fm.horizontalAdvance(text) + pad_x * 2;
  const int h = fm.height() + pad_y * 2 + border_w * 2;

  image_ = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
  image_.fill(Qt::transparent);

  QPainter p(&image_);
  p.setRenderHint(QPainter::Antialiasing);
  const QRect rect(0, 0, w, h);
  p.setPen(QPen(QColor(74, 144, 196), border_w));  // #4a90c4 accent
  p.setBrush(QColor(30, 30, 34, 235));
  p.drawRoundedRect(rect, radius, radius);
  p.setPen(QColor(240, 240, 240));
  p.setFont(font);
  p.drawText(rect.adjusted(pad_x, 0, -pad_x, 0), Qt::AlignVCenter | Qt::AlignLeft,
             text);
  p.end();
}

QPoint HintToast::ReferencePoint() const {
  if (anchor_override_) return *anchor_override_;
  RECT caret{};
  if (GetCaretScreenRect(&caret)) {
    return QPoint(caret.left, caret.bottom);
  }
  POINT cursor{};
  GetCursorPos(&cursor);
  return QPoint(cursor.x, cursor.y);
}

qreal HintToast::ToastScaleForPoint(const QPoint& ref) const {
  const HMONITOR mon =
      MonitorFromPoint({ref.x(), ref.y()}, MONITOR_DEFAULTTONEAREST);
  // Take the larger of the two so a high-DPI OR a high-resolution screen
  // both get a comfortably sized toast, and it never shrinks below 1.0.
  return qMax(DpiScaleForMonitor(mon), ResolutionScaleForMonitor(mon));
}

QPoint HintToast::ComputePosition() const {
  // Screenshot/automation mode: use a caller-supplied anchor and skip the
  // caret/cursor probes (which can return a stale rect from a previous
  // focused window).
  if (anchor_override_) {
    return *anchor_override_;
  }
  RECT caret{};
  QPoint pos;
  if (GetCaretScreenRect(&caret)) {
    pos = QPoint(caret.left, caret.bottom + kCaretGap);
    MONITORINFO mi{sizeof(mi)};
    HMONITOR mon = MonitorFromPoint({caret.left, caret.bottom},
                                    MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(mon, &mi)) {
      // No room below the caret: flip above it.
      if (pos.y() + image_.height() > mi.rcWork.bottom) {
        pos.setY(caret.top - image_.height() - kCaretGap);
      }
      if (pos.x() < mi.rcWork.left) pos.setX(mi.rcWork.left);
      if (pos.x() + image_.width() > mi.rcWork.right) {
        pos.setX(mi.rcWork.right - image_.width());
      }
      if (pos.y() < mi.rcWork.top) pos.setY(mi.rcWork.top);
    }
  } else {
    POINT pt{};
    GetCursorPos(&pt);
    pos = QPoint(pt.x + kCursorOffsetX, pt.y + kCursorOffsetY);
    MONITORINFO mi{sizeof(mi)};
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(mon, &mi)) {
      if (pos.x() + image_.width() > mi.rcWork.right) {
        pos.setX(mi.rcWork.right - image_.width());
      }
      if (pos.y() + image_.height() > mi.rcWork.bottom) {
        pos.setY(mi.rcWork.bottom - image_.height());
      }
    }
  }
  return pos;
}

void HintToast::PushFrame() {
  if (!hwnd_ || image_.isNull()) return;
  const int w = image_.width();
  const int h = image_.height();

  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;  // top-down DIB
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!hbm) return;
  memcpy(bits, image_.constBits(), static_cast<size_t>(w) * h * 4);

  HDC screen_dc = GetDC(nullptr);
  HDC mem_dc = CreateCompatibleDC(screen_dc);
  HGDIOBJ old = SelectObject(mem_dc, hbm);
  POINT src{0, 0};
  SIZE size{w, h};
  POINT pos{pos_.x(), pos_.y()};
  BLENDFUNCTION bf{};
  bf.BlendOp = AC_SRC_OVER;
  bf.SourceConstantAlpha = 255;
  bf.AlphaFormat = AC_SRC_ALPHA;
  UpdateLayeredWindow(hwnd_, screen_dc, &pos, &size, mem_dc, &src, 0, &bf,
                      ULW_ALPHA);
  SelectObject(mem_dc, old);
  DeleteDC(mem_dc);
  ReleaseDC(nullptr, screen_dc);
  DeleteObject(hbm);
  DWORD err = GetLastError();
  if (err != 0) {
    LOG_ERROR("HintToast: UpdateLayeredWindow GetLastError=%lu", err);
  }
}

}  // namespace capswriter
