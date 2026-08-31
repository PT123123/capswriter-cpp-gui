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

void EmitEvent(const DictationEventCallback& cb, DictationEvent ev,
               OutputKind kind, const std::string& text) {
  if (cb) cb(ev, kind, text);
}

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

}  // namespace

DictationController::DictationController(const Config& config,
                                         AsrEngine* engine)
    : config_(config), engine_(engine) {
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
  recorder_.set_device_id(config_.microphone);
  if (!recorder_.Start(&err)) {
    LOG_ERROR("Microphone unavailable: %s", err.c_str());
    return false;
  }
  time_start_ = std::time(nullptr);
  start_time_ = std::chrono::steady_clock::now();
  recording_.store(true);
  LOG_INFO("Recording... release the key to recognize.");
  EmitEvent(event_callback_, DictationEvent::kRecordingStarted, OutputKind::kTyped, "");
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
  // Run on a worker thread with no C++ exception safety net: any throw here
  // would escape std::thread and call std::terminate -> abort (0xc0000409)
  // with no log. Catch it so a bad text/rule/injector step fails gracefully
  // and is recorded instead of silently crashing the app.
  try {
    RunPipelineImpl(recognize);
  } catch (const std::exception& e) {
    LOG_ERROR("RunPipeline threw (uncaught exception): %s", e.what());
    busy_.store(false);
  } catch (...) {
    LOG_ERROR("RunPipeline threw (unknown exception type)");
    busy_.store(false);
  }
}

void DictationController::RunPipelineImpl(bool recognize) {
  std::vector<float> samples = recorder_.Stop();
  const int sample_rate = recorder_.target_sample_rate();
  busy_.store(true);  // stays set until the pipeline finishes

  if (!recognize) {
    LOG_INFO("Recording cancelled.");
    EmitEvent(event_callback_, DictationEvent::kRecordingEnded, OutputKind::kTyped, "");
    busy_.store(false);
    return;
  }
  if (samples.empty()) {
    LOG_INFO("No audio captured.");
    EmitEvent(event_callback_, DictationEvent::kRecordingEnded, OutputKind::kTyped, "");
    busy_.store(false);
    return;
  }

  const double duration = static_cast<double>(samples.size()) / sample_rate;

  LOG_INFO("Recognizing %.2fs of audio...", duration);
  std::string text = engine_->Recognize(
      samples.data(), static_cast<int32_t>(samples.size()), sample_rate);
  if (text.empty()) {
    LOG_INFO("No speech recognized.");
    EmitEvent(event_callback_, DictationEvent::kRecordingEnded, OutputKind::kTyped, "");
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

  // When the hotkey fires outside a text input (desktop, file manager,
  // our own UI, ...), typing would go nowhere: copy to the clipboard
  // instead and let the frontend say so.
  if (!HasTextInputFocus()) {
    if (CopyUtf8Text(text)) {
      broadcaster_->Broadcast(text);
      LOG_INFO("No text input focused; result copied to the clipboard.");
      EmitEvent(event_callback_, DictationEvent::kTextOutput,
                OutputKind::kCopied, text);
    } else {
      LOG_ERROR("Failed to copy the recognized text to the clipboard.");
      EmitEvent(event_callback_, DictationEvent::kOutputFailed,
                OutputKind::kCopied, text);
    }
    busy_.store(false);
    return;
  }

  const bool ok = use_paste ? PasteUtf8Text(text, config_.restore_clip)
                            : InjectUtf8Text(text);
  if (!ok) {
    LOG_ERROR("Failed to output the recognized text.");
    EmitEvent(event_callback_, DictationEvent::kOutputFailed,
              use_paste ? OutputKind::kPasted : OutputKind::kTyped, text);
  } else {
    broadcaster_->Broadcast(text);
    EmitEvent(event_callback_, DictationEvent::kTextOutput,
              use_paste ? OutputKind::kPasted : OutputKind::kTyped, text);

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

  busy_.store(false);
}

}  // namespace capswriter
