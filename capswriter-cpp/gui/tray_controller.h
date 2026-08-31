#pragma once

#include <QObject>
#include <QSystemTrayIcon>

class QMenu;

namespace capswriter {

class MainWindow;

// 系统托盘:图标 + tooltip + 双击显示主窗口 + 右键菜单
// (显示主界面 / 设置 / 打开日志 / 退出)。
class TrayController : public QObject {
  Q_OBJECT

 public:
  explicit TrayController(MainWindow* window, QObject* parent = nullptr);

  // 创建托盘图标与菜单;托盘不可用时返回 false。
  bool Initialize();

 private:
  void ShowWindow();

  MainWindow* window_ = nullptr;
  QSystemTrayIcon* tray_ = nullptr;
  QMenu* menu_ = nullptr;
};

}  // namespace capswriter
