#include "asr_engine.h"

#include <filesystem>
#include <sherpa-onnx/c-api/c-api.h>

#include "log.h"
#include "models.h"

namespace capswriter {

AsrEngine::~AsrEngine() {
  if (recognizer_) SherpaOnnxDestroyOfflineRecognizer(recognizer_);
  if (punct_) SherpaOnnxDestroyOfflinePunctuation(punct_);
}

// Loads the Paraformer model configured through asr.paraformer_model /
// asr.tokens. Kept as the default / fallback path.
bool AsrEngine::InitParaformer(const Config& config, std::string* err) {
  const std::string model =
      config.ResolveModelPath(config.asr.paraformer_model);
  const std::string tokens = config.ResolveModelPath(config.asr.tokens);
  if (model.empty() || tokens.empty()) {
    if (err) *err = "asr.paraformer_model / asr.tokens are not configured";
    return false;
  }

  // Hotwords (contextual biasing) only work with modified_beam_search.
  // Paraformer is limited to greedy_search, and sherpa-onnx rejects the config
  // (failing to create the recognizer) when a hotwords_file is given together
  // with greedy_search. Skip it in that case so the app still starts.
  std::string hotwords;
  if (config.asr.decoding_method == "modified_beam_search") {
    hotwords = config.ResolveModelPath(config.asr.hotwords_file);
    std::error_code ec;
    if (!hotwords.empty() && !std::filesystem::exists(hotwords, ec)) {
      hotwords.clear();
    }
  }

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
    if (err) {
      *err = "failed to create the sherpa-onnx offline recognizer (paraformer: " +
             model + ")";
    }
    return false;
  }
  loaded_model_id_ = "Paraformer";
  loaded_model_path_ = std::filesystem::path(model).parent_path().string();
  InitPunctuation(config);
  return true;
}

// Loads a registry-driven model (Zipformer transducer / Zipformer CTC) by
// locating its files under the model base directory.
bool AsrEngine::InitRegistryModel(const ModelInfo& info, const Config& config,
                                  std::string* err) {
  const ResolvedModelFiles files = ResolveModelFiles(info, config.model_root);
  if (!files.ok) {
    if (err) {
      *err = "model \"" + info.id +
             "\" is not downloaded yet (missing files under " +
             ModelBaseDir(config.model_root) + "/" + info.subdir + ")";
    }
    return false;
  }

  // Hotwords (contextual biasing) only work with modified_beam_search.
  // Paraformer is limited to greedy_search, and sherpa-onnx rejects the config
  // (failing to create the recognizer) when a hotwords_file is given together
  // with greedy_search. Skip it in that case so the app still starts.
  std::string hotwords;
  if (config.asr.decoding_method == "modified_beam_search") {
    hotwords = config.ResolveModelPath(config.asr.hotwords_file);
    std::error_code ec;
    if (!hotwords.empty() && !std::filesystem::exists(hotwords, ec)) {
      hotwords.clear();
    }
  }

  SherpaOnnxOfflineRecognizerConfig recognizer_config{};
  recognizer_config.feat_config.sample_rate = 16000;
  recognizer_config.feat_config.feature_dim = 80;
  recognizer_config.model_config.tokens = files.tokens.c_str();
  recognizer_config.model_config.num_threads = config.asr.num_threads;
  recognizer_config.model_config.debug = config.debug ? 1 : 0;
  recognizer_config.model_config.provider = config.asr.provider.c_str();
  recognizer_config.decoding_method = config.asr.decoding_method.c_str();
  recognizer_config.hotwords_file = hotwords.c_str();
  recognizer_config.hotwords_score = config.asr.hotwords_score;

  if (info.type == AsrModelType::kTransducer) {
    recognizer_config.model_config.transducer.encoder = files.encoder.c_str();
    recognizer_config.model_config.transducer.decoder = files.decoder.c_str();
    recognizer_config.model_config.transducer.joiner = files.joiner.c_str();
    LOG_INFO("Loading Zipformer transducer model (encoder=%s)",
             files.encoder.c_str());
  } else {  // kZipformerCtc
    recognizer_config.model_config.zipformer_ctc.model = files.ctc_model.c_str();
    LOG_INFO("Loading Zipformer CTC model (%s)", files.ctc_model.c_str());
  }

  recognizer_ = SherpaOnnxCreateOfflineRecognizer(&recognizer_config);
  if (!recognizer_) {
    if (err) {
      *err = "failed to create the sherpa-onnx offline recognizer (" + info.id +
             ": " + files.tokens + ")";
    }
    return false;
  }
  loaded_model_id_ = info.id;
  loaded_model_path_ =
      (std::filesystem::path(ModelBaseDir(config.model_root)) / info.subdir)
          .string();
  InitPunctuation(config);
  return true;
}

bool AsrEngine::Init(const Config& config, std::string* err) {
  const std::string& selected = config.asr.model;
  const ModelInfo* info = FindModel(selected);

  // Resolve the model we will actually load: the selected one if it's locally
  // available, otherwise fall back to a usable model so the app can still start
  // (the user can download the selected model and restart to switch to it).
  const ModelInfo* resolved = nullptr;
  if (info && info->type == AsrModelType::kParaformer) {
    if (IsParaformerReady(config)) resolved = info;
  } else if (info && info->downloadable) {
    if (IsModelDownloaded(*info, config.model_root)) resolved = info;
  }

  if (!resolved) {
    if (IsParaformerReady(config)) {
      LOG_WARN(
          "Selected model \"%s\" is not available locally; falling back to "
          "Paraformer.",
          selected.c_str());
      resolved = FindModel("Paraformer");
    } else {
      for (const auto& m : GetModelRegistry()) {
        if (m.downloadable && IsModelDownloaded(m, config.model_root)) {
          LOG_WARN(
              "Selected model \"%s\" is not available locally; falling back to "
              "%s.",
              selected.c_str(), m.id.c_str());
          resolved = &m;
          break;
        }
      }
    }
  }

  if (!resolved) {
    if (err) {
      *err =
          "没有可用的 ASR 模型：请先在设置中下载一个模型，或配置 Paraformer "
          "模型路径（asr.paraformer_model / asr.tokens）。";
    }
    return false;
  }

  NotifyFallbackIfAny(config, resolved);

  if (resolved->type == AsrModelType::kParaformer) {
    return InitParaformer(config, err);
  }
  return InitRegistryModel(*resolved, config, err);
}

void AsrEngine::NotifyFallbackIfAny(const Config& config,
                                    const ModelInfo* resolved) {
  if (!resolved) return;
  const std::string& selected = config.asr.model;
  // Only flag a real fallback: the user picked a specific model and we ended
  // up loading a different one. An empty selection or an exact match is fine.
  if (selected.empty() || selected == resolved->id) return;

  const ModelInfo* sel_info = FindModel(selected);
  const std::string sel_name = sel_info ? sel_info->display : selected;
  const std::string res_name = resolved->display;
  fallback_notice_ =
      "所选模型「" + sel_name +
      "」当前不可用，已自动回退并加载「" + res_name +
      "」。如需使用该模型，请先在设置中下载对应模型，然后重启本程序。";
}

bool AsrEngine::IsParaformerReady(const Config& config) const {
  const std::string model =
      config.ResolveModelPath(config.asr.paraformer_model);
  const std::string tokens = config.ResolveModelPath(config.asr.tokens);
  if (model.empty() || tokens.empty()) return false;
  std::error_code ec;
  return std::filesystem::exists(model, ec) &&
         std::filesystem::exists(tokens, ec);
}

void AsrEngine::InitPunctuation(const Config& config) {
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
