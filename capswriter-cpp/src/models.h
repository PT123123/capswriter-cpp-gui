#pragma once

#include <string>
#include <vector>

namespace capswriter {

// Family of ASR model the offline engine knows how to load via sherpa-onnx.
enum class AsrModelType {
  kParaformer,     // sherpa-onnx Paraformer (offline); configured via
                   // asr.paraformer_model / asr.tokens paths.
  kTransducer,     // sherpa-onnx streaming Zipformer transducer (offline
                   // transducer: encoder/decoder/joiner + tokens).
  kZipformerCtc,   // sherpa-onnx Zipformer CTC (zipformer_ctc.model + tokens).
};

// Static description of one selectable / downloadable recognition model.
struct ModelInfo {
  std::string id;       // stored in config.asr.model; stable identifier.
  std::string display;  // human-readable label shown in the UI.
  AsrModelType type;
  std::string subdir;   // folder name the archive extracts to, under the
                        // model base directory.
  std::string url;      // full GitHub release tarball URL (raw, no mirror).
  bool downloadable;    // false for the manually-configured Paraformer.
  std::string desc;     // one-line description (size / RAM / languages).
};

// The complete list of models offered in the settings UI. This is the single
// source of truth for both the engine (how to load) and the GUI (how to
// download / display). Add or reorder models here only.
const std::vector<ModelInfo>& GetModelRegistry();

// Look up a model by its stable id, or nullptr if unknown.
const ModelInfo* FindModel(const std::string& id);

// Base directory where models are installed. Mirrors the resolution used by
// Config::ResolveModelPath: model_root when set, otherwise the executable
// directory.
std::string ModelBaseDir(const std::string& model_root);

// Concrete on-disk file paths for a model, discovered by globbing the
// extracted sub-directory (epoch / revision numbers vary between releases).
// `ok` is true only when every file required by the model family exists.
struct ResolvedModelFiles {
  bool ok = false;
  std::string encoder;   // transducer encoder  (*.onnx)
  std::string decoder;   // transducer decoder  (*.onnx)
  std::string joiner;    // transducer joiner   (*.onnx)
  std::string tokens;    // tokens.txt
  std::string ctc_model; // zipformer_ctc model (*.onnx)
};

ResolvedModelFiles ResolveModelFiles(const ModelInfo& m,
                                     const std::string& model_root);

// Convenience: true when the model's required files are present on disk.
bool IsModelDownloaded(const ModelInfo& m, const std::string& model_root);

}  // namespace capswriter
