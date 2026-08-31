#include "settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSize>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

#include <filesystem>

#include "app_icons.h"
#include "../src/asr_engine.h"
#include "../src/audio_recorder.h"
#include "../src/config.h"
#include "../src/log.h"
#include "../src/models.h"

namespace capswriter {

namespace {

// Dark theme stylesheet for the settings dialog, matching the main window.
const char kSettingsStyle[] = R"(
    QDialog, QWidget {
        background-color: #121212;
        color: #e0e0e0;
    }
    QTabWidget::pane {
        border: 1px solid #333;
        background-color: #1e1e1e;
    }
    QTabWidget::tab-bar { left: 4px; }
    QTabBar::tab {
        background-color: #1e1e1e;
        color: #b0b0b0;
        padding: 8px 16px;
        margin-right: 2px;
        border: 1px solid #333;
        border-bottom: none;
        border-top-left-radius: 4px;
        border-top-right-radius: 4px;
    }
    QTabBar::tab:selected {
        background-color: #2d2d2d;
        color: #e0e0e0;
    }
    QTabBar::tab:!selected { margin-top: 2px; }
    QGroupBox {
        border: 1px solid #333;
        border-radius: 4px;
        margin-top: 12px;
        padding-top: 16px;
        color: #e0e0e0;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        left: 10px;
        padding: 0 4px;
        color: #7ab8e6;
    }
    QLabel { color: #e0e0e0; }
    QLabel#hintLabel { color: #888; font-size: 11px; }
    QLabel#restartLabel { color: #e0a040; font-size: 11px; }
    QLabel#restartLabel a {
        color: #e0a040;
        text-decoration: none;
    }
    QLabel#restartLabel a:hover {
        color: #ffc46b;
    }
    QCheckBox { color: #e0e0e0; spacing: 8px; }
    QCheckBox::indicator {
        width: 16px; height: 16px;
        border: 1px solid #555;
        border-radius: 3px;
        background-color: #1e1e1e;
    }
    QCheckBox::indicator:checked {
        background-color: #2d6e9e;
        border-color: #4a90c4;
    }
    QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit {
        background-color: #2a2a2a;
        color: #e0e0e0;
        border: 1px solid #444;
        border-radius: 3px;
        padding: 4px 8px;
        min-height: 20px;
    }
    QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover,
    QLineEdit:hover { border-color: #555; }
    QComboBox::drop-down { border: none; width: 20px; }
    QComboBox QAbstractItemView {
        background-color: #2a2a2a;
        color: #e0e0e0;
        selection-background-color: #2d6e9e;
        border: 1px solid #444;
    }
    QSpinBox::up-button, QDoubleSpinBox::up-button,
    QSpinBox::down-button, QDoubleSpinBox::down-button {
        background-color: #333;
        border: none;
        width: 16px;
    }
    QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
    QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
        background-color: #444;
    }
    QPushButton {
        background-color: #2d2d2d;
        color: #e0e0e0;
        border: 1px solid #444;
        border-radius: 4px;
        padding: 6px 20px;
        min-width: 72px;
    }
    QPushButton:hover { background-color: #383838; border-color: #555; }
    QPushButton:pressed { background-color: #252525; }
    QPushButton#okButton {
        background-color: #2d6e9e;
        border-color: #4a90c4;
        color: #fff;
    }
    QPushButton#okButton:hover {
        background-color: #3a80b8;
        border-color: #5ba0d4;
    }
)";

// Widget row: label on the left, control on the right.
// hint is an optional small grey description below the label. The "需重启" tag
// is created by the member SettingsDialog::AddRow (see below), which keeps it
// hidden until the control is actually edited.

}  // namespace

SettingsDialog::SettingsDialog(Config* config,
                               const std::string& config_path,
                               AsrEngine* engine,
                               std::function<void()> restart_fn,
                               QWidget* parent)
    : QDialog(parent), config_(config), config_path_(config_path),
      engine_(engine), restart_fn_(std::move(restart_fn)) {
  // 标题栏带上 ⚙️(U+2699 U+FE0F),窗口图标用同一套 emoji。
  setWindowTitle(QStringLiteral(
      "\xe2\x9a\x99\xef\xb8\x8f CapsWriter \xe8\xae\xbe\xe7\xbd\xae"));
  // "⚙️ CapsWriter 设置"
  setWindowIcon(MakeEmojiIcon(0x2699, 32, QColor(45, 110, 158)));
  setMinimumSize(520, 560);
  BuildUi();
  reloading_ = true;
  LoadFromConfig();
  reloading_ = false;
  // Snapshot the config so OnOk can tell whether a restart-needed setting
  // actually changed (otherwise we stay silent on a plain "OK").
  original_ = *config_;
  // Baseline for the per-row "需重启" tags: hidden until a real edit.
  CaptureRestartOriginals();
}

void SettingsDialog::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  // Reload widget values from the live config each time we're shown, so the
  // dialog always reflects the current state (e.g. after a previous cancel),
  // and re-baseline the per-row "需重启" tags (hidden until a real edit).
  reloading_ = true;
  LoadFromConfig();
  reloading_ = false;
  original_ = *config_;
  CaptureRestartOriginals();
  // The engine finishes loading a few seconds after startup. If the dialog is
  // opened during that window the "current model" box was frozen on
  // "not ready"; poll until it is, then stop.
  if (engine_ && !engine_->ready()) {
    if (!refresh_timer_) {
      refresh_timer_ = new QTimer(this);
      connect(refresh_timer_, &QTimer::timeout, this, [this] {
        RefreshCurrentModelDisplay();
        if (!engine_ || engine_->ready()) refresh_timer_->stop();
      });
    }
    refresh_timer_->start(250);
  }
  // Always reflect the latest state on show.
  RefreshCurrentModelDisplay();
}

