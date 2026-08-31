#pragma once

#include <QPoint>
#include <QWidget>

class QLabel;
class QPushButton;
class QTabWidget;
class QTextEdit;

namespace capswriter {

// 无边框暗色主窗口,布局模仿原版 tray_gui.py:
//  - 自定义标题栏(标题 + 最小化/最大化/关闭按钮,关闭按钮悬停变红)
//  - QTabWidget 双标签页:日志 / 识别结果(只读 QTextEdit,自动滚动)
//  - 按住窗口任意位置拖动
//  - 点关闭只是隐藏到托盘,托盘菜单"退出"才真正退出
//
// 与废弃的 qt-tray-gui 骨架不同,本窗口接入真实程序:
// AppendLog 由 Logger sink 驱动,AppendResult 由 DictationController
// 的识别结果回调驱动(两者均经线程安全队列 + 主线程定时器刷新)。
class MainWindow : public QWidget {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  // 追加一行日志 / 一条识别结果(仅应在主线程调用)。
  void AppendLog(const QString& line);
  void AppendResult(const QString& text);

  // 托盘菜单"退出"调用:标记允许真正退出,再关闭窗口。
  void RequestExit();

 protected:
  void closeEvent(QCloseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  void ToggleMaximize();
  QTextEdit* MakeLogView();

  QLabel* title_label_ = nullptr;
  QPushButton* min_button_ = nullptr;
  QPushButton* max_button_ = nullptr;
  QPushButton* close_button_ = nullptr;
  QTabWidget* tab_widget_ = nullptr;
  QTextEdit* log_view_ = nullptr;
  QTextEdit* result_view_ = nullptr;

  QPoint drag_offset_;
  bool exit_requested_ = false;
};

}  // namespace capswriter
