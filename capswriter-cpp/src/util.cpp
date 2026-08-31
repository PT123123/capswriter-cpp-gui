#include "util.h"

#include <windows.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

namespace capswriter {

std::wstring Utf8ToWide(const std::string& utf8) {
  if (utf8.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                              static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring w(n, L'\0');
  if (n > 0) {
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                        static_cast<int>(utf8.size()), w.data(), n);
  }
  return w;
}

std::string WideToUtf8(const std::wstring& wide) {
  if (wide.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                              static_cast<int>(wide.size()), nullptr, 0,
                              nullptr, nullptr);
  std::string s(n, '\0');
  if (n > 0) {
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                        static_cast<int>(wide.size()), s.data(), n, nullptr,
                        nullptr);
  }
  return s;
}

std::string FormatLocalTime(const std::string& format, std::time_t t) {
  if (t == 0) {
    t = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
  }
  std::tm tm_buf{};
#ifdef _WIN32
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  char buf[64];
  std::strftime(buf, sizeof(buf), format.c_str(), &tm_buf);
  return buf;
}

bool ReadFileBytes(const std::string& utf8_path, std::string* out) {
  std::ifstream in(Utf8ToWide(utf8_path), std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  *out = ss.str();
  return true;
}

}  // namespace capswriter
