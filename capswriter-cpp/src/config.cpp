#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>

namespace capswriter {
namespace {

struct Node {
  std::map<std::string, Node> children;
  std::vector<std::string> items;  // list of scalars
  std::vector<Node> item_maps;     // list of maps
  std::string scalar;
};

std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Strip a trailing comment (a '#' not inside quotes).
std::string StripComment(const std::string& s) {
  bool in_single = false, in_double = false;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '\'' && !in_double) in_single = !in_single;
    if (c == '"' && !in_single) in_double = !in_double;
    if (c == '#' && !in_single && !in_double) {
      if (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t') return s.substr(0, i);
    }
  }
  return s;
}

std::string Unquote(const std::string& s) {
  if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                         (s.front() == '\'' && s.back() == '\''))) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

struct Line {
  int indent;
  std::string text;
};

Node ParseBlock(const std::vector<Line>& lines, size_t& i, int indent) {
  Node node;
  while (i < lines.size()) {
    const Line& line = lines[i];
    if (line.indent < indent) break;
    if (line.indent > indent) {
      ++i;  // stray deeper line; skip defensively
      continue;
    }

    if (!line.text.empty() && line.text[0] == '-') {
      std::string rest = Trim(line.text.substr(1));
      ++i;
      size_t colon = rest.find(':');
      if (colon == std::string::npos) {
        node.items.push_back(Unquote(rest));
        continue;
      }
      // List item that is itself a map: "- key: value".
      Node item;
      std::string key = Trim(rest.substr(0, colon));
      std::string value = Trim(rest.substr(colon + 1));
      Node child;
      child.scalar = Unquote(value);
      item.children[key] = child;
      // Consume the item's continuation lines (deeper indent).
      if (i < lines.size() && lines[i].indent > indent) {
        Node more = ParseBlock(lines, i, lines[i].indent);
        for (auto& kv : more.children) item.children[kv.first] = kv.second;
        for (auto& s : more.items) item.items.push_back(s);
      }
      node.item_maps.push_back(item);
      continue;
    }

    size_t colon = line.text.find(':');
    if (colon == std::string::npos) {
      ++i;
      continue;
    }
    std::string key = Trim(line.text.substr(0, colon));
    std::string value = Trim(line.text.substr(colon + 1));
    ++i;

    if (value.empty()) {
      if (i < lines.size() && lines[i].indent > indent) {
        node.children[key] = ParseBlock(lines, i, lines[i].indent);
      } else {
        node.children[key] = Node{};
      }
    } else {
      Node child;
      child.scalar = Unquote(value);
      node.children[key] = child;
    }
  }
  return node;
}

const Node* Find(const Node& node, const std::string& path) {
  const Node* cur = &node;
  size_t start = 0;
  while (start <= path.size()) {
    size_t dot = path.find('.', start);
    std::string part = path.substr(
        start, dot == std::string::npos ? std::string::npos : dot - start);
    auto it = cur->children.find(part);
    if (it == cur->children.end()) return nullptr;
    cur = &it->second;
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  return cur;
}

std::string GetStr(const Node& root, const std::string& path,
                   const std::string& fallback) {
  const Node* n = Find(root, path);
  return (n && !n->scalar.empty()) ? n->scalar : fallback;
}

int GetInt(const Node& root, const std::string& path, int fallback) {
  const Node* n = Find(root, path);
  if (!n || n->scalar.empty()) return fallback;
  try {
    return std::stoi(n->scalar);
  } catch (...) {
    return fallback;
  }
}

double GetDouble(const Node& root, const std::string& path, double fallback) {
  const Node* n = Find(root, path);
  if (!n || n->scalar.empty()) return fallback;
  try {
    return std::stod(n->scalar);
  } catch (...) {
    return fallback;
  }
}

bool GetBool(const Node& root, const std::string& path, bool fallback) {
  const Node* n = Find(root, path);
  if (!n || n->scalar.empty()) return fallback;
  std::string v = n->scalar;
  std::transform(v.begin(), v.end(), v.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
  if (v == "false" || v == "0" || v == "no" || v == "off") return false;
  return fallback;
}

std::vector<std::string> GetStrList(const Node& root, const std::string& path) {
  const Node* n = Find(root, path);
  if (!n) return {};
  if (!n->items.empty()) return n->items;
  if (!n->scalar.empty()) return {n->scalar};
  return {};
}

}  // namespace

std::string ExeDirectory() {
  wchar_t buf[MAX_PATH * 4];
  DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
  std::wstring path(buf, len);
  size_t pos = path.find_last_of(L"\\/");
  if (pos == std::string::npos) return ".";
  std::wstring dir = path.substr(0, pos);
  int n = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, nullptr, 0,
                              nullptr, nullptr);
  std::string utf8(n ? n - 1 : 0, '\0');
  if (n > 1) {
    WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, &utf8[0], n, nullptr,
                        nullptr);
  }
  return utf8;
}

