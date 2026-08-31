#include "diary.h"

#include <windows.h>

#include <fstream>

#include "log.h"
#include "util.h"

namespace capswriter {

namespace {

// Same header the Python DiaryWriter puts at the top of each diary file.
const char kHeaderMd[] =
    "```txt\n"
    "正则表达式 Tip\n"
    "\n"
    "匹配到音频文件链接：\\[(.+)\\]\\((.{10,})\\)[\\s]*\n"
    "替换为 HTML 控件：<audio controls><source src=\"$2\" type=\"audio/mpeg\">"
    "$1</audio>\\n\\n\n"
    "\n"
    "匹配 HTML 控件：<audio controls><source src=\"(.+)\" type=\"audio/mpeg\">"
    "(.+)</audio>\\n\\n\n"
    "替换为文件链接：[$2]($1) \n"
    "\n"
    "\n"
    "```\n"
    "\n";

void CreateDirsRecursive(const std::wstring& path) {
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == L'\\' || path[i] == L'/') {
      const std::wstring prefix = path.substr(0, i);
      if (!prefix.empty()) CreateDirectoryW(prefix.c_str(), nullptr);
    }
  }
  CreateDirectoryW(path.c_str(), nullptr);
}

std::string ReplaceAll(std::string s, const std::string& from,
                       const std::string& to) {
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

}  // namespace

DiaryWriter::DiaryWriter(const std::string& data_root_utf8)
    : data_root_(data_root_utf8) {}

void DiaryWriter::Write(const std::string& text, std::time_t time_start,
                        const std::string& audio_path_utf8) {
  const std::string year = FormatLocalTime("%Y", time_start);
  const std::string month = FormatLocalTime("%m", time_start);
  const std::string day = FormatLocalTime("%d", time_start);
  const std::string hms = FormatLocalTime("%H:%M:%S", time_start);

  std::string dir = data_root_;
  if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += "/";
  dir += year + "/" + month;
  CreateDirsRecursive(Utf8ToWide(dir));

  const std::string md_path = dir + "/" + day + ".md";
  const std::wstring wide_md = Utf8ToWide(md_path);
  const bool exists = GetFileAttributesW(wide_md.c_str()) !=
                      INVALID_FILE_ATTRIBUTES;

  std::ofstream f(wide_md, std::ios::binary | std::ios::app);
  if (!f) {
    LOG_ERROR("Cannot open the diary file: %s", md_path.c_str());
    return;
  }
  if (!exists) f << kHeaderMd;

  // The recording lives in <dir>/assets/, the diary in <dir>/.
  std::string line = hms + " " + text;
  if (!audio_path_utf8.empty()) {
    const size_t slash = audio_path_utf8.find_last_of("/\\");
    std::string rel =
        "assets/" + (slash == std::string::npos ? audio_path_utf8
                                                : audio_path_utf8.substr(slash + 1));
    rel = ReplaceAll(rel, " ", "%20");
    line = "[" + hms + "](" + rel + ") " + text;
  }
  f << line << "\n\n";

  LOG_INFO("Diary updated: %s", md_path.c_str());
}

}  // namespace capswriter