void SettingsDialog::BuildUi() {
  setStyleSheet(QString::fromUtf8(kSettingsStyle));

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(8);

  auto* tabs = new QTabWidget;
  tabs->setIconSize(QSize(16, 16));

  // ====== Tab 1: Output ======
  auto* output_tab = new QWidget;
  auto* output_form = new QFormLayout(output_tab);
  output_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  output_form->setSpacing(12);
  output_form->setContentsMargins(16, 16, 16, 16);

  paste_check_ = new QCheckBox(
      QStringLiteral("\xe7\xb2\x98\xe8\xb4\xb4"
                     "\xe8\xbe\x93\xe5\x87\xba"
                     " (Ctrl+V)"));
  // "粘贴输出 (Ctrl+V)"
  AddRow(output_form,
         QStringLiteral("\xe8\xbe\x93\xe5\x87\xba\xe6\x96\xb9\xe5\xbc\x8f"),
         // "输出方式"
         paste_check_,
         QStringLiteral(
             "\xe9\x80\x89\xe4\xb8\xad\xe7\xb2\x98"
             "\xe8\xb4\xb4\xef\xbc\x9a\xe5\x86\x99\xe5\x89\xaa"
             "\xe8\xb4\xb4\xe6\x9d\xbf\xe5\x90\x8e"
             "\xe6\xa8\xa1\xe6\x8b\x9f Ctrl+V\xef\xbc\x9b"
             "\xe4\xb8\x8d\xe9\x80\x89\xe5\x88\x99"
             "\xe6\xa8\xa1\xe6\x8b\x9f\xe6\x89\x93"
             "\xe5\xad\x97\xe3\x80\x82"));
  // "选中粘贴：写剪贴板后模拟 Ctrl+V；不选则模拟打字。"
  connect(paste_check_, &QCheckBox::toggled, this,
          &SettingsDialog::OnPasteToggled);

  restore_clip_check_ = new QCheckBox(
      QStringLiteral("\xe7\xb2\x98\xe8\xb4\xb4\xe5\x90\x8e"
                     "\xe6\x81\xa2\xe5\xa4\x8d"
                     "\xe5\x89\xaa\xe8\xb4\xb4\xe6\x9d\xbf"));
  // "粘贴后恢复剪贴板"
  AddRow(output_form, QString(), restore_clip_check_);

  show_hint_check_ = new QCheckBox(
      QStringLiteral("\xe8\xbe\x93\xe5\x85\xa5\xe6\x97\xb6"
                     "\xe5\x9c\xa8\xe8\xbe\x93\xe5\x85\xa5\xe6\xa1\x86"
                     "\xe4\xb8\x8b\xe6\x96\xb9"
                     "\xe6\x98\xbe\xe7\xa4\xba"
                     "\xe6\x8f\x90\xe7\xa4\xba\xe6\x9d\xa1"));
  // "输入时在输入框下方显示提示条"
  AddRow(output_form, QString(), show_hint_check_,
         QStringLiteral(
             "\xe5\xbd\x95\xe9\x9f\xb3\xe6\x97\xb6"
             "\xe6\x98\xbe\xe7\xa4\xba"
             "\xe2\x80\x9c\xf0\x9f\x8e\xa4 "
             "\xe6\xad\xa3\xe5\x9c\xa8\xe5\x90\xac"
             "\xe5\x86\x99\xe2\x80\xa6\xe2\x80\x9d"
             "\xef\xbc\x8c"
             "\xe7\xbb\x93\xe6\x9e\x9c"
             "\xe6\x8f\x90\xe7\xa4\xba"
             "\xe6\x89\x93\xe5\xad\x97/"
             "\xe7\xb2\x98\xe8\xb4\xb4/"
             "\xe5\x89\xaa\xe8\xb4\xb4\xe6\x9d\xbf"
             "\xef\xbc\x9b"
             "\xe6\x97\xa0\xe8\xbe\x93\xe5\x85\xa5\xe6\xa1\x86"
             "\xe6\x97\xb6"
             "\xe8\x87\xaa\xe5\x8a\xa8"
             "\xe5\xa4\x8d\xe5\x88\xb6"
             "\xe5\x88\xb0"
             "\xe5\x89\xaa\xe8\xb4\xb4\xe6\x9d\xbf"
             "\xe5\xb9\xb6"
             "\xe6\x8f\x90\xe7\xa4\xba"
             "\xe3\x80\x82"));
  // "录音时显示"🎤 正在听写…"，结果提示打字/粘贴/剪贴板；无输入框时自动复制到剪贴板并提示。"

  threshold_spin_ = new QDoubleSpinBox;
  threshold_spin_->setRange(0.1, 2.0);
  threshold_spin_->setSingleStep(0.1);
  threshold_spin_->setDecimals(1);
  threshold_spin_->setSuffix(QStringLiteral(" s"));
  AddRow(output_form,
         QStringLiteral(
             "\xe7\x9f\xad\xe6\x8c\x89\xe9\x98\x88\xe5\x80\xbc"),
         // "短按阈值"
         threshold_spin_,
         QStringLiteral(
             "\xe7\x9f\xad\xe4\xba\x8e\xe6\xad\xa4"
             "\xe6\x97\xb6\xe9\x97\xb4\xe8\xa7\x86"
             "\xe4\xb8\xba\xe6\x99\xae\xe9\x80\x9a"
             "\xe6\x8c\x89\xe9\x94\xae\xef\xbc\x8c"
             "\xe5\x8f\x96\xe6\xb6\x88\xe8\xaf\x86"
             "\xe5\x88\xab\xe5\xb9\xb6"
             "\xe8\xa1\xa5\xe5\x8f\x91\xe3\x80\x82"));
  // "短于此时间视为普通按键，取消识别并补发。"

  microphone_combo_ = new QComboBox;
  {
    // 第一项固定为系统默认输入设备(空 id)。
    microphone_combo_->addItem(QStringLiteral("系统默认"), QVariant(QString()));
    const std::vector<AudioDeviceInfo> devices = ListCaptureDevices();
    for (const auto& dev : devices) {
      microphone_combo_->addItem(QString::fromStdString(dev.name),
                                 QVariant(QString::fromStdString(dev.id)));
    }
  }
  AddRow(output_form,
         QStringLiteral("\xe9\xba\xa6\xe5\x85\x8b\xe9\xa3\x8e"),
         // "麦克风"
         microphone_combo_,
         QStringLiteral(
             "\xe5\xbd\x95\xe9\x9f\xb3\xe4\xbd\xbf"
             "\xe7\x94\xa8\xe7\x9a\x84\xe8\xbe\x93"
             "\xe5\x85\xa5\xe8\xae\xbe\xe5\xa4\x87"
             "\xef\xbc\x9b\xe9\xbb\x98\xe8\xae\xa4"
             "\xe4\xb8\xba\xe7\xb3\xbb\xe7\xbb\x9f"
             "\xe5\xbd\x93\xe5\x89\x8d\xe9\xbb\x98"
             "\xe8\xae\xa4\xe8\xbe\x93\xe5\x85\xa5"
             "\xe3\x80\x82"));
  // "录音使用的输入设备；默认为系统当前默认输入。"

  tabs->addTab(output_tab,  // 📤 输出
               MakeEmojiIcon(0x1F4E4, 16, QColor(45, 110, 158)),
               QStringLiteral(
                   "\xe8\xbe\x93\xe5\x87\xba"));  // "输出"

  // ====== Tab 2: Text ======
  auto* text_tab = new QWidget;
  auto* text_form = new QFormLayout(text_tab);
  text_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  text_form->setSpacing(12);
  text_form->setContentsMargins(16, 16, 16, 16);

  punct_check_ = new QCheckBox(
      QStringLiteral(
          "\xe6\xa0\x87\xe7\x82\xb9"
          "\xe6\x81\xa2\xe5\xa4\x8d"));
  // "标点恢复"
  AddRow(text_form, QString(), punct_check_, QString(), true,
         [this]() { SaveAndRestart(); });

  traditional_check_ = new QCheckBox(
      QStringLiteral(
          "\xe7\xb9\x81\xe4\xbd\x93"
          "\xe8\xbd\xac\xe6\x8d\xa2"));
  // "繁体转换"
  AddRow(text_form, QString(), traditional_check_);

  traditional_locale_combo_ = new QComboBox;
  traditional_locale_combo_->addItem(
      QStringLiteral("zh-hant"));
  traditional_locale_combo_->addItem(
      QStringLiteral("zh-tw"));
  traditional_locale_combo_->addItem(
      QStringLiteral("zh-hk"));
  AddRow(text_form,
         QStringLiteral(
             "\xe7\xb9\x81\xe4\xbd\x93"
             "\xe5\x9c\xb0\xe5\x8c\xba"),
         // "繁体地区"
         traditional_locale_combo_);

  hot_rule_check_ = new QCheckBox(
      QStringLiteral(
          "\xe8\xa7\x84\xe5\x88\x99"
          "\xe6\x9b\xbf\xe6\x8d\xa2"));
  // "规则替换"
  AddRow(text_form, QString(), hot_rule_check_,
         QStringLiteral(
             "hot-rule.txt \xe6\xad\xa3"
             "\xe5\x88\x99\xe6\x9b\xbf"
             "\xe6\x8d\xa2\xef\xbc\x8c"
             "\xe6\x96\x87\xe4\xbb\xb6"
             "\xe6\x94\xb9\xe5\x8a\xa8"
             "\xe8\x87\xaa\xe5\x8a\xa8"
             "\xe7\x83\xad\xe9\x87\x8d"
             "\xe8\xbd\xbd"));
  // "hot-rule.txt 正则替换，文件改动自动热重载"

  trash_punc_thresh_spin_ = new QSpinBox;
  trash_punc_thresh_spin_->setRange(1, 100);
  AddRow(text_form,
         QStringLiteral(
             "\xe6\x9c\xab\xe5\xb0\xbe"
             "\xe6\xa0\x87\xe7\x82\xb9"
             "\xe6\xb6\x88\xe9\x99\xa4"
             "\xe9\x98\x88\xe5\x80\xbc"),
         // "末尾标点消除阈值"
         trash_punc_thresh_spin_,
         QStringLiteral(
             "\xe8\xaf\xad\xe4\xb9\x89"
             "\xe5\xad\x97\xe7\xac\xa6"
             "\xe6\x95\xb0\xe4\xb8\x8d"
             "\xe8\xb6\x85\xe8\xbf\x87"
             "\xe6\xad\xa4\xe5\x80\xbc"
             "\xe6\x97\xb6\xef\xbc\x8c"
             "\xe5\x8e\xbb\xe9\x99\xa4"
             "\xe6\x9c\xab\xe5\xb0\xbe"
             "\xe6\xa0\x87\xe7\x82\xb9"));
  // "语义字符数不超过此值时，去除末尾标点"

  tabs->addTab(text_tab,  // 📝 文本
               MakeEmojiIcon(0x1F4DD, 16, QColor(60, 90, 160)),
               QStringLiteral(
                   "\xe6\x96\x87\xe6\x9c\xac"));  // "文本"

  // ====== Tab 3: Network ======
  auto* net_tab = new QWidget;
  auto* net_form = new QFormLayout(net_tab);
  net_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  net_form->setSpacing(12);
  net_form->setContentsMargins(16, 16, 16, 16);

  udp_broadcast_check_ = new QCheckBox(
      QStringLiteral(
          "UDP \xe5\xb9\xbf\xe6\x92\xad"
          "\xe8\xaf\x86\xe5\x88\xab"
          "\xe7\xbb\x93\xe6\x9e\x9c"));
  // "UDP 广播识别结果"
  AddRow(net_form, QString(), udp_broadcast_check_, QString(), true,
         [this]() { SaveAndRestart(); });

  udp_control_check_ = new QCheckBox(
      QStringLiteral(
          "UDP \xe5\xa4\x96\xe9\x83\xa8"
          "\xe6\x8e\xa7\xe5\x88\xb6"
          "\xe5\xbd\x95\xe9\x9f\xb3"));
  // "UDP 外部控制录音"
  AddRow(net_form, QString(), udp_control_check_, QString(), true,
         [this]() { SaveAndRestart(); });

  udp_control_addr_edit_ = new QLineEdit;
  udp_control_addr_edit_->setPlaceholderText(
      QStringLiteral("127.0.0.1"));
  AddRow(net_form,
         QStringLiteral(
             "UDP \xe6\x8e\xa7\xe5\x88\xb6"
             "\xe5\x9c\xb0\xe5\x9d\x80"),
         // "UDP 控制地址"
         udp_control_addr_edit_, QString(), true,
         [this]() { SaveAndRestart(); });

  udp_control_port_spin_ = new QSpinBox;
  udp_control_port_spin_->setRange(1, 65535);
  AddRow(net_form,
         QStringLiteral(
             "UDP \xe6\x8e\xa7\xe5\x88\xb6"
             "\xe7\xab\xaf\xe5\x8f\xa3"),
         // "UDP 控制端口"
         udp_control_port_spin_, QString(), true,
         [this]() { SaveAndRestart(); });

  tabs->addTab(net_tab,  // 🌐 网络
               MakeEmojiIcon(0x1F310, 16, QColor(40, 120, 120)),
               QStringLiteral(
                   "\xe7\xbd\x91\xe7\xbb\x9c"));  // "网络"

  // ====== Tab 4: Hotwords ======
  auto* hotwords_tab = new QWidget;
  auto* hotwords_layout = new QVBoxLayout(hotwords_tab);
  hotwords_layout->setContentsMargins(16, 16, 16, 16);
  hotwords_layout->setSpacing(8);

  auto* hotwords_hint = new QLabel(QStringLiteral(
      "\xe6\xaf\x8f\xe8\xa1\x8c\xe4\xb8\x80\xe4\xb8\xaa"
      "\xe7\x83\xad\xe8\xaf\x8d\xef\xbc\x8c"
      "\xe8\xaf\x86\xe5\x88\xab\xe6\x97\xb6"
      "\xe5\x8a\xa0\xe6\x9d\x83\xe6\x8f\x90\xe5\x8d\x87"
      "\xe5\x91\xbd\xe4\xb8\xad\xe7\x8e\x87"
      "\xe3\x80\x82"
      "\xe4\xbf\x9d\xe5\xad\x98\xe5\x90\x8e"
      "\xe9\x9c\x80\xe9\x87\x8d\xe5\x90\xaf"
      "\xe7\xa8\x8b\xe5\xba\x8f\xe7\x94\x9f"
      "\xe6\x95\x88\xe3\x80\x82"));
  // "每行一个热词，识别时加权提升命中率。保存后需重启程序生效。"
  hotwords_hint->setObjectName(QStringLiteral("hintLabel"));
  hotwords_hint->setWordWrap(true);
  hotwords_layout->addWidget(hotwords_hint);

  hotwords_edit_ = new QPlainTextEdit;
  hotwords_edit_->setPlaceholderText(QStringLiteral(
      "\xe8\xbe\x93\xe5\x85\xa5"
      "\xe7\x83\xad\xe8\xaf\x8d\xef\xbc\x8c"
      "\xe6\xaf\x8f\xe8\xa1\x8c\xe4\xb8\x80"
      "\xe4\xb8\xaa"));
  // "输入热词，每行一个"
  hotwords_edit_->setMinimumHeight(240);
  hotwords_edit_->setStyleSheet(
      QStringLiteral("QPlainTextEdit { "
      "background-color: #1a1a1a; color: #e0e0e0;"
      " border: 1px solid #444; border-radius: 3px;"
      " font-family: Consolas, 'Microsoft YaHei', monospace;"
      " font-size: 13px; }"));
  hotwords_layout->addWidget(hotwords_edit_);

  auto* hotwords_restart_label = new QLabel;
  hotwords_restart_label->setObjectName(QStringLiteral("restartLabel"));
  hotwords_restart_label->setTextFormat(Qt::RichText);
  hotwords_restart_label->setTextInteractionFlags(Qt::TextBrowserInteraction);
  hotwords_restart_label->setOpenExternalLinks(false);
  hotwords_restart_label->setText(QStringLiteral(
      "<a href=\"restart\" style=\"color:#e0a040;text-decoration:none;\">"
      "\xe9\x9c\x80\xe9\x87\x8d\xe5\x90\xaf\xe7\xa8\x8b"
      "\xe5\xba\x8f\xe7\x94\x9f\xe6\x95\x88</a>"));
  // "需重启程序生效"
  QObject::connect(hotwords_restart_label, &QLabel::linkActivated,
                   hotwords_restart_label,
                   [this](const QString&) { SaveAndRestart(); });
  // Hide the "需重启程序生效" hint until the user actually edits the hotwords;
  // RegisterRestartControl wires the editor's change signal to reveal it.
  hotwords_restart_label->setVisible(false);
  RegisterRestartControl(hotwords_edit_, hotwords_restart_label);
  hotwords_layout->addWidget(hotwords_restart_label);

  tabs->addTab(hotwords_tab,  // 🔥 热词
               MakeEmojiIcon(0x1F525, 16, QColor(170, 80, 40)),
               QStringLiteral(
                   "\xe7\x83\xad\xe8\xaf\x8d"));  // "热词"

  // ====== Tab 5: Engine ======
  auto* engine_tab = new QWidget;
  auto* engine_form = new QFormLayout(engine_tab);
  engine_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  engine_form->setSpacing(12);
  engine_form->setContentsMargins(16, 16, 16, 16);

  // --- Recognition model selection ---
  model_combo_ = new QComboBox;
  {
    const auto& models = GetModelRegistry();
    for (const auto& m : models) {
      model_combo_->addItem(QString::fromStdString(m.display),
                            QVariant(QString::fromStdString(m.id)));
    }
  }
  connect(model_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &SettingsDialog::OnModelComboChanged);
  AddRow(engine_form, QStringLiteral("识别模型"), model_combo_,
         QStringLiteral(
             "切换模型后需重启程序生效；未下载的模型可点击下方“下载”按钮获取。"),
         true, [this]() { SaveAndRestart(); });

  model_desc_label_ = new QLabel;
  model_desc_label_->setObjectName(QStringLiteral("hintLabel"));
  model_desc_label_->setWordWrap(true);
  engine_form->addRow(model_desc_label_);

  // --- Current active model (what the engine actually loaded) ---
  // Surfaces the real, fallback-resolved model + path so a mismatch between the
  // selection (config) and what is running is immediately visible.
  auto* cur_group = new QGroupBox(QStringLiteral("当前生效模型"));
  {
    auto* cur_layout = new QVBoxLayout(cur_group);
    cur_layout->setContentsMargins(12, 16, 12, 12);
    cur_layout->setSpacing(6);

    current_model_label_ = new QLabel;
    current_model_label_->setObjectName(QStringLiteral("hintLabel"));
    current_model_label_->setWordWrap(true);

    current_model_path_label_ = new QLabel;
    current_model_path_label_->setObjectName(QStringLiteral("hintLabel"));
    current_model_path_label_->setWordWrap(true);
    current_model_path_label_->setTextInteractionFlags(
        Qt::TextSelectableByMouse);

    model_mismatch_label_ = new QLabel;
    model_mismatch_label_->setObjectName(QStringLiteral("restartLabel"));
    model_mismatch_label_->setWordWrap(true);
    model_mismatch_label_->setTextFormat(Qt::RichText);
    model_mismatch_label_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    model_mismatch_label_->setOpenExternalLinks(false);
    model_mismatch_label_->setVisible(false);
    QObject::connect(model_mismatch_label_, &QLabel::linkActivated,
                     model_mismatch_label_,
                     [this](const QString&) { SaveAndRestart(); });

    cur_layout->addWidget(current_model_label_);
    cur_layout->addWidget(current_model_path_label_);
    cur_layout->addWidget(model_mismatch_label_);
  }
  engine_form->addRow(cur_group);

  // --- Model download / management ---
  model_mgmt_group_ = new QGroupBox(QStringLiteral("模型下载 / 管理"));
  {
    auto* mgmt_layout = new QVBoxLayout(model_mgmt_group_);
    mgmt_layout->setContentsMargins(12, 16, 12, 12);
    mgmt_layout->setSpacing(8);

    mirror_check_ =
        new QCheckBox(QStringLiteral("使用 GitHub 镜像加速 (ghfast.top)"));
    mirror_check_->setChecked(true);
    mgmt_layout->addWidget(mirror_check_);

    const auto& models = GetModelRegistry();
    for (size_t i = 0; i < models.size(); ++i) {
      const ModelInfo& m = models[i];
      if (!m.downloadable) continue;  // Paraformer is configured manually.

      auto* row = new QWidget;
      auto* rl = new QHBoxLayout(row);
      rl->setContentsMargins(0, 0, 0, 0);
      rl->setSpacing(8);

      auto* name = new QLabel(QString::fromStdString(m.display));
      auto* status = new QLabel;
      status->setObjectName(QStringLiteral("hintLabel"));
      auto* btn = new QPushButton(QStringLiteral("下载"));
      btn->setProperty("model_index", QVariant(static_cast<int>(i)));
      connect(btn, &QPushButton::clicked, this,
              &SettingsDialog::OnDownloadClicked);

      rl->addWidget(name, 1);
      rl->addWidget(status);
      rl->addWidget(btn);
      mgmt_layout->addWidget(row);
      model_rows_.push_back(ModelRow{&m, status, btn});
    }

    dl_progress_ = new QProgressBar;
    dl_progress_->setRange(0, 100);
    dl_progress_->setVisible(false);
    mgmt_layout->addWidget(dl_progress_);

    dl_status_label_ = new QLabel;
    dl_status_label_->setObjectName(QStringLiteral("hintLabel"));
    dl_status_label_->setWordWrap(true);
    mgmt_layout->addWidget(dl_status_label_);
  }
  engine_form->addRow(model_mgmt_group_);

  asr_threads_spin_ = new QSpinBox;
  asr_threads_spin_->setRange(1, 16);
  AddRow(engine_form,
         QStringLiteral(
             "ASR \xe7\xba\xbf\xe7\xa8\x8b\xe6\x95\xb0"),
         // "ASR 线程数"
         asr_threads_spin_, QString(), true,
         [this]() { SaveAndRestart(); });

  asr_provider_combo_ = new QComboBox;
  asr_provider_combo_->addItem(QStringLiteral("cpu"));
  asr_provider_combo_->addItem(QStringLiteral("cuda"));
  AddRow(engine_form,
         QStringLiteral("ASR \xe6\x8f\x90\xe4\xbe\x9b\xe5\x95\x86"),
         // "ASR 提供商"
         asr_provider_combo_, QString(), true,
         [this]() { SaveAndRestart(); });

  asr_decoding_combo_ = new QComboBox;
  asr_decoding_combo_->addItem(QStringLiteral("greedy_search"));
  asr_decoding_combo_->addItem(
      QStringLiteral("modified_beam_search"));
  AddRow(engine_form,
         QStringLiteral(
             "\xe8\xa7\xa3\xe7\xa0\x81"
             "\xe6\x96\xb9\xe6\xb3\x95"),
         // "解码方法"
         asr_decoding_combo_, QString(), true,
         [this]() { SaveAndRestart(); });

  asr_hotwords_score_spin_ = new QDoubleSpinBox;
  asr_hotwords_score_spin_->setRange(0.0, 5.0);
  asr_hotwords_score_spin_->setSingleStep(0.1);
  asr_hotwords_score_spin_->setDecimals(1);
  AddRow(engine_form,
         QStringLiteral(
             "\xe7\x83\xad\xe8\xaf\x8d"
             "\xe8\xaf\x84\xe5\x88\x86"),
         // "热词评分"
         asr_hotwords_score_spin_,
         QStringLiteral(
             "sherpa-onnx \xe4\xb8\x8a"
             "\xe4\xb8\x8b\xe6\x96\x87"
             "\xe7\x83\xad\xe8\xaf\x8d"
             "\xe7\x9a\x84\xe5\x8a\xa0"
             "\xe6\x9d\x83\xe5\x88\x86"
             "\xe6\x95\xb0\xef\xbc\x8c"
             "\xe8\xb6\x8a\xe5\xa4\xa7"
             "\xe8\xb6\x8a\xe4\xbc\x98"
             "\xe5\x85\x88"),
  // "sherpa-onnx 上下文热词的加权分数，越大越优先"
         true, [this]() { SaveAndRestart(); });

  log_level_combo_ = new QComboBox;
  log_level_combo_->addItem(QStringLiteral("DEBUG"));
  log_level_combo_->addItem(QStringLiteral("INFO"));
  log_level_combo_->addItem(QStringLiteral("WARNING"));
  log_level_combo_->addItem(QStringLiteral("ERROR"));
  AddRow(engine_form,
         QStringLiteral(
             "\xe6\x97\xa5\xe5\xbf\x97"
             "\xe7\xba\xa7\xe5\x88\xab"),
         // "日志级别"
         log_level_combo_);

  tabs->addTab(engine_tab,  // ⚙️ 引擎
               MakeEmojiIcon(0x2699, 16, QColor(80, 80, 90)),
               QStringLiteral(
                   "\xe5\xbc\x95\xe6\x93\x8e"));  // "引擎"

  root->addWidget(tabs);

  // --- Config file path display ---
  path_label_ = new QLabel;
  path_label_->setObjectName(QStringLiteral("hintLabel"));
  path_label_->setWordWrap(true);
  path_label_->setTextInteractionFlags(
      Qt::TextSelectableByMouse);
  root->addWidget(path_label_);

  // --- Bottom buttons ---
  auto* btn_bar = new QHBoxLayout;
  btn_bar->setContentsMargins(0, 0, 0, 0);
  btn_bar->setSpacing(8);

  auto* folder_btn = new QPushButton(
      QStringLiteral(
          "\xe6\x89\x93\xe5\xbc\x80"
          "\xe9\x85\x8d\xe7\xbd\xae"
          "\xe6\x96\x87\xe4\xbb\xb6"
          "\xe5\xa4\xb9"));
  // "打开配置文件夹"
  // 底部按钮也用 emoji 图标:📂 / ❌ / ✅
  folder_btn->setIcon(MakeEmojiIcon(0x1F4C2, 16, QColor(70, 70, 75)));
  folder_btn->setIconSize(QSize(16, 16));
  connect(folder_btn, &QPushButton::clicked, this,
          &SettingsDialog::OnOpenConfigFolder);
  btn_bar->addWidget(folder_btn);
  btn_bar->addStretch();

  auto* cancel_btn = new QPushButton(
      QStringLiteral("\xe5\x8f\x96\xe6\xb6\x88"));  // "取消"
  cancel_btn->setIcon(MakeEmojiIcon(0x274C, 16, QColor(70, 70, 75)));
  cancel_btn->setIconSize(QSize(16, 16));
  connect(cancel_btn, &QPushButton::clicked, this, &QDialog::reject);
  btn_bar->addWidget(cancel_btn);

  auto* ok_btn = new QPushButton(
      QStringLiteral("\xe7\xa1\xae\xe5\xae\x9a"));  // "确定"
  ok_btn->setIcon(MakeEmojiIcon(0x2705, 16, QColor(45, 110, 158)));
  ok_btn->setIconSize(QSize(16, 16));
  ok_btn->setObjectName(QStringLiteral("okButton"));
  ok_btn->setDefault(true);
  connect(ok_btn, &QPushButton::clicked, this, &SettingsDialog::OnOk);
  btn_bar->addWidget(ok_btn);

  root->addLayout(btn_bar);
}