bool Config::LoadFromFile(const std::string& path, std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "cannot open config file: " + path;
    return false;
  }

  std::vector<Line> lines;
  std::string raw;
  while (std::getline(in, raw)) {
    std::string s = StripComment(raw);
    std::string t = Trim(s);
    if (t.empty()) continue;
    int indent = 0;
    while (indent < static_cast<int>(s.size()) && s[indent] == ' ') ++indent;
    lines.push_back({indent, t});
  }

  size_t i = 0;
  Node root;
  if (!lines.empty()) root = ParseBlock(lines, i, lines[0].indent);

  if (const Node* sc = Find(root, "shortcuts")) {
    for (const Node& item : sc->item_maps) {
      ShortcutConfig entry;
      entry.key = GetStr(item, "key", "");
      entry.type = GetStr(item, "type", "keyboard");
      entry.suppress = GetBool(item, "suppress", true);
      entry.hold_mode = GetBool(item, "hold_mode", true);
      entry.enabled = GetBool(item, "enabled", true);
      if (!entry.key.empty()) shortcuts.push_back(entry);
    }
  }

  threshold = GetDouble(root, "threshold", threshold);

  paste = GetBool(root, "paste", paste);
  restore_clip = GetBool(root, "restore_clip", restore_clip);
  paste_apps = GetStrList(root, "paste_apps");

  if (const Node* n = Find(root, "enter_apps")) {
    for (const Node& item : n->item_maps) {
      EnterAppRule rule;
      rule.app = GetStr(item, "app", "");
      rule.delay = GetDouble(item, "delay", 0.5);
      if (!rule.app.empty()) enter_apps.push_back(rule);
    }
  }

  save_audio = GetBool(root, "save_audio", save_audio);
  audio_name_len = GetInt(root, "audio_name_len", audio_name_len);
  data_root = GetStr(root, "data_root", data_root);

  traditional_convert = GetBool(root, "traditional_convert",
                                traditional_convert);
  traditional_locale = GetStr(root, "traditional_locale", traditional_locale);

  hot_rule = GetBool(root, "hot_rule", hot_rule);
  rule_file = GetStr(root, "rule_file", rule_file);

  trash_punc = GetStr(root, "trash_punc", trash_punc);
  trash_punc_thresh = GetInt(root, "trash_punc_thresh", trash_punc_thresh);
  trash_punc_apps = GetStrList(root, "trash_punc_apps");

  asr.num_threads = GetInt(root, "asr.num_threads", asr.num_threads);
  asr.provider = GetStr(root, "asr.provider", asr.provider);
  asr.decoding_method =
      GetStr(root, "asr.decoding_method", asr.decoding_method);
  asr.paraformer_model =
      GetStr(root, "asr.paraformer_model", asr.paraformer_model);
  asr.tokens = GetStr(root, "asr.tokens", asr.tokens);
  asr.hotwords_file = GetStr(root, "asr.hotwords_file", asr.hotwords_file);
  asr.hotwords_score =
      static_cast<float>(GetDouble(root, "asr.hotwords_score",
                                   asr.hotwords_score));

  punct.enabled = GetBool(root, "punct.enabled", punct.enabled);
  punct.num_threads = GetInt(root, "punct.num_threads", punct.num_threads);
  punct.provider = GetStr(root, "punct.provider", punct.provider);
  punct.model = GetStr(root, "punct.model", punct.model);

  udp_broadcast = GetBool(root, "udp_broadcast", udp_broadcast);
  if (const Node* n = Find(root, "udp_broadcast_targets")) {
    for (const Node& item : n->item_maps) {
      UdpTarget target;
      target.addr = GetStr(item, "addr", "");
      target.port = GetInt(item, "port", 6017);
      if (!target.addr.empty() && target.port > 0) {
        udp_broadcast_targets.push_back(target);
      }
    }
  }

  udp_control = GetBool(root, "udp_control", udp_control);
  udp_control_addr = GetStr(root, "udp_control_addr", udp_control_addr);
  udp_control_port = GetInt(root, "udp_control_port", udp_control_port);

  model_root = GetStr(root, "model_root", model_root);
  debug = GetBool(root, "debug", debug);
  log_level = GetStr(root, "log_level", log_level);
  return true;
}

std::string Config::ResolveModelPath(const std::string& path) const {
  if (path.empty()) return path;
  auto is_absolute = [](const std::string& p) {
    return (p.size() >= 2 && p[1] == ':') || p[0] == '/' || p[0] == '\\';
  };
  if (is_absolute(path)) return path;

  std::string base = model_root.empty() ? ExeDirectory() : model_root;
  if (!is_absolute(base)) base = ExeDirectory() + "/" + base;
  if (!base.empty() && base.back() != '/' && base.back() != '\\') {
    base += '/';
  }
  const std::string joined = base + path;
  // Normalize the joined path (resolve "." / ".." and unify separators).
  // sherpa-onnx does a plain fopen() on the string, so a path containing
  // an unresolved ".." mixed with '/' and '\\' is not guaranteed to work.
  std::error_code ec;
  const std::filesystem::path norm =
      std::filesystem::weakly_canonical(joined, ec);
  return ec ? joined : norm.string();
}

}  // namespace capswriter
