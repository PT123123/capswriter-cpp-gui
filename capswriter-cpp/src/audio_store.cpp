#include "audio_store.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <vector>

#include "log.h"
#include "util.h"

namespace capswriter {

namespace {

// Creates every missing directory of a UTF-8 path.
void CreateDirsRecursive(const std::wstring& path) {
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == L'\\' || path[i] == L'/') {
      const std::wstring prefix = path.substr(0, i);
      if (!prefix.empty()) CreateDirectoryW(prefix.c_str(), nullptr);
    }
  }
  CreateDirectoryW(path.c_str(), nullptr);
}

void WriteU16(std::ofstream& f, uint16_t v) {
  const char b[2] = {static_cast<char>(v & 0xFF),
                     static_cast<char>((v >> 8) & 0xFF)};
  f.write(b, 2);
}

void WriteU32(std::ofstream& f, uint32_t v) {
  const char b[4] = {static_cast<char>(v & 0xFF),
                     static_cast<char>((v >> 8) & 0xFF),
                     static_cast<char>((v >> 16) & 0xFF),
                     static_cast<char>((v >> 24) & 0xFF)};
  f.write(b, 4);
}

// Replaces characters that are illegal in Windows file names.
std::string SanitizeName(const std::string& text) {
  std::string out = text;
  for (char& c : out) {
    if (c == '\\' || c == '/' || c == ':' || c == '"' || c == '*' ||
        c == '?' || c == '<' || c == '>' || c == '|') {
      c = ' ';
    }
  }
  return out;
}

// Keeps at most `max_units` code points of a UTF-8 string.
std::string TruncateUtf8(const std::string& text, int max_units) {
  if (max_units <= 0) return "";
  int units = 0;
  size_t i = 0;
  while (i < text.size()) {
    if (units == max_units) return text.substr(0, i);
    const unsigned char c = static_cast<unsigned char>(text[i]);
    size_t len = 1;
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    i += len;
    ++units;
  }
  return text;
}

}  // namespace

AudioStore::AudioStore(const std::string& data_root_utf8)
    : data_root_(data_root_utf8) {}

std::string AudioStore::Save(const float* samples, size_t n, int sample_rate,
                             std::time_t time_start) {
  if (!samples || n == 0 || sample_rate <= 0) return "";

  const std::string year = FormatLocalTime("%Y", time_start);
  const std::string month = FormatLocalTime("%m", time_start);
  const std::string stamp = FormatLocalTime("%Y%m%d-%H%M%S", time_start);

  std::string dir = data_root_;
  if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += "/";
  dir += year + "/" + month + "/assets";
  const std::string path = dir + "/(" + stamp + ").wav";

  CreateDirsRecursive(Utf8ToWide(dir));

  std::ofstream f(Utf8ToWide(path), std::ios::binary | std::ios::trunc);
  if (!f) {
    LOG_ERROR("Cannot create the recording file: %s", path.c_str());
    return "";
  }

  // 44-byte canonical wav header: mono 16-bit PCM.
  const uint32_t data_bytes = static_cast<uint32_t>(n * 2);
  f.write("RIFF", 4);
  WriteU32(f, 36 + data_bytes);
  f.write("WAVE", 4);
  f.write("fmt ", 4);
  WriteU32(f, 16);
  WriteU16(f, 1);  // PCM
  WriteU16(f, 1);  // mono
  WriteU32(f, static_cast<uint32_t>(sample_rate));
  WriteU32(f, static_cast<uint32_t>(sample_rate) * 2);  // byte rate
  WriteU16(f, 2);  // block align
  WriteU16(f, 16);  // bits per sample
  f.write("data", 4);
  WriteU32(f, data_bytes);

  std::vector<int16_t> pcm(n);
  for (size_t i = 0; i < n; ++i) {
    float v = samples[i];
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    pcm[i] = static_cast<int16_t>(v * 32767.0f);
  }
  f.write(reinterpret_cast<const char*>(pcm.data()), data_bytes);

  LOG_INFO("Recording saved: %s", path.c_str());
  return path;
}

std::string AudioStore::Rename(const std::string& saved_path,
                               const std::string& text, std::time_t time_start,
                               int name_len) {
  if (saved_path.empty()) return "";
  if (GetFileAttributesW(Utf8ToWide(saved_path).c_str()) ==
      INVALID_FILE_ATTRIBUTES) {
    LOG_WARN("Recording file missing, not renamed: %s", saved_path.c_str());
    return saved_path;
  }

  const std::string stamp = FormatLocalTime("%Y%m%d-%H%M%S", time_start);
  const std::string prefix =
      SanitizeName(TruncateUtf8(text, name_len));

  const size_t slash = saved_path.find_last_of("/\\");
  const std::string dir =
      slash == std::string::npos ? "" : saved_path.substr(0, slash);
  const std::string new_path = (dir.empty() ? "" : dir + "/") + "(" + stamp +
                               ")" + prefix + ".wav";

  if (MoveFileW(Utf8ToWide(saved_path).c_str(),
                Utf8ToWide(new_path).c_str())) {
    LOG_INFO("Recording renamed: %s", new_path.c_str());
    return new_path;
  }
  LOG_WARN("Failed to rename the recording (error %lu)", GetLastError());
  return saved_path;
}

}  // namespace capswriter
