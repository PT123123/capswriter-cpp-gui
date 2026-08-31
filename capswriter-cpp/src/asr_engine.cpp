#include "asr_engine.h"

#include <sherpa-onnx/c-api/c-api.h>

#include "log.h"

namespace capswriter {

AsrEngine::~AsrEngine() {
  if (recognizer_) SherpaOnnxDestroyOfflineRecognizer(recognizer_);
  if (punct_) SherpaOnnxDestroyOfflinePunctuation(punct_);
}

bool AsrEngine::Init(const Config& config, std::string* err) {
  const std::string model =
      config.ResolveModelPath(config.asr.paraformer_model);
  const std::string tokens = config.ResolveModelPath(config.asr.tokens);
  if (model.empty() || tokens.empty()) {
    if (err) *err = "asr.paraformer_model / asr.tokens are not configured";
    return false;
  }

  const std::string hotwords =
      config.ResolveModelPath(config.asr.hotwords_file);

  SherpaOnnxOfflineRecognizerConfig recognizer_config{};
  recognizer_config.feat_config.sample_rate = 16000;
  recognizer_config.feat_config.feature_dim = 80;
  recognizer_config.model_config.paraformer.model = model.c_str();
  recognizer_config.model_config.tokens = tokens.c_str();
  recognizer_config.model_config.num_threads = config.asr.num_threads;
  recognizer_config.model_config.debug = config.debug ? 1 : 0;
  recognizer_config.model_config.provider = config.asr.provider.c_str();
  recognizer_config.decoding_method = config.asr.decoding_method.c_str();
  recognizer_config.hotwords_file = hotwords.c_str();
  recognizer_config.hotwords_score = config.asr.hotwords_score;

  LOG_INFO("Loading paraformer model: %s", model.c_str());
  recognizer_ = SherpaOnnxCreateOfflineRecognizer(&recognizer_config);
  if (!recognizer_) {
    if (err) *err = "failed to create the sherpa-onnx offline recognizer";
    return false;
  }

  if (config.punct.enabled && !config.punct.model.empty()) {
    const std::string punct_model =
        config.ResolveModelPath(config.punct.model);
    SherpaOnnxOfflinePunctuationConfig punct_config{};
    punct_config.model.ct_transformer = punct_model.c_str();
    punct_config.model.num_threads = config.punct.num_threads;
    punct_config.model.debug = config.debug ? 1 : 0;
    punct_config.model.provider = config.punct.provider.c_str();

    LOG_INFO("Loading punctuation model: %s", punct_model.c_str());
    punct_ = SherpaOnnxCreateOfflinePunctuation(&punct_config);
    if (!punct_) {
      LOG_WARN("Failed to load the punctuation model; continuing without it");
      punct_ = nullptr;
    }
  }

  return true;
}

std::string AsrEngine::Recognize(const float* samples, int32_t n,
                                 int32_t sample_rate) {
  if (!recognizer_ || !samples || n <= 0) return "";

  const SherpaOnnxOfflineStream* stream =
      SherpaOnnxCreateOfflineStream(recognizer_);
  if (!stream) return "";

  SherpaOnnxAcceptWaveformOffline(stream, sample_rate, samples, n);
  SherpaOnnxDecodeOfflineStream(recognizer_, stream);

  std::string text;
  const SherpaOnnxOfflineRecognizerResult* result =
      SherpaOnnxGetOfflineStreamResult(stream);
  if (result && result->text) text = result->text;
  if (result) SherpaOnnxDestroyOfflineRecognizerResult(result);
  SherpaOnnxDestroyOfflineStream(stream);

  if (!text.empty() && punct_) {
    const char* with_punct = SherpaOfflinePunctuationAddPunct(
        punct_, text.c_str());
    if (with_punct) {
      text = with_punct;
      SherpaOfflinePunctuationFreeText(with_punct);
    }
  }
  return text;
}

}  // namespace capswriter
