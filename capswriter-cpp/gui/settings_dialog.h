#pragma once

#include <QDialog>
#include <QShowEvent>
#include <QTimer>
#include <functional>
#include <vector>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QFormLayout>

#include "../src/config.h"
#include "models.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QSpinBox;
class QTabWidget;

namespace capswriter {

struct Config;
class AsrEngine;

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
  // engine (optional) is the live ASR engine, used to display which model is
  // actually loaded; restart_fn (optional) performs an application restart
  // (releasing the single-instance mutex so the new process can take over).
  explicit SettingsDialog(Config* config, const std::string& config_path,
                          AsrEngine* engine = nullptr,
                          std::function<void()> restart_fn = {},
                          QWidget* parent = nullptr);

 protected:
  void showEvent(QShowEvent* event) override;

 private slots:
  void OnOk();
  void SaveAndRestart();  // 点击"需重启"标签：保存配置后立即重启
  void OnOpenConfigFolder();
  void OnPasteToggled(bool checked);
  void OnModelComboChanged();
  void OnDownloadClicked();
  void OnDownloadProgress(qint64 bytes_received, qint64 bytes_total);
  void OnDownloadFinished(QNetworkReply* reply);
  void OnExtractFinished(int exit_code, QProcess::ExitStatus exit_status);

  // Builds a labeled settings row, optionally appending a clickable "需重启"
  // tag for restart-needed settings. The tag stays hidden until the user
  // actually edits the control (see RegisterRestartControl / UpdateRestartTag).
  void AddRow(QFormLayout* form, const QString& label, QWidget* control,
              const QString& hint = QString(), bool needs_restart = false,
              std::function<void()> on_restart = {});

 private:
  void BuildUi();
  void LoadFromConfig();
  void ApplyToConfig();
  void ApplyLive();

  // Refreshes the "current active model" display from the live engine (model id,
  // path) and reconciles it against the model selected in the combo box.
  void RefreshCurrentModelDisplay();

  // Starts the download/extraction flow for the given row in model_rows_.
  // Used both by the per-model "下载" buttons and by the save-time guard.
  void StartDownload(int index);
  // Finds the model_rows_ index whose ModelInfo::id matches, or -1.
  int DownloadRowIndexForId(const std::string& id);

  Config* config_;
  std::string config_path_;
  std::string hotwords_file_path_;  // resolved absolute path to hotwords.txt
  AsrEngine* engine_ = nullptr;     // live engine, for "current model" display
  std::function<void()> restart_fn_;  // performs app restart on model switch

  // Snapshot of the config taken when the dialog opens, used to detect whether
  // any restart-needed setting actually changed (so we don't nag the user with
  // the "restart required" notice when nothing relevant was edited).
  Config original_;
  // Polls the engine status until it is ready, so the "current model" box
  // reflects reality even if the dialog was opened during the initial load.
  QTimer* refresh_timer_ = nullptr;

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
  QComboBox* model_combo_ = nullptr;              // restart (model switch)
  QLabel* model_desc_label_ = nullptr;            // selected model description
  QLabel* current_model_label_ = nullptr;         // current active model (id)
  QLabel* current_model_path_label_ = nullptr;    // current active model path
  QLabel* model_mismatch_label_ = nullptr;        // selected vs active mismatch
  QGroupBox* model_mgmt_group_ = nullptr;         // download / status area
  QCheckBox* mirror_check_ = nullptr;             // GitHub mirror acceleration
  QProgressBar* dl_progress_ = nullptr;           // download progress
  QLabel* dl_status_label_ = nullptr;            // download / extraction status
  QComboBox* log_level_combo_ = nullptr;           // live

  // Per-model download rows (downloadable models only).
  struct ModelRow {
    const ModelInfo* info = nullptr;
    QLabel* status = nullptr;        // "已下载 ✓" / "未下载"
    QPushButton* download_btn = nullptr;
  };
  std::vector<ModelRow> model_rows_;

  // In-flight download / extraction state.
  QNetworkAccessManager* net_mgr_ = nullptr;
  QProcess* tar_process_ = nullptr;
  int current_dl_index_ = -1;          // index into model_rows_, or -1
  QString dl_archive_path_;            // downloaded tarball (to extract/clean)
  QString dl_base_dir_;                // model base directory for extraction

  // --- Hotwords tab ---
  QPlainTextEdit* hotwords_edit_ = nullptr;        // restart

  // --- Config file path label ---
  QLabel* path_label_ = nullptr;

  // Restart-needed rows: the control widget, its "需重启" tag, and the value
  // captured when the dialog opens, so the tag only appears after a real edit.
  struct RestartRow {
    QWidget* control = nullptr;
    QLabel* tag = nullptr;
    QString original_value;
  };
  std::vector<RestartRow> restart_rows_;

  // Set while (re)loading widget values from config so the change-signal
  // handlers don't spuriously show the restart tag during a programmatic reset.
  bool reloading_ = false;

  // Registers a restart-needed control with its tag: hides the tag and wires
  // the control's change signal to UpdateRestartTag.
  void RegisterRestartControl(QWidget* control, QLabel* tag);
  // Shows/hides the tag for `control` based on whether its value changed.
  void UpdateRestartTag(QWidget* control);
  // Reads the current value of a settings control as a comparable string.
  QString CurrentValue(QWidget* control) const;
  // Snapshots current values of every restart-needed control as the baseline.
  void CaptureRestartOriginals();
};

}  // namespace capswriter