void SettingsDialog::AddRow(QFormLayout* form, const QString& label,
                            QWidget* control, const QString& hint,
                            bool needs_restart,
                            std::function<void()> on_restart) {
  auto* label_widget = new QLabel(label);
  auto* row_layout = new QHBoxLayout;
  row_layout->setContentsMargins(0, 0, 0, 0);
  row_layout->setSpacing(8);
  row_layout->addWidget(control);

  if (needs_restart) {
    auto* restart_tag = new QLabel;
    // "需重启" — a neutral tag, not a warning triangle, so the engine tab
    // doesn't read as if every row is in an error state. Rendered as a link
    // so it can be clicked to trigger an immediate restart. It stays hidden
    // until the user actually edits this control.
    restart_tag->setObjectName(QStringLiteral("restartLabel"));
    restart_tag->setTextFormat(Qt::RichText);
    restart_tag->setTextInteractionFlags(Qt::TextBrowserInteraction);
    restart_tag->setOpenExternalLinks(false);
    restart_tag->setText(QStringLiteral(
        "<a href=\"restart\" style=\"color:#e0a040;text-decoration:none;\">"
        "\xe9\x9c\x80\xe9\x87\x8d\xe5\x90\xaf</a>"));
    // "需重启"
    restart_tag->setToolTip(QStringLiteral(
        "\xe6\xad\xa4\xe8\xae\xbe\xe7\xbd\xae\xe9\x9c\x80\xe9\x87\x8d"
        "\xe5\x90\xaf\xe7\xa8\x8b\xe5\xba\x8f\xe5\x90\x8e\xe7\x94\x9f"
        "\xe6\x95\x88\xef\xbc\x9b\xe7\x82\xb9\xe5\x87\xbb"
        "\xe7\xab\x8b\xe5\x8d\xb3\xe9\x87\x8d\xe5\x90\xaf"));
    // "此设置需重启程序后生效；点击立即重启"
    if (on_restart) {
      QObject::connect(restart_tag, &QLabel::linkActivated, restart_tag,
                       [on_restart](const QString&) { on_restart(); });
    }
    row_layout->addWidget(restart_tag);
    // Register so the tag only appears once the user edits this control.
    RegisterRestartControl(control, restart_tag);
  }
  row_layout->addStretch();

  if (hint.isEmpty() && !needs_restart) {
    form->addRow(label_widget, row_layout);
  } else {
    auto* vbox_widget = new QWidget;
    auto* vbox = new QVBoxLayout(vbox_widget);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(2);
    vbox->addWidget(label_widget);
    if (!hint.isEmpty()) {
      auto* hint_label = new QLabel(hint);
      hint_label->setObjectName(QStringLiteral("hintLabel"));
      hint_label->setWordWrap(true);
      vbox->addWidget(hint_label);
    }
    form->addRow(vbox_widget, row_layout);
  }
}

