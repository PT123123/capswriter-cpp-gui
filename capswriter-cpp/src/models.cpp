#include "models.h"

#include <algorithm>
#include <filesystem>

#include "config.h"

namespace capswriter {
namespace {

bool StartsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), s.begin());
}

bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

// First file in `dir` whose name starts with `prefix` and ends with `suffix`.
std::string FindFile(const std::string& dir, const std::string& prefix,
                     const std::string& suffix) {
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return "";
  std::string best;
  for (const auto& entry :
       std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    const std::string name = entry.path().filename().string();
    if (StartsWith(name, prefix) && EndsWith(name, suffix)) {
      // Prefer the shortest match (e.g. model.onnx over model.int8.onnx when
      // both happen to exist) — but keep it deterministic.
      if (best.empty() || name.size() < best.size()) best = name;
    }
  }
  if (best.empty()) return "";
  return (std::filesystem::path(dir) / best).string();
}

}  // namespace

const std::vector<ModelInfo>& GetModelRegistry() {
  static const std::vector<ModelInfo> kModels = {
      {
          /* id */ "Paraformer",
          /* display */ "Paraformer（内置，手动配置路径）",
          /* type */ AsrModelType::kParaformer,
          /* subdir */ "",  // uses asr.paraformer_model / asr.tokens directly
          /* url */ "",
          /* downloadable */ false,
          /* desc */ "当前默认模型，约 1GB 内存；需在设置中指定模型与 tokens 路径。",
      },
      {
          // NOTE: 本注册表只收 OFFLINE（非流式）Transducer 模型。
          // "sherpa-onnx-streaming-zipformer-*" 是 ONLINE（流式）模型，喂给
          // SherpaOnnxCreateOfflineRecognizer 会"加载成功"却在解码阶段崩
          // (0xc0000409)。下面这个 zh-en-2023-11-22 是官方离线版，解码安全。
          /* id */ "ZipformerBilingual",
          /* display */ "Zipformer 中英双语（离线 Transducer）",
          /* type */ AsrModelType::kTransducer,
          /* subdir */ "sherpa-onnx-zipformer-zh-en-2023-11-22",
          /* url */
          "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/"
          "sherpa-onnx-zipformer-zh-en-2023-11-22.tar.bz2",
          /* downloadable */ true,
          /* desc */ "离线 Transducer 模型，支持中英双语；需先下载再重启生效。",
      },
      {
          // 离线（非流式）Zipformer CTC 模型。注意：k2-fsa 的
          // "sherpa-onnx-streaming-zipformer-*-ctc-*" 是流式 CTC，喂给离线
          // zipformer_ctc 配置会在解码阶段崩（已用官方离线 CLI 实锤）；必须用
          // 不带 "streaming" 的离线版（如下）。
          /* id */ "Zipformer2CtcZhInt8",
          /* display */ "Zipformer CTC 中文 int8（离线，中文准确率高）",
          /* type */ AsrModelType::kZipformerCtc,
          /* subdir */ "sherpa-onnx-zipformer-ctc-zh-int8-2025-07-03",
          /* url */
          "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/"
          "sherpa-onnx-zipformer-ctc-zh-int8-2025-07-03.tar.bz2",
          /* downloadable */ true,
          /* desc */ "离线 CTC 模型，中文识别准确率高，int8 量化、内存适中。",
      },
  };
  return kModels;
}

const ModelInfo* FindModel(const std::string& id) {
  for (const auto& m : GetModelRegistry()) {
    if (m.id == id) return &m;
  }
  return nullptr;
}

std::string ModelBaseDir(const std::string& model_root) {
  std::string base = model_root.empty() ? ExeDirectory() : model_root;
  if (!std::filesystem::path(base).is_absolute()) {
    base = ExeDirectory() + "/" + base;
  }
  return base;
}

ResolvedModelFiles ResolveModelFiles(const ModelInfo& m,
                                     const std::string& model_root) {
  ResolvedModelFiles r;
  if (m.type == AsrModelType::kParaformer) {
    // Paraformer is configured via explicit paths, not the registry subdir.
    return r;
  }

  const std::string dir = ModelBaseDir(model_root) + "/" + m.subdir;
  r.tokens = FindFile(dir, "tokens", ".txt");

  if (m.type == AsrModelType::kTransducer) {
    r.encoder = FindFile(dir, "encoder", ".onnx");
    r.decoder = FindFile(dir, "decoder", ".onnx");
    r.joiner = FindFile(dir, "joiner", ".onnx");
    r.ok = !r.encoder.empty() && !r.decoder.empty() &&
           !r.joiner.empty() && !r.tokens.empty();
  } else if (m.type == AsrModelType::kZipformerCtc) {
    r.ctc_model = FindFile(dir, "model", ".onnx");
    r.ok = !r.ctc_model.empty() && !r.tokens.empty();
  }
  return r;
}

bool IsModelDownloaded(const ModelInfo& m, const std::string& model_root) {
  if (!m.downloadable) return false;
  return ResolveModelFiles(m, model_root).ok;
}

}  // namespace capswriter
