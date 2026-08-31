#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace capswriter {

// Captures audio from the default capture device using WASAPI (shared mode),
// downmixes to mono, and linearly resamples to the target sample rate
// (16 kHz for the paraformer models).
class WasapiRecorder {
 public:
  WasapiRecorder();
  ~WasapiRecorder();

  WasapiRecorder(const WasapiRecorder&) = delete;
  WasapiRecorder& operator=(const WasapiRecorder&) = delete;

  void set_target_sample_rate(int rate) { target_sample_rate_ = rate; }

  // Starts a capture session. Blocks until the capture device is opened
  // (or fails). Returns false with *err set on failure.
  bool Start(std::string* err);

  // Stops capture and returns the recorded audio as mono float32 samples
  // at target_sample_rate(). Returns an empty vector when nothing was
  // recorded.
  std::vector<float> Stop();

  bool recording() const { return recording_.load(); }
  int target_sample_rate() const { return target_sample_rate_; }

 private:
  void CaptureThread();

  int target_sample_rate_ = 16000;

  std::thread thread_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> recording_{false};

  std::mutex start_mutex_;
  std::condition_variable start_cv_;
  bool start_done_ = false;
  bool start_ok_ = false;
  std::string start_error_;

  std::mutex buffer_mutex_;
  std::vector<float> buffer_;  // mono float32 at device sample rate
  int device_sample_rate_ = 0;
};

}  // namespace capswriter
