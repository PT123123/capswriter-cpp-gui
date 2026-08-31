#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace capswriter {

// A capture device enumerated from the system (microphones, etc.).
struct AudioDeviceInfo {
  std::string id;    // endpoint id (opaque string passed to WASAPI GetDevice)
  std::string name;  // human-readable friendly name shown to the user
};

// Enumerate all active capture (recording) endpoints. Returns an empty list
// on failure (e.g. COM unavailable). The list order is not significant; callers
// should treat an empty device id as "system default input device".
std::vector<AudioDeviceInfo> ListCaptureDevices();

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

  // Selects a specific capture device by its endpoint id. Pass an empty string
  // to use the system default input device. Must be set before Start(); it
  // applies to the next capture session.
  void set_device_id(const std::string& id) { device_id_ = id; }

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
  std::string device_id_;  // empty = system default capture endpoint

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
