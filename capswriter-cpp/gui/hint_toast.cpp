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
  // Scale the toast with the DPI of the screen under the caret/cursor so it
  // matches the rest of the UI on high-DPI displays.
  RECT caret{};
  POINT cursor{};
  QPoint ref;
  if (GetCaretScreenRect(&caret)) {
    ref = QPoint(caret.left, caret.bottom);
  } else {
    GetCursorPos(&cursor);
    ref = QPoint(cursor.x, cursor.y);
  }
  HDC screen_dc = GetDC(nullptr);
  const int dpi = screen_dc ? GetDeviceCaps(screen_dc, LOGPIXELSX) : 96;
  if (screen_dc) ReleaseDC(nullptr, screen_dc);
  const qreal scale = dpi / 96.0;

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
