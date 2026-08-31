#pragma once

#include <string>
#include <vector>

namespace capswriter {

// Directory of the running executable, as a UTF-8 path.
std::string ExeDirectory();

// Per-user roaming config path: %APPDATA%/CapsWriter/config.yaml.
// Falls back to the executable directory when APPDATA is unavailable.
std::string AppDataConfigPath();

// Resolve the config file path to load:
//   1) explicit --config/-c <path> if given,
//   2) %APPDATA%/CapsWriter/config.yaml (migrated from the executable
//      directory on first run),
//   3) fallback to a config.yaml next to the executable.
std::string ResolveConfigPath(int argc, char** argv);

// Mirrors one entry of ClientConfig.shortcuts in config_client.py.
struct ShortcutConfig {
  std::string key;                    // e.g. "caps_lock", "f12", "x2"
  std::string type = "keyboard";      // "keyboard" | "mouse"
  bool suppress = true;               // swallow the key; short presses are re-emitted
  bool hold_mode = true;              // true: hold to record; false: click toggles
  bool enabled = true;
};

struct AsrConfig {
  int num_threads = 4;
  std::string provider = "cpu";
  std::string decoding_method = "greedy_search";
  std::string paraformer_model;  // path to paraformer onnx model
  std::string tokens;            // path to tokens.txt
  // Contextual biasing hotword list fed to sherpa-onnx (one word per line,
  // same format as hot.txt of the original project).
  std::string hotwords_file;
  float hotwords_score = 1.5f;
};

struct PunctConfig {
  bool enabled = true;
  int num_threads = 2;
  std::string provider = "cpu";
  std::string model;  // path to ct-transformer punctuation model
};

struct EnterAppRule {
  std::string app;    // process name, e.g. "happ.exe"
  double delay = 0.5; // seconds to wait before pressing Enter
};

struct UdpTarget {
  std::string addr;  // e.g. "127.255.255.255"
  int port = 6017;
};

// Mirrors the dictation-related fields of config_client.py. LLM, phoneme
// hotword RAG, file transcription and the tray UI are out of scope for the
// C++ port (the tray lives in ../qt-tray-icon-ui/).
struct Config {
  std::vector<ShortcutConfig> shortcuts;
  double threshold = 0.3;  // seconds; presses shorter than this are treated
                           // as normal key clicks (re-emitted when suppressed)

  bool paste = false;        // type via clipboard + Ctrl-V instead of typing
  bool restore_clip = true;  // restore the clipboard after pasting
  std::vector<std::string> paste_apps;  // force paste in these processes

  // Floating hint bar near the input box while dictation is in progress /
  // after output (GUI frontend only; pure text + emoji, no images).
  bool show_hint = true;

  // Auto-Enter after output in these apps (Config.enter_apps).
  std::vector<EnterAppRule> enter_apps;

  bool traditional_convert = false;      // convert output to traditional Chinese
  std::string traditional_locale;        // "zh-hant" | "zh-tw" | "zh-hk"

  // Regex rule replacement, mirrors hot-rule.txt of the original project.
  bool hot_rule = true;
  std::string rule_file;     // path to hot-rule.txt (model_root when relative)

  std::string trash_punc = "\xEF\xBC\x8C\xE3\x80\x82,\xE3\x80\x82.";  // "，。,."
  int trash_punc_thresh = 8;  // strip trailing punctuation when the text is
                              // no longer than this many semantic units
  std::vector<std::string> trash_punc_apps;  // always strip in these apps

  AsrConfig asr;
  PunctConfig punct;

  // UDP broadcast of each recognized text (Config.udp_broadcast).
  bool udp_broadcast = false;
  std::vector<UdpTarget> udp_broadcast_targets;

  // UDP control: START/STOP recording commands from external programs.
  bool udp_control = false;
  std::string udp_control_addr = "127.0.0.1";
  int udp_control_port = 6018;

  std::string model_root;  // base directory for relative model paths

  // Selected capture device (microphone). Empty = use the system default input
  // device. Holds the WASAPI endpoint id; set via the tray menu or the settings
  // dialog. Changing it takes effect on the next recording (no restart needed).
  std::string microphone;

  bool debug = false;
  std::string log_level = "INFO";  // DEBUG | INFO | WARNING | ERROR

  bool LoadFromFile(const std::string& path, std::string* error);

  // Serialize the current config back to a YAML file. Preserves the
  // same field order and structure as config.yaml. Returns false on
  // write failure.
  bool SaveToFile(const std::string& path, std::string* error) const;

  // Resolve a possibly-relative path against model_root (or the executable
  // directory when model_root is empty).
  std::string ResolveModelPath(const std::string& path) const;
};

}  // namespace capswriter
