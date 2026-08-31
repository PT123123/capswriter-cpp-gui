#include "tray_controller.h"

#include <QAction>
#include <QApplication>
#include <QMenu>

#include "app_icons.h"
#include "audio_recorder.h"
#include "asr_engine.h"
#include "config.h"
#include "log.h"
#include "main_window.h"
#include "settings_dialog.h"

namespace capswriter {

TrayController::TrayController(MainWindow* window, Config* config,
                               const std::string& config_path,
                               AsrEngine* engine,
                               std::function<void()> restart_fn,
                               QObject* parent)
    : QObject(parent),
      window_(window),
      config_(config),
      config_path_(config_path),
      engine_(engine),
      restart_fn_(std::move(restart_fn)) {}

bool TrayController::Initialize() {
  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    return false;
  }

  tray_ = new QSystemTrayIcon(this);
  tray_->setIcon(MakeTrayIcon(current_state_));
  tray_->setToolTip(TooltipFor(current_state_, engine_));

  // 双击托盘图标显示主窗口。
  connect(tray_, &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick) {
              ShowWindow();
            }
          });

  // 右键菜单：图标全部由 emoji 实时绘制,不引入任何外部图片资源。
  menu_ = new QMenu();

  auto* action_show = menu_->addAction(
      MakeEmojiIcon(0x1F5A5, 16, QColor(47, 125, 246)),
      QStringLiteral("显示主界面"));  // 🖥️
  auto* action_settings = menu_->addAction(
      MakeEmojiIcon(0x2699, 16, QColor(80, 80, 80)),
      QStringLiteral("设置"));  // ⚙️
  auto* action_log = menu_->addAction(
      MakeEmojiIcon(0x1F4CB, 16, QColor(47, 125, 246)),
      QStringLiteral("打开日志"));  // 📋

  // 麦克风选择子菜单:列出所有可用输入设备,当前选中的打勾;选择后写入
  // config.microphone 并保存到 config.yaml(下次录音生效,无需重启)。
  mic_submenu_ = new QMenu(QStringLiteral("选择麦克风"), menu_);  // 🎤
  mic_submenu_->setIcon(
      MakeEmojiIcon(0x1F3A4, 16, QColor(47, 125, 246)));
  menu_->addMenu(mic_submenu_);
  connect(mic_submenu_, &QMenu::aboutToShow, this,
          &TrayController::RebuildMicrophoneMenu);

  menu_->addSeparator();
  auto* action_quit = menu_->addAction(
      MakeEmojiIcon(0x1F6AA, 16, QColor(196, 43, 28)),
      QStringLiteral("退出"));  // 🚪

  connect(action_show, &QAction::triggered, this, [this] { ShowWindow(); });
  connect(action_settings, &QAction::triggered, this,
          [this] { OpenSettings(); });
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

void TrayController::SetState(TrayIconState state) {
  if (state == current_state_) return;
  current_state_ = state;
  if (!tray_) return;

  tray_->setIcon(MakeTrayIcon(current_state_));
  tray_->setToolTip(TooltipFor(current_state_, engine_));
}

QString TrayController::TooltipFor(TrayIconState state, AsrEngine* engine) {
  switch (state) {
    case TrayIconState::kRecording:
      return QStringLiteral("CapsWriter-Offline · 正在听写");
    case TrayIconState::kBusy:
      return QStringLiteral("CapsWriter-Offline · 正在加载语音模型，请稍候…");
    case TrayIconState::kError:
      return QStringLiteral("CapsWriter-Offline · 出错（请查看日志）");
    case TrayIconState::kIdle:
    default: {
      QString tip = QStringLiteral("CapsWriter-Offline · 待命");
      if (engine && engine->ready() && !engine->loaded_model_id().empty()) {
        tip += QStringLiteral("（") +
               QString::fromStdString(engine->loaded_model_id()) +
               QStringLiteral("）");
      }
      return tip;
    }
  }
}

void TrayController::ShowWindow() {
  window_->showNormal();
  window_->raise();
  window_->activateWindow();
}

void TrayController::OpenSettings() {
  SettingsDialog dialog(config_, config_path_, engine_, restart_fn_, nullptr);
  dialog.exec();
}

void TrayController::RebuildMicrophoneMenu() {
  if (!mic_submenu_) return;
  mic_submenu_->clear();

  const std::vector<AudioDeviceInfo> devices = ListCaptureDevices();

  auto add_entry = [this](const std::string& id, const QString& label) {
    auto* action = mic_submenu_->addAction(label);
    action->setCheckable(true);
    action->setChecked(id == config_->microphone);
    connect(action, &QAction::triggered, this, [this, id] {
      config_->microphone = id;
      std::string err;
      if (config_->SaveToFile(config_path_, &err)) {
        LOG_INFO("Microphone set to: %s",
                 id.empty() ? "<system default>" : id.c_str());
      } else {
        LOG_ERROR("Failed to save microphone setting: %s", err.c_str());
      }
    });
  };

  // 系统默认(空 id)。
  add_entry("", QStringLiteral("系统默认"));
  if (!devices.empty()) mic_submenu_->addSeparator();
  for (const auto& dev : devices) {
    add_entry(dev.id, QString::fromStdString(dev.name));
  }
}

}  // namespace capswriter
