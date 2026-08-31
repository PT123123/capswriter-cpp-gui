#include "text_transform.h"

#include <windows.h>
#include <winnls.h>

#include <cctype>
#include <fstream>
#include <sstream>

#include "log.h"
#include "util.h"

namespace capswriter {

namespace {

std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// File modification time in 100 ns units since the epoch; 0 when absent.
uint64_t FileMtime(const std::string& utf8_path) {
  WIN32_FILE_ATTRIBUTE_DATA info{};
  const std::wstring wide = Utf8ToWide(utf8_path);
  if (!GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &info)) {
    return 0;
  }
  ULARGE_INTEGER li{};
  li.LowPart = info.ftLastWriteTime.dwLowDateTime;
  li.HighPart = info.ftLastWriteTime.dwHighDateTime;
  return li.QuadPart;
}

}  // namespace

std::string ConvertTraditional(const std::string& utf8,
                               const std::string& locale) {
  (void)locale;  // zh-hant / zh-tw / zh-hk all map to the same per-char map
  if (utf8.empty()) return utf8;

  // zh-CN locale (0x0804) + LCMAP_TRADITIONAL_CHINESE maps simplified
  // characters to traditional ones. LOCALE_CHINESE_SIMPLIFIED was removed
  // from modern Windows SDK headers, so build the LCID from parts.
  const LCID kZhCn = MAKELCID(
      MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED), SORT_CHINESE_PRC);

  const std::wstring wide = Utf8ToWide(utf8);
  const int n = LCMapStringW(kZhCn, LCMAP_TRADITIONAL_CHINESE, wide.c_str(),
                             static_cast<int>(wide.size()), nullptr, 0);
  if (n <= 0) return utf8;
  std::wstring mapped(n, L'\0');
  if (LCMapStringW(kZhCn, LCMAP_TRADITIONAL_CHINESE, wide.c_str(),
                   static_cast<int>(wide.size()), mapped.data(), n) <= 0) {
    return utf8;
  }
  return WideToUtf8(mapped);
}

// Parses "pattern = replacement" lines; invalid regexes are skipped.
// `source` is the rule file path, used only to make the warning actionable.
std::vector<std::pair<std::regex, std::string>> ParseRules(
    const std::string& content, const std::string& source = std::string()) {
  std::vector<std::pair<std::regex, std::string>> rules;
  std::istringstream stream(content);
  std::string line;
  int line_no = 0;
  while (std::getline(stream, line)) {
    ++line_no;
    // Same convention as the Python client: skip blanks/comments, split
    // on " = ", turn a literal \s in the replacement into a space.
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;
    const size_t sep = line.find(" = ");
    if (sep == std::string::npos) continue;
    const std::string pattern = Trim(line.substr(0, sep));
    std::string replacement = Trim(line.substr(sep + 3));
    size_t pos = 0;
    while ((pos = replacement.find("\\s", pos)) != std::string::npos) {
      replacement.replace(pos, 2, " ");
      pos += 1;
    }
    try {
      rules.emplace_back(std::regex(pattern, std::regex::ECMAScript),
                         replacement);
    } catch (const std::regex_error& e) {
      // std::regex(ECMAScript) 不支持后行断言 (?<=...) 等写法,常见导致整条
      // 规则被跳过却毫无提示。这里给出行号、出错文件与可读的排查方向。
      const std::string where =
          source.empty() ? std::string()
                         : ("（规则文件: " + source + "）");
      LOG_WARN(
          "替换规则第 %d 行正则无效,已跳过：「%s」(std::regex 报错: %s)。"
          "注意 std::regex 不支持后行断言 (?<=...),请改用前瞻 (?=...) 或改写;"
          "该行本次不会生效。%s",
          line_no, pattern.c_str(), e.what(), where.c_str());
    }
  }
  return rules;
}

int RuleCorrector::UpdateRules(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  path_ = path;

  std::string content;
  std::vector<std::pair<std::regex, std::string>> rules;
  if (ReadFileBytes(path, &content)) {
    rules = ParseRules(content, path);
  } else {
    LOG_INFO("Rule file not found (no rules loaded): %s", path.c_str());
  }

  mtime_ = FileMtime(path);
  rules_ = std::move(rules);
  return static_cast<int>(rules_.size());
}

int RuleCorrector::rule_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(rules_.size());
}

void RuleCorrector::ReloadIfChanged() {
  // Caller holds mutex_; the parsing work is inlined via ParseRules rather
  // than calling UpdateRules because mutex_ is non-recursive.
  if (path_.empty()) return;
  const uint64_t mtime = FileMtime(path_);
  if (mtime == 0 || mtime == mtime_) return;

  LOG_INFO("Rule file changed, reloading: %s", path_.c_str());
  std::string content;
  if (!ReadFileBytes(path_, &content)) return;
  rules_ = ParseRules(content, path_);
  mtime_ = mtime;
  LOG_INFO("Reloaded %d rules", static_cast<int>(rules_.size()));
}

std::string RuleCorrector::Substitute(const std::string& text) {
  if (text.empty()) return text;
  std::lock_guard<std::mutex> lock(mutex_);
  ReloadIfChanged();
  if (rules_.empty()) return text;

  std::string result = text;
  for (const auto& rule : rules_) {
    try {
      result = std::regex_replace(result, rule.first, rule.second);
    } catch (const std::regex_error&) {
      // Ignore invalid rules at match time, like the Python version.
    }
  }
  return result;
}

}  // namespace capswriter