void SettingsDialog::RegisterRestartControl(QWidget* control, QLabel* tag) {
  restart_rows_.push_back({control, tag, QString()});
  tag->setVisible(false);
  if (QCheckBox* c = qobject_cast<QCheckBox*>(control)) {
    connect(c, &QCheckBox::toggled, this,
            [this, control]() { UpdateRestartTag(control); });
  } else if (QSpinBox* s = qobject_cast<QSpinBox*>(control)) {
    connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, control]() { UpdateRestartTag(control); });
  } else if (QDoubleSpinBox* s = qobject_cast<QDoubleSpinBox*>(control)) {
    connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, control]() { UpdateRestartTag(control); });
  } else if (QComboBox* c = qobject_cast<QComboBox*>(control)) {
    connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, control]() { UpdateRestartTag(control); });
  } else if (QLineEdit* l = qobject_cast<QLineEdit*>(control)) {
    connect(l, &QLineEdit::textChanged, this,
            [this, control]() { UpdateRestartTag(control); });
  } else if (QPlainTextEdit* p = qobject_cast<QPlainTextEdit*>(control)) {
    connect(p, &QPlainTextEdit::textChanged, this,
            [this, control]() { UpdateRestartTag(control); });
  }
}

void SettingsDialog::UpdateRestartTag(QWidget* control) {
  if (reloading_) return;  // don't flicker while reloading from config
  for (auto& r : restart_rows_) {
    if (r.control == control) {
      r.tag->setVisible(CurrentValue(control) != r.original_value);
      return;
    }
  }
}

