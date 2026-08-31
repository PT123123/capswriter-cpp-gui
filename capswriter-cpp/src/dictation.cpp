#include "dictation.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <vector>

#include "log.h"
#include "text_injector.h"
#include "window_util.h"

namespace capswriter {

namespace {

bool ContainsApp(const std::vector<std::string>& apps,
                 const std::string& process_name) {
  if (process_name.empty()) return false;
  return std::any_of(apps.begin(), apps.end(), [&](const std::string& app) {
    std::string lower = app;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    return lower == process_name;
  });
}

// Resolves data_root against the exe directory (empty = exe directory).
std::string ResolveDataRoot(const Config& config) {
  const std::string& root = config.data_root;
  auto is_absolute = [](const std::string& p) {
    return (p.size() >= 2 && p[1] == ':') || p[0] == '/' || p[0] == '\\';
  };
  if (root.empty()) return ExeDirectory();
  if (is_absolute(root)) return root;
  return ExeDirectory() + "/" + root;
}

}  // namespace

DictationController::DictationController(const Config& config,
                                         AsrEngine* engine)
    : config_(config), engine_(engine) {
  const std::string data_root = ResolveDataRoot(config);
  audio_store_ = std::make_unique<AudioStore>(data_root);
  diary_ = std::make_unique<DiaryWriter>(data_root);
  broadcaster_ = std::make_unique<UdpBroadcaster>(config.udp_broadcast_targets);

  if (config.hot_rule) {
    std::string rule_file = config.rule_file;
    if (rule_file.empty()) rule_file = "hot-rule.txt";
    const int n = rules_.UpdateRules(config.ResolveModelPath(rule_file));
    LOG_INFO("Loaded %d replacement rules", n);
  }
}

DictationController::~DictationController() { Stop(); }

bool DictationController::StartRecording() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (recording_.load() || busy_.load()) return false;

  std::string err;
  if (!recorder_.Start(&err)) {
    LOG_ERROR("Microphone unavailable: %s", err.c_str());
    return false;
  }
  time_start_ = std::time(nullptr);
  start_time_ = std::chrono::steady_clock::now();
  recording_.store(true);
  LOG_INFO("Recording... release the key to recognize.");
  return true;
}

void DictationController::FinishRecording() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!recording_.load()) return;
  recording_.store(false);
  busy_.store(true);
  if (worker_.joinable()) worker_.join();
  worker_ = std::thread(&DictationController::RunPipeline, this, true);
}

void DictationController::CancelRecording() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!recording_.load()) return;
  recording_.store(false);
  busy_.store(true);
  if (worker_.joinable()) worker_.join();
  worker_ = std::thread(&DictationController::RunPipeline, this, false);
}

double DictationController::recording_duration() const {
  if (!recording_.load()) return 0.0;
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now() - start_time_)
      .count();
}

void DictationController::Stop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (worker_.joinable()) worker_.join();
}

void DictationController::RunPipeline(bool recognize) {
  std::vector<float> samples = recorder_.Stop();
  const int sample_rate = recorder_.target_sample_rate();
  const std::time_t t0 = time_start_;
  busy_.store(true);  // stays set until the pipeline finishes

  if (!recognize) {
    LOG_INFO("Recording cancelled.");
    busy_.store(false);
    return;
  }
  if (samples.empty()) {
    LOG_INFO("No audio captured.");
    busy_.store(false);
    return;
  }

  const double duration = static_cast<double>(samples.size()) / sample_rate;

  // Save the recording before renaming it with the recognized text.
  std::string audio_path;
  if (config_.save_audio) {
    audio_path = audio_store_->Save(samples.data(), samples.size(),
                                    sample_rate, t0);
  }

  LOG_INFO("Recognizing %.2fs of audio...", duration);
  std::string text = engine_->Recognize(
      samples.data(), static_cast<int32_t>(samples.size()), sample_rate);
  if (text.empty()) {
    LOG_INFO("No speech recognized.");
    busy_.store(false);
    return;
  }

  // Same post-processing order as ResultProcessor._handle_message:
  // traditional conversion -> trailing punctuation removal -> rule
  // substitution. (The phoneme-level hotword RAG of the Python client is
  // not ported; contextual biasing is available via asr.hotwords_file.)
  if (config_.traditional_convert) {
    text = ConvertTraditional(text, config_.traditional_locale);
  }

  const std::string process_name = GetForegroundProcessName();
  if (!config_.trash_punc.empty()) {
    const bool force = ContainsApp(config_.trash_punc_apps, process_name);
    if (force ||
        CountSemanticUnits(text) <= config_.trash_punc_thresh) {
      text = StripTrailingPunc(text, config_.trash_punc);
    }
  }

  if (config_.hot_rule) {
    text = rules_.Substitute(text);
  }

  LOG_INFO("Result: %s", text.c_str());
  if (result_callback_) result_callback_(text);

  const bool use_paste =
      config_.paste || ContainsApp(config_.paste_apps, process_name);
  const bool ok = use_paste ? PasteUtf8Text(text, config_.restore_clip)
                            : InjectUtf8Text(text);
  if (!ok) {
    LOG_ERROR("Failed to output the recognized text.");
  } else {
    broadcaster_->Broadcast(text);

    // Auto Enter for configured apps (Config.enter_apps).
    for (const EnterAppRule& rule : config_.enter_apps) {
      if (ContainsApp({rule.app}, process_name)) {
        const double delay = rule.delay;
        std::thread([delay] {
          Sleep(static_cast<DWORD>(delay * 1000.0));
          SendEnterKey();
        }).detach();
        LOG_INFO("Auto Enter scheduled in %.2fs", delay);
      }
    }
  }

  // Rename the recording with the final text and write the diary entry.
  if (config_.save_audio && !audio_path.empty()) {
    const std::string final_path = audio_store_->Rename(
        audio_path, text, t0, config_.audio_name_len);
    diary_->Write(text, t0, final_path);
  }

  busy_.store(false);
}

}  // namespace capswriter
