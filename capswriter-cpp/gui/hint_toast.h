#pragma once

#include <windows.h>

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QTimer>

#include <optional>

namespace capswriter {

// Always-on-top hint bar shown near the focused text caret (fallback: the
// mouse cursor) while dictation input is in progress or right after the
// text was delivered.
//
// Implemented as a native Win32 layered window instead of a Qt::Tool /
// Qt::ToolTip widget, because Qt hides tool/tooltip windows when the
// application is inactive — which is exactly when we need the hint to be
// visible (the user is typing into another app). The content is rendered
// by Qt into a transparent image (so emoji render in color, no image files
// involved) and pushed with UpdateLayeredWindow. The window never takes
// focus, is transparent to mouse input and stays out of the taskbar.
class HintToast : public QObject {
  Q_OBJECT

 public:
  explicit HintToast(QObject* parent = nullptr);
  ~HintToast() override;

  HintToast(const HintToast&) = delete;
  HintToast& operator=(const HintToast&) = delete;

  // Shows `text` for `duration_ms` near the caret/cursor. A large duration
  // keeps it visible until the next Show()/Hide() (e.g. while recording).
  void Show(const QString& text, int duration_ms);

  // Overrides the position reference (caret/cursor). When set, ComputePosition
  // returns anchor_override_ + caret gap directly, bypassing GetCaretScreenRect
  // and GetCursorPos. Used by the --screenshot harness so the toast sits in
  // a known location even if the focused thread reports a stale caret.
  void SetAnchorOverride(const QPoint& anchor) { anchor_override_ = anchor; }
  void ClearAnchorOverride() { anchor_override_.reset(); }

  // Hides the hint immediately.
  void Hide();

  bool visible() const { return visible_; }

 private:
  bool EnsureWindow();
  void Render(const QString& text);
  QPoint ComputePosition() const;
  void PushFrame();
  void Tick();

  HWND hwnd_ = nullptr;
  QTimer timer_;
  QImage image_;
  QPoint pos_;
  int remain_ms_ = 0;
  bool visible_ = false;
  std::optional<QPoint> anchor_override_;
};

}  // namespace capswriter
