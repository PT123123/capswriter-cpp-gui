#pragma once

#include <cstdint>
#include <string>

#include "config.h"

struct SherpaOnnxOfflineRecognizer;
struct SherpaOnnxOfflinePunctuation;

namespace capswriter {

// In-process speech recognition built on the sherpa-onnx C API.
// Replaces the Python server + HTTP round trip of the original project:
// the paraformer recognizer and the ct-transformer punctuation model run
// directly in this process.
class AsrEngine {
 public:
  AsrEngine() = default;
  ~AsrEngine();

  AsrEngine(const AsrEngine&) = delete;
  AsrEngine& operator=(const AsrEngine&) = delete;

  // Loads the paraformer model and (optionally) the punctuation model.
  bool Init(const Config& config, std::string* err);

  // Recognizes one utterance. `samples` must be mono float32 audio at
  // `sample_rate` Hz. Returns the (punctuated when enabled) UTF-8 text,
  // or an empty string on failure / silence.
  std::string Recognize(const float* samples, int32_t n, int32_t sample_rate);

  bool ready() const { return recognizer_ != nullptr; }

 private:
  const SherpaOnnxOfflineRecognizer* recognizer_ = nullptr;
  const SherpaOnnxOfflinePunctuation* punct_ = nullptr;
};

}  // namespace capswriter
