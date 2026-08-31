#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTabWidget;

namespace capswriter {

struct Config;

// Settings dialog for the CapsWriter C++ client. Displays all user-facing
// configuration in a tabbed dark-themed QDialog, matching the main window
// style. On "OK", writes the edited values back to the Config struct, saves
// them to config.yaml, and applies live-updatable fields immediately.
//
// Settings are classified into two categories:
//  - Live:   applied instantly (DictationController / HotkeyDispatch read
//            the Config by const reference each pipeline / key event).
//  - Restart: model paths, thread counts, UDP sockets — the engine or
//            hooks are already initialized; the user must restart for
//            these to take effect. These are flagged with a restart icon.
class SettingsDialog : public QDialog {
  Q_OBJECT

 public:
  // config_path is the path to config.yaml (for saving + showing location).
  // config is a mutable pointer to the live Config struct owned by AppRuntime.
  explicit SettingsDialog(Config* config, const std::string& config_path,
                          QWidget* parent = nullptr);

 private slots:
  void OnOk();
  void OnOpenConfigFolder();
  void OnPasteToggled(bool checked);

 private:
  void BuildUi();
  void LoadFromConfig();
  void ApplyToConfig();
  void ApplyLive();

  Config* config_;
  std::string config_path_;
  std::string hotwords_file_path_;  // resolved absolute path to hotwords.txt

  // --- Output tab ---
  QCheckBox* paste_check_ = nullptr;
  QCheckBox* restore_clip_check_ = nullptr;
  QCheckBox* show_hint_check_ = nullptr;
  QDoubleSpinBox* threshold_spin_ = nullptr;
  QComboBox* microphone_combo_ = nullptr;  // empty item = system default

  // --- Text tab ---
  QCheckBox* traditional_check_ = nullptr;
  QComboBox* traditional_locale_combo_ = nullptr;
  QCheckBox* hot_rule_check_ = nullptr;
  QSpinBox* trash_punc_thresh_spin_ = nullptr;
  QCheckBox* punct_check_ = nullptr;  // restart

  // --- Network tab ---
  QCheckBox* udp_broadcast_check_ = nullptr;  // restart
  QCheckBox* udp_control_check_ = nullptr;    // restart
  QLineEdit* udp_control_addr_edit_ = nullptr;  // restart
  QSpinBox* udp_control_port_spin_ = nullptr;   // restart

  // --- Engine tab ---
  QSpinBox* asr_threads_spin_ = nullptr;          // restart
  QComboBox* asr_provider_combo_ = nullptr;        // restart
  QComboBox* asr_decoding_combo_ = nullptr;        // restart
  QDoubleSpinBox* asr_hotwords_score_spin_ = nullptr;  // restart
  QComboBox* log_level_combo_ = nullptr;           // live

  // --- Hotwords tab ---
  QPlainTextEdit* hotwords_edit_ = nullptr;        // restart

  // --- Config file path label ---
  QLabel* path_label_ = nullptr;
};

}  // namespace capswriter