QString SettingsDialog::CurrentValue(QWidget* control) const {
  if (QCheckBox* c = qobject_cast<QCheckBox*>(control))
    return c->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
  if (QSpinBox* s = qobject_cast<QSpinBox*>(control))
    return QString::number(s->value());
  if (QDoubleSpinBox* s = qobject_cast<QDoubleSpinBox*>(control))
    return QString::number(s->value());
  if (QComboBox* c = qobject_cast<QComboBox*>(control))
    return c->currentData().toString() + QStringLiteral("|") + c->currentText();
  if (QLineEdit* l = qobject_cast<QLineEdit*>(control))
    return l->text();
  if (QPlainTextEdit* p = qobject_cast<QPlainTextEdit*>(control))
    return p->toPlainText();
  return QString();
}

void SettingsDialog::CaptureRestartOriginals() {
  for (auto& r : restart_rows_) {
    r.original_value = CurrentValue(r.control);
  }
}

void SettingsDialog::LoadFromConfig() {
  if (!config_) return;

  // Output
  paste_check_->setChecked(config_->paste);
  restore_clip_check_->setChecked(config_->restore_clip);
  restore_clip_check_->setEnabled(config_->paste);
  show_hint_check_->setChecked(config_->show_hint);
  threshold_spin_->setValue(config_->threshold);

  // Microphone: match the combo to the saved endpoint id (empty = default).
  {
    const QString cur = QString::fromStdString(config_->microphone);
    const int idx = microphone_combo_->findData(QVariant(cur));
    microphone_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
  }

  // Text
  punct_check_->setChecked(config_->punct.enabled);
  traditional_check_->setChecked(config_->traditional_convert);
  int idx = traditional_locale_combo_->findText(
      QString::fromStdString(config_->traditional_locale));
  traditional_locale_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
  hot_rule_check_->setChecked(config_->hot_rule);
  trash_punc_thresh_spin_->setValue(config_->trash_punc_thresh);

  // Network
  udp_broadcast_check_->setChecked(config_->udp_broadcast);
  udp_control_check_->setChecked(config_->udp_control);
  udp_control_addr_edit_->setText(
      QString::fromStdString(config_->udp_control_addr));
  udp_control_port_spin_->setValue(config_->udp_control_port);

  // Engine
  idx = model_combo_->findData(
      QVariant(QString::fromStdString(config_->asr.model)));
  model_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
  OnModelComboChanged();  // refresh description + per-model download status
  asr_threads_spin_->setValue(config_->asr.num_threads);
  idx = asr_provider_combo_->findText(
      QString::fromStdString(config_->asr.provider));
  asr_provider_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
  idx = asr_decoding_combo_->findText(
      QString::fromStdString(config_->asr.decoding_method));
  asr_decoding_combo_->setCurrentIndex(idx >= 0 ? idx : 0);
  asr_hotwords_score_spin_->setValue(
      static_cast<double>(config_->asr.hotwords_score));
  idx = log_level_combo_->findText(
      QString::fromStdString(config_->log_level));
  log_level_combo_->setCurrentIndex(idx >= 0 ? idx : 1);

  // Path label
  path_label_->setText(
      QStringLiteral(
          "\xe9\x85\x8d\xe7\xbd\xae"
          "\xe6\x96\x87\xe4\xbb\xb6"
          "\xef\xbc\x9a%1")
          // "配置文件："
          .arg(QString::fromStdString(config_path_)));

  // Hotwords: resolve the file path and load contents. They are only usable
  // with modified_beam_search; Paraformer only supports greedy_search, and
  // sherpa-onnx rejects a config that combines greedy_search with a
  // hotwords_file. So only wire the file up when the decoding method supports
  // it, otherwise leave it empty to avoid a startup failure.
  if (config_->asr.decoding_method == "modified_beam_search") {
    config_->asr.hotwords_file = "hotwords.txt";
  } else {
    config_->asr.hotwords_file.clear();
  }
  hotwords_file_path_ = config_->ResolveModelPath(config_->asr.hotwords_file);
  QFile hw_file(QString::fromStdString(hotwords_file_path_));
  if (hw_file.exists()) {
    if (hw_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&hw_file);
      hotwords_edit_->setPlainText(in.readAll());
      hw_file.close();
    }
  }
}

