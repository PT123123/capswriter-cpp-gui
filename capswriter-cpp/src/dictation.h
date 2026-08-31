#pragma once

#include <atomic>
#include <chrono>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "asr_engine.h"
#include "audio_recorder.h"
#include "audio_store.h"
#include "config.h"
#include "diary.h"
#include "text_transform.h"
#include "udp_io.h"

namespace capswriter {

// Owns the full dictation pipeline: recording, recognition, post-processing
// (traditional conversion, trailing punctuation removal, rule substitution),
// output (typing / pasting / auto Enter), UDP broadcast, recording archival
// and diary writing. Replaces ResultProcessor + AudioStreamManager +
// AudioFileManager + DiaryWriter wiring of the Python client.
//
// StartRecording() is called from the hotkey hook thread or the UDP control
// thread; FinishRecording()/CancelRecording() return immediately and run the
// recognition pipeline on a worker thread, so neither the low-level input
// hook nor the UDP listener ever blocks on the recognizer.
class DictationController {
 public:
  DictationController(const Config& config, AsrEngine* engine);
  ~DictationController();

  DictationController(const DictationController&) = delete;
  DictationController& operator=(const DictationController&) = delete;

  // Starts a new recording; false when already recording/busy or when the
  // microphone cannot be opened.
  bool StartRecording();

  // Stops the recording and recognizes it asynchronously.
  void FinishRecording();

  // Stops the recording and discards it (short press / aborted click).
  void CancelRecording();

  bool recording() const { return recording_.load(); }
  bool busy() const { return busy_.load(); }

  // Seconds since the current recording started (steady clock, sub-second).
  double recording_duration() const;

  // Wall-clock timestamp of the current/last recording start.
  std::time_t time_start() const { return time_start_; }

  // Optional observer notified with the final post-processed text right
  // before output (used by the Qt frontend to show the recognition
  // history). Called on the pipeline worker thread.
  void set_result_callback(std::function<void(const std::string&)> cb) {
    result_callback_ = std::move(cb);
  }

  // Joins the worker thread (for shutdown).
  void Stop();

 private:
  void RunPipeline(bool recognize);

  const Config& config_;
  AsrEngine* engine_;

  WasapiRecorder recorder_;
  std::unique_ptr<AudioStore> audio_store_;
  std::unique_ptr<DiaryWriter> diary_;
  std::unique_ptr<UdpBroadcaster> broadcaster_;
  RuleCorrector rules_;

  std::mutex mutex_;                 // guards state transitions
  std::atomic<bool> recording_{false};
  std::atomic<bool> busy_{false};
  std::thread worker_;
  std::chrono::steady_clock::time_point start_time_{};
  std::time_t time_start_ = 0;
  std::function<void(const std::string&)> result_callback_;
};

}  // namespace capswriter
