#pragma once

#include <QObject>
#include <QSystemTrayIcon>

#include <functional>
#include <string>

#include "app_icons.h"

class QMenu;

namespace capswriter {

class MainWindow;
struct Config;
class AsrEngine;

// 系统托盘:图标 + tooltip + 双击显示主窗口 + 右键菜单
// (显示主界面 / 设置 / 打开日志 / 退出)。
class TrayController : public QObject {
  Q_OBJECT

 public:
  // engine / restart_fn are forwarded to the settings dialog so it can display
  // the currently-loaded model and offer to restart on a model switch.
  TrayController(MainWindow* window, Config* config,
                 const std::string& config_path, AsrEngine* engine = nullptr,
                 std::function<void()> restart_fn = {},
                 QObject* parent = nullptr);

  // 创建托盘图标与菜单;托盘不可用时返回 false。
  bool Initialize();

  // 切换托盘图标状态(录音中 / 忙碌 / 错误 / 待命)。
  void SetState(TrayIconState state);

 private:
  // 各状态对应的托盘 tooltip 文案(待命态会附带当前生效模型名)。
  static QString TooltipFor(TrayIconState state, AsrEngine* engine);

  void ShowWindow();
  void OpenSettings();

  // (Re)builds the microphone selection submenu from the live device list.
  // Called when the submenu is about to show so the device list and the
  // current selection stay fresh.
  void RebuildMicrophoneMenu();

  MainWindow* window_ = nullptr;
  Config* config_ = nullptr;
  std::string config_path_;
  AsrEngine* engine_ = nullptr;
  std::function<void()> restart_fn_;
  QSystemTrayIcon* tray_ = nullptr;
  QMenu* menu_ = nullptr;
  QMenu* mic_submenu_ = nullptr;  // microphone selection (owned by menu_)
  TrayIconState current_state_ = TrayIconState::kIdle;
};

}  // namespace capswriter