void SettingsDialog::ApplyToConfig() {
  // Output
  config_->paste = paste_check_->isChecked();
  config_->restore_clip = restore_clip_check_->isChecked();
  config_->show_hint = show_hint_check_->isChecked();
  config_->threshold = threshold_spin_->value();
  config_->microphone =
      microphone_combo_->currentData().toString().toStdString();

  // Text
  config_->punct.enabled = punct_check_->isChecked();
  config_->traditional_convert = traditional_check_->isChecked();
  config_->traditional_locale =
      traditional_locale_combo_->currentText().toStdString();
  config_->hot_rule = hot_rule_check_->isChecked();
  config_->trash_punc_thresh = trash_punc_thresh_spin_->value();

  // Network
  config_->udp_broadcast = udp_broadcast_check_->isChecked();
  config_->udp_control = udp_control_check_->isChecked();
  config_->udp_control_addr =
      udp_control_addr_edit_->text().toStdString();
  config_->udp_control_port = udp_control_port_spin_->value();

  // Engine
  config_->asr.model =
      model_combo_->currentData().toString().toStdString();
  config_->asr.num_threads = asr_threads_spin_->value();
  config_->asr.provider =
      asr_provider_combo_->currentText().toStdString();
  config_->asr.decoding_method =
      asr_decoding_combo_->currentText().toStdString();
  config_->asr.hotwords_score =
      static_cast<float>(asr_hotwords_score_spin_->value());
  config_->log_level =
      log_level_combo_->currentText().toStdString();
}

void SettingsDialog::ApplyLive() {
  // Log level is applied immediately via Logger.
  Logger::Instance().set_level(config_->log_level);
  LOG_INFO("Settings updated (live fields applied).");
}

