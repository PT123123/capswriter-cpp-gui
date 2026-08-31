#pragma once

#include <cstdint>
#include <mutex>
#include <regex>
#include <string>
#include <vector>

namespace capswriter {

// Converts simplified Chinese text to traditional Chinese. `locale` accepts
// "zh-hant" / "zh-tw" / "zh-hk" for compatibility with config_client.py;
// the conversion itself is done per-character via LCMapStringW, so it is a
// close approximation of the phrase-based zhconv used by the Python client.
std::string ConvertTraditional(const std::string& utf8,
                               const std::string& locale);

// Regex rule replacement, mirroring hot-rule.txt / RuleCorrector of the
// original project: each line is "pattern = replacement" (Python re syntax
// for the pattern, $N back references in the replacement). The rule file is
// re-read automatically when its modification time changes.
class RuleCorrector {
 public:
  // Loads (or reloads) rules from `path` (UTF-8). Returns the number of
  // valid rules; a missing file simply yields 0 rules.
  int UpdateRules(const std::string& path);

  // Number of loaded rules.
  int rule_count() const;

  // Reloads the rule file when it changed on disk, then applies every rule
  // in order (mirrors RuleCorrector.substitute).
  std::string Substitute(const std::string& text);

 private:
  void ReloadIfChanged();

  mutable std::mutex mutex_;
  std::vector<std::pair<std::regex, std::string>> rules_;
  std::string path_;
  uint64_t mtime_ = 0;
};

}  // namespace capswriter
