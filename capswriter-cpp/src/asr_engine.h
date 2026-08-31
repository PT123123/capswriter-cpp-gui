#pragma once

#include <cstdint>
#include <string>

#include "config.h"
#include "models.h"

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

  // Loads the manually-configured Paraformer model (asr.paraformer_model /
  // asr.tokens). Used as both the "Paraformer" selection and the fallback for
  // unknown model ids.
  bool InitParaformer(const Config& config, std::string* err);

  // Loads a registry-driven Zipformer model (transducer or Zipformer CTC) by
  // locating its files under the model base directory.
  bool InitRegistryModel(const ModelInfo& info, const Config& config,
                        std::string* err);

  // Loads the optional punctuation model (shared by all model loaders).
  void InitPunctuation(const Config& config);

  // Recognizes one utterance. `samples` must be mono float32 audio at
  // `sample_rate` Hz. Returns the (punctuated when enabled) UTF-8 text,
  // or an empty string on failure / silence.
  std::string Recognize(const float* samples, int32_t n, int32_t sample_rate);

  bool ready() const { return recognizer_ != nullptr; }

  // Identity of the model actually loaded after fallback resolution. Useful for
  // the settings UI to show what is really running (which may differ from the
  // configured selection when the selected model is missing).
  const std::string& loaded_model_id() const { return loaded_model_id_; }

  // Resolved directory/file path of the loaded model (parent dir of the main
  // model files). Exposed by the settings UI so the user can see where the
  // active model lives.
  const std::string& loaded_model_path() const { return loaded_model_path_; }

  // Non-empty only when the actually-loaded model differs from the user's
  // selection (a fallback happened during Init). The GUI surfaces this at
  // startup so the user is never silently running the wrong model.
  const std::string& fallback_notice() const { return fallback_notice_; }

 private:
  const SherpaOnnxOfflineRecognizer* recognizer_ = nullptr;
  const SherpaOnnxOfflinePunctuation* punct_ = nullptr;

  // Populated during Init so the UI can display the genuinely active model.
  std::string loaded_model_id_;
  std::string loaded_model_path_;

  // Set during Init when the loaded model is not what the user selected
  // (fallback). Read by the GUI to warn the user at startup.
  std::string fallback_notice_;

  // True only when asr.paraformer_model / asr.tokens resolve to existing files.
  bool IsParaformerReady(const Config& config) const;

  // Builds fallback_notice_ when the model we will load differs from the
  // user's selection. Called from Init once `resolved` is known.
  void NotifyFallbackIfAny(const Config& config, const ModelInfo* resolved);
};

}  // namespace capswriter