void SettingsDialog::OnOk() {
  // Guard: never let the user save a model that hasn't been downloaded yet.
  // Ask whether to download it; either way, do NOT write that selection into
  // config.yaml nor close the dialog.
  {
    const QString selected_id = model_combo_->currentData().toString();
    const ModelInfo* info = FindModel(selected_id.toStdString());
    if (info && info->downloadable &&
        !IsModelDownloaded(*info, config_->model_root)) {
      const int row = DownloadRowIndexForId(info->id);
      const QMessageBox::StandardButton choice = QMessageBox::question(
          this, QStringLiteral("CapsWriter"),
          QStringLiteral(
              "\xe8\xaf\x86\xe5\x88\xab\xe6\xa8\xa1\xe5\x9e\x8b\xe3\x80"
              "\x8c\x25\x31\xe3\x80\x8d\xe5\xb0\x9a\xe6\x9c\xaa\xe4\xb8"
              "\x8b\xe8\xbd\xbd\xef\xbc\x8c\xe6\x98\xaf\xe5\x90\xa6\xe7"
              "\x8e\xb0\xe5\x9c\xa8\xe4\xb8\x8b\xe8\xbd\xbd\xef\xbc\x9f")
              .arg(QString::fromStdString(info->display)),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
      if (choice == QMessageBox::Yes && row >= 0) {
        StartDownload(row);  // status label shows "正在下载 …"
        return;              // do NOT save / close
      }
      QMessageBox::information(
          this, QStringLiteral("CapsWriter"),
          QStringLiteral(
              "\xe8\xaf\xb7\xe9\x80\x89\xe6\x8b\xa9\xe5\xb7\xb2"
              "\xe4\xb8\x8b\xe8\xbd\xbd\xe7\x9a\x84\xe6\xa8\xa1\xe5"
              "\x9e\x8b\xef\xbc\x8c\xe6\x88\x96\xe5\x85\x88\xe7\x82"
              "\xb9\xe5\x87\xbb\xe3\x80\x8c\xe4\xb8\x8b\xe8\xbd\xbd"
              "\xe3\x80\x8d\xe6\x8c\x89\xe9\x92\xae\xe8\x8e\xb7"
              "\xe5\x8f\x96\xe5\x90\x8e\xe5\x86\x8d\xe5\x90\xaf"
              "\xe7\x94\xa8\xe3\x80\x82"));
      return;  // do NOT save / close
    }
  }

  ApplyToConfig();
  ApplyLive();

  // Save hotwords to file (before config save so hotwords_file is set).
  if (!hotwords_file_path_.empty()) {
    QFile hw_file(QString::fromStdString(hotwords_file_path_));
    if (hw_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
      QTextStream out(&hw_file);
      out << hotwords_edit_->toPlainText();
      hw_file.close();
      LOG_INFO("Hotwords saved to: %s", hotwords_file_path_.c_str());
    } else {
      LOG_WARN("Failed to save hotwords to: %s",
               hotwords_file_path_.c_str());
    }
  }

  // Save to file.
  std::string err;
  if (!config_->SaveToFile(config_path_, &err)) {
    QMessageBox::warning(this,
                          QStringLiteral("CapsWriter"),
                          QStringLiteral(
                              "\xe4\xbf\x9d\xe5\xad\x98"
                              "\xe9\x85\x8d\xe7\xbd\xae"
                              "\xe5\xa4\xb1\xe8\xb4\xa5"
                              "\xef\xbc\x9a\n%1")
                          // "保存设置失败：\n"
                              .arg(QString::fromLocal8Bit(err.c_str())));
  } else {
    LOG_INFO("Config saved to: %s", config_path_.c_str());
  }

  // If the selected model differs from the one actually loaded by the engine,
  // the switch only takes effect after a restart. Offer to restart now.
  if (engine_ && restart_fn_ && model_combo_) {
    const QString selected_id = model_combo_->currentData().toString();
    if (!selected_id.isEmpty() &&
        selected_id != QString::fromStdString(engine_->loaded_model_id())) {
      const ModelInfo* sel_info = FindModel(selected_id.toStdString());
      const QString sel_display = sel_info
          ? QString::fromStdString(sel_info->display)
          : selected_id;
      const QMessageBox::StandardButton choice = QMessageBox::question(
          this, QStringLiteral("CapsWriter"),
          QStringLiteral(
              "\xe8\xaf\x86\xe5\x88\xab\xe6\xa8\xa1\xe5\x9e\x8b\xe5\xb7\xb2\xe5\x88"
              "\x87\xe6\x8d\xa2\xe4\xb8\xba\xe3\x80\x8c\x25\x31\xe3\x80\x8d\xef\xbc"
              "\x8c\xe9\x9c\x80\xe9\x87\x8d\xe5\x90\xaf\xe7\xa8\x8b\xe5\xba\x8f\xe6"
              "\x89\x8d\xe8\x83\xbd\xe7\x94\x9f\xe6\x95\x88\xe3\x80\x82\xe6\x98\xaf"
              "\xe5\x90\xa6\xe7\x8e\xb0\xe5\x9c\xa8\xe9\x87\x8d\xe5\x90\xaf\xef\xbc"
              "\x9f")  // "识别模型已切换为「%1」，需重启程序才能生效。是否现在重启？"
              .arg(sel_display),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
      if (choice == QMessageBox::Yes) {
        accept();         // close the dialog first
        restart_fn_();    // releases mutex, starts new process, quits app
        return;
      }
      // Declined: fall through to the generic "restart needed" notice below.
    }
  }

  // Warn about restart-needed settings ONLY when one of them actually changed.
  // Plain "OK" with no relevant edit stays silent (no useless nag prompt).
  const bool restart_needed_changed = [this]() {
    if (original_.punct.enabled != config_->punct.enabled) return true;
    if (original_.asr.model != config_->asr.model) return true;
    if (original_.asr.num_threads != config_->asr.num_threads) return true;
    if (original_.asr.provider != config_->asr.provider) return true;
    if (original_.asr.decoding_method != config_->asr.decoding_method) return true;
    if (original_.asr.hotwords_score != config_->asr.hotwords_score) return true;
    if (original_.udp_broadcast != config_->udp_broadcast) return true;
    if (original_.udp_control != config_->udp_control) return true;
    if (original_.udp_control_addr != config_->udp_control_addr) return true;
    if (original_.udp_control_port != config_->udp_control_port) return true;
    // Hotwords: compare the editor text against the on-disk file.
    if (!hotwords_file_path_.empty()) {
      QFile f(QString::fromStdString(hotwords_file_path_));
      QString on_disk;
      if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        on_disk = QTextStream(&f).readAll();
        f.close();
      }
      if (on_disk != hotwords_edit_->toPlainText()) return true;
    }
    return false;
  }();

  if (restart_needed_changed) {
    QMessageBox::information(
        this,
        QStringLiteral("CapsWriter"),
        QStringLiteral(
            "\xe8\xae\xbe\xe7\xbd\xae\xe5\xb7\xb2"
            "\xe4\xbf\x9d\xe5\xad\x98\xe3\x80\x82"
            "\n\n"
            "\xe6\xa0\x87\xe7\x82\xb9\xe6\x81\xa2"
            "\xe5\xa4\x8d\xe3\x80\x81"
            "\xe8\xaf\x86\xe5\x88\xab\xe6\xa8\xa1"
            "\xe5\x9e\x8b\xe3\x80\x81"
            "\xe7\x83\xad\xe8\xaf\x8d\xe3\x80\x81"
            "UDP \xe5\xb9\xbf\xe6\x92\xad"
            "\xe3\x80\x81UDP \xe6\x8e\xa7"
            "\xe5\x88\xb6\xe3\x80\x81"
            "ASR \xe5\xbc\x95\xe6\x93\x8e"
            "\xe7\xad\x89\xe8\xae\xbe\xe7\xbd\xae"
            "\xe9\x9c\x80\xe9\x87\x8d"
            "\xe5\x90\xaf\xe7\xa8\x8b"
            "\xe5\xba\x8f\xe5\x90\x8e"
            "\xe7\x94\x9f\xe6\x95\x88"
            "\xe3\x80\x82"));
    // "设置已保存。\n\n标点恢复、热词、UDP 广播、UDP 控制、ASR 引擎等设置需重启程序后生效。"
  }

  accept();
}

void SettingsDialog::SaveAndRestart() {
  // 与 OnOk 一致的未下载模型拦截:还没下载的模型不允许写入配置/重启。
  const QString selected_id = model_combo_->currentData().toString();
  const ModelInfo* info = FindModel(selected_id.toStdString());
  if (info && info->downloadable &&
      !IsModelDownloaded(*info, config_->model_root)) {
    QMessageBox::information(
        this, QStringLiteral("CapsWriter"),
        QStringLiteral(
            "\xe6\x89\x80\xe9\x80\x89\xe6\xa8\xa1\xe5\x9e\x8b"
            "\xe5\xb0\x9a\xe6\x9c\xaa\xe4\xb8\x8b\xe8\xbd\xbd"
            "\xef\xbc\x8c\xe8\xaf\xb7\xe5\x85\x88\xe7\x82\xb9"
            "\xe2\x80\x9c\xe4\xb8\x8b\xe8\xbd\xbd\xe2\x80\x9d"
            "\xe8\x8e\xb7\xe5\x8f\x96\xe5\x90\x8e\xe5\x86\x8d"
            "\xe9\x87\x8d\xe5\x90\xaf\xe3\x80\x82"));
    // "所选模型尚未下载，请先点击“下载”获取后再重启。"
    return;
  }

  // 先保存当前设置,再立即重启,使改动在重启后生效。
  ApplyToConfig();
  ApplyLive();

  // 保存热词到文件 (与 OnOk 一致:先写文件再写 config)。
  if (!hotwords_file_path_.empty()) {
    QFile hw_file(QString::fromStdString(hotwords_file_path_));
    if (hw_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
      QTextStream out(&hw_file);
      out << hotwords_edit_->toPlainText();
      hw_file.close();
      LOG_INFO("Hotwords saved to: %s", hotwords_file_path_.c_str());
    } else {
      LOG_WARN("Failed to save hotwords to: %s", hotwords_file_path_.c_str());
    }
  }

  std::string err;
  if (!config_->SaveToFile(config_path_, &err)) {
    QMessageBox::warning(this, QStringLiteral("CapsWriter"),
                         QStringLiteral(
                             "\xe4\xbf\x9d\xe5\xad\x98"
                             "\xe9\x85\x8d\xe7\xbd\xae"
                             "\xe5\xa4\xb1\xe8\xb4\xa5"
                             "\xef\xbc\x9a\n%1")
                             .arg(QString::fromLocal8Bit(err.c_str())));
    return;  // 保存失败不重启,避免丢失
  }

  if (restart_fn_) {
    accept();        // 关闭对话框,释放资源
    restart_fn_();   // 释放互斥体、启动新进程、退出
  }
}

void SettingsDialog::OnOpenConfigFolder() {
  const std::filesystem::path p(config_path_);
  const std::string dir = p.parent_path().string();
  QDesktopServices::openUrl(
      QUrl::fromLocalFile(QString::fromStdString(dir)));
}

void SettingsDialog::OnPasteToggled(bool checked) {
  restore_clip_check_->setEnabled(checked);
}

void SettingsDialog::OnModelComboChanged() {
  const QString id = model_combo_->currentData().toString();
  const ModelInfo* info = FindModel(id.toStdString());
  if (!info) return;

  const bool downloaded =
      info->downloadable && IsModelDownloaded(*info, config_->model_root);
  QString status = downloaded ? QStringLiteral("（已下载）") : QStringLiteral("");
  model_desc_label_->setText(
      QString::fromStdString(info->display) + QStringLiteral(" — ") +
      QString::fromStdString(info->desc) + status);

  // Refresh per-model download-status labels.
  for (auto& row : model_rows_) {
    if (!row.info) continue;
    const bool ok = IsModelDownloaded(*row.info, config_->model_root);
    row.status->setText(ok ? QStringLiteral("已下载 ✓")
                           : QStringLiteral("未下载"));
    if (row.download_btn) row.download_btn->setEnabled(current_dl_index_ < 0);
  }

  RefreshCurrentModelDisplay();
}

void SettingsDialog::RefreshCurrentModelDisplay() {
  if (!current_model_label_) return;

  if (!engine_ || !engine_->ready()) {
    current_model_label_->setText(QStringLiteral(
        "（语音引擎正在加载模型，请稍候几秒；若长时间停留在此，请打开日志查看"
        "是否是模型加载失败）"));
    current_model_path_label_->clear();
    model_mismatch_label_->setVisible(false);
    return;
  }

  const std::string loaded_id = engine_->loaded_model_id();
  const ModelInfo* info = FindModel(loaded_id);
  const QString id_display = info ? QString::fromStdString(info->display)
                                  : QString::fromStdString(loaded_id);
  current_model_label_->setText(
      QStringLiteral(
          "\xe5\xbd\x93\xe5\x89\x8d\xe7\x94\x9f\xe6\x95\x88\xef\xbc\x9a")  // "当前生效："
      + id_display);
  current_model_path_label_->setText(
      QStringLiteral("\xe8\xb7\xaf\xe5\xbe\x84\xef\xbc\x9a")  // "路径："
      + QString::fromStdString(engine_->loaded_model_path()));

  // Reconcile the selection (config) against what is actually loaded.
  const QString selected =
      model_combo_ ? model_combo_->currentData().toString() : QString();
  if (!model_combo_ || selected == QString::fromStdString(loaded_id)) {
    model_mismatch_label_->setVisible(false);
    return;
  }
  const ModelInfo* sel_info = FindModel(selected.toStdString());
  const QString sel_display =
      sel_info ? QString::fromStdString(sel_info->display) : selected;
  model_mismatch_label_->setText(
      QStringLiteral(
          "<a href=\"restart\" style=\"color:#e0a040;text-decoration:none;\">"
          "\xe6\x89\x80\xe9\x80\x89\xe3\x80\x8c\x25\x31\xe3\x80\x8d\xe5\xb0\x86"
          "\xe5\x9c\xa8\xe9\x87\x8d\xe5\x90\xaf\xe5\x90\x8e\xe7\x94\x9f\xe6\x95\x88"
          "\xef\xbc\x9b\xe5\xbd\x93\xe5\x89\x8d\xe5\xbc\x95\xe6\x93\x8e\xe5\xae\x9e"
          "\xe9\x99\x85\xe5\x8a\xa0\xe8\xbd\xbd\xe7\x9a\x84\xe6\x98\xaf\xe3\x80\x8c"
          "\x25\x32\xe3\x80\x8d\xe3\x80\x82</a>")  // "所选「%1」将在重启后生效；当前引擎实际加载的是「%2」。"
          .arg(sel_display)
          .arg(id_display));
  model_mismatch_label_->setVisible(true);
}

int SettingsDialog::DownloadRowIndexForId(const std::string& id) {
  for (size_t i = 0; i < model_rows_.size(); ++i) {
    if (model_rows_[i].info && model_rows_[i].info->id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void SettingsDialog::StartDownload(int index) {
  if (index < 0 || index >= static_cast<int>(model_rows_.size())) return;
  if (current_dl_index_ >= 0) return;  // a download is already in progress
  current_dl_index_ = index;

  const ModelInfo& m = *model_rows_[index].info;
  dl_base_dir_ = QString::fromStdString(
      ModelBaseDir(config_->model_root));
  const QDir base_dir(dl_base_dir_);
  base_dir.mkpath(".");

  const QString archive_name =
      QString::fromStdString(m.subdir) + QStringLiteral(".tar.bz2");
  dl_archive_path_ = dl_base_dir_ + "/" + archive_name;

  QString url = QString::fromStdString(m.url);
  if (mirror_check_->isChecked()) {
    url.replace(QStringLiteral("https://github.com/"),
                QStringLiteral("https://ghfast.top/https://github.com/"));
  }

  // Disable all download buttons during the operation.
  for (auto& row : model_rows_) {
    if (row.download_btn) row.download_btn->setEnabled(false);
  }
  dl_progress_->setVisible(true);
  dl_progress_->setValue(0);
  dl_status_label_->setText(
      QStringLiteral("正在下载 ") + QString::fromStdString(m.display) +
      QStringLiteral(" …"));

  if (!net_mgr_) net_mgr_ = new QNetworkAccessManager(this);
  QNetworkReply* reply =
      net_mgr_->get(QNetworkRequest(QUrl(url)));
  connect(reply, &QNetworkReply::downloadProgress, this,
          &SettingsDialog::OnDownloadProgress);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    this->OnDownloadFinished(reply);
  });
}

void SettingsDialog::OnDownloadClicked() {
  if (current_dl_index_ >= 0) return;  // a download is already in progress
  const QObject* sender_obj = sender();
  if (!sender_obj) return;
  const int index = sender_obj->property("model_index").toInt();
  if (index < 0 || index >= static_cast<int>(model_rows_.size())) return;
  StartDownload(index);
}

void SettingsDialog::OnDownloadProgress(qint64 bytes_received,
                                       qint64 bytes_total) {
  if (bytes_total <= 0) {
    dl_progress_->setRange(0, 0);  // indeterminate
    return;
  }
  dl_progress_->setRange(0, 100);
  dl_progress_->setValue(static_cast<int>(bytes_received * 100 / bytes_total));
  dl_status_label_->setText(
      QStringLiteral("下载中：%1 / %2 MB").arg(bytes_received / (1024 * 1024)).arg(
          bytes_total / (1024 * 1024)));
}

void SettingsDialog::OnDownloadFinished(QNetworkReply* reply) {
  reply->deleteLater();
  if (current_dl_index_ < 0) return;

  if (reply->error() != QNetworkReply::NoError) {
    dl_status_label_->setText(
        QStringLiteral("下载失败：") + reply->errorString());
    dl_progress_->setVisible(false);
    current_dl_index_ = -1;
    OnModelComboChanged();  // re-enable buttons
    return;
  }

  const QByteArray data = reply->readAll();
  {
    QFile f(dl_archive_path_);
    if (!f.open(QIODevice::WriteOnly)) {
      dl_status_label_->setText(
          QStringLiteral("无法写入文件：") + dl_archive_path_);
      dl_progress_->setVisible(false);
      current_dl_index_ = -1;
      OnModelComboChanged();
      return;
    }
    f.write(data);
    f.close();
  }

  dl_status_label_->setText(QStringLiteral("下载完成，正在解压…"));
  dl_progress_->setRange(0, 0);  // indeterminate during extraction

  if (!tar_process_) {
    tar_process_ = new QProcess(this);
    connect(tar_process_, &QProcess::finished, this,
            &SettingsDialog::OnExtractFinished);
  }
  tar_process_->start(
      QStringLiteral("tar"),
      QStringList() << QStringLiteral("-xf") << dl_archive_path_
                    << QStringLiteral("-C") << dl_base_dir_);
  if (!tar_process_->waitForStarted(3000)) {
    dl_status_label_->setText(
        QStringLiteral("解压失败：系统未找到 tar 工具（Windows 10 以上内置）。"));
    dl_progress_->setVisible(false);
    current_dl_index_ = -1;
    OnModelComboChanged();
  }
}

void SettingsDialog::OnExtractFinished(int exit_code,
                                      QProcess::ExitStatus exit_status) {
  const int idx = current_dl_index_;
  current_dl_index_ = -1;

  if (idx < 0 || idx >= static_cast<int>(model_rows_.size())) {
    dl_progress_->setVisible(false);
    return;
  }
  const ModelInfo& m = *model_rows_[idx].info;

  // Clean up the archive regardless of outcome.
  QFile::remove(dl_archive_path_);

  if (exit_status != QProcess::NormalExit || exit_code != 0) {
    dl_status_label_->setText(
        QStringLiteral("解压失败（退出码 %1）。请检查磁盘空间，或手动下载并解压到：")
            .arg(exit_code) +
        dl_base_dir_ + "/" + QString::fromStdString(m.subdir));
    dl_progress_->setVisible(false);
    OnModelComboChanged();
    return;
  }

  const bool ok = IsModelDownloaded(m, config_->model_root);
  if (ok) {
    dl_status_label_->setText(
        QStringLiteral("已下载并解压：") + QString::fromStdString(m.display) +
        QStringLiteral("。重启程序后在“识别模型”中切换即可使用。"));
  } else {
    dl_status_label_->setText(
        QStringLiteral("解压完成但未找到模型文件，请检查压缩包内容。"));
  }
  dl_progress_->setVisible(false);
  OnModelComboChanged();
}

}  // namespace capswriter
