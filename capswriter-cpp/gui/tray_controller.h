#pragma once

#include <QObject>
#include <QSystemTrayIcon>

#include <string>

#include "app_icons.h"

class QMenu;

namespace capswriter {

class MainWindow;
struct Config;

// 系统托盘:图标 + tooltip + 双击显示主窗口 + 右键菜单
// (显示主界面 / 设置 / 打开日志 / 退出)。
class TrayController : public QObject {
  Q_OBJECT

 public:
  TrayController(MainWindow* window, Config* config,
                 const std::string& config_path,
                 QObject* parent = nullptr);

  // 创建托盘图标与菜单;托盘不可用时返回 false。
  bool Initialize();

  // 切换托盘图标状态(录音中 / 忙碌 / 错误 / 待命)。
  void SetState(TrayIconState state);

 private:
  void ShowWindow();
  void OpenSettings();

  MainWindow* window_ = nullptr;
  Config* config_ = nullptr;
  std::string config_path_;
  QSystemTrayIcon* tray_ = nullptr;
  QMenu* menu_ = nullptr;
  TrayIconState current_state_ = TrayIconState::kIdle;
};

}  // namespace capswriter
