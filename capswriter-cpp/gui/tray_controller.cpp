#include "tray_controller.h"

#include <QAction>
#include <QApplication>
#include <QMenu>

#include "app_icons.h"
#include "main_window.h"

namespace capswriter {

TrayController::TrayController(MainWindow* window, QObject* parent)
    : QObject(parent), window_(window) {}

bool TrayController::Initialize() {
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    return false;
  }

  tray_ = new QSystemTrayIcon(this);
  tray_->setIcon(MakeTrayIcon());
  tray_->setToolTip(QStringLiteral("CapsWriter-Offline"));

  // 双击托盘图标显示主窗口。
  connect(tray_, &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick) {
              ShowWindow();
            }
          });

  // 右键菜单。
  menu_ = new QMenu();

  auto* action_show = menu_->addAction(QStringLiteral("显示主界面"));
  auto* action_settings = menu_->addAction(QStringLiteral("设置"));
  auto* action_log = menu_->addAction(QStringLiteral("打开日志"));
  menu_->addSeparator();
  auto* action_quit = menu_->addAction(QStringLiteral("退出"));

  connect(action_show, &QAction::triggered, this, [this] { ShowWindow(); });
  connect(action_settings, &QAction::triggered, this, [this] {
    // 与原程序一致:设置界面暂未实现,仅在日志中提示。
    window_->AppendLog(QStringLiteral("[设置] 设置界面尚未实现,"
                                      "请编辑 config.yaml 后重启"));
  });
  connect(action_log, &QAction::triggered, this, [this] { ShowWindow(); });
  connect(action_quit, &QAction::triggered, this, [this] {
    window_->RequestExit();
    QApplication::quit();
  });

  menu_->setStyleSheet(QStringLiteral(
      "QMenu { background-color: #1e1e1e; color: #e0e0e0;"
      " border: 1px solid #333; }"
      "QMenu::item { padding: 6px 24px; }"
      "QMenu::item:selected { background-color: #333; }"));

  tray_->setContextMenu(menu_);
  tray_->show();
  return true;
}

void TrayController::ShowWindow() {
  window_->showNormal();
  window_->raise();
  window_->activateWindow();
}

}  // namespace capswriter
