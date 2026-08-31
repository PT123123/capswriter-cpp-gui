#pragma once

#include <ctime>
#include <string>

namespace capswriter {

// Appends recognized texts to daily markdown files under
// <data_root>/YYYY/MM/DD.md, mirroring DiaryWriter of the original project
// (same header template and entry format, so the diary stays interchangeable
// with the Python client).
class DiaryWriter {
 public:
  explicit DiaryWriter(const std::string& data_root_utf8);

  // Appends "HH:MM:SS text" (or "[HH:MM:SS](audio_rel_path) text" when an
  // audio file is given) to the diary of `time_start`'s day.
  void Write(const std::string& text, std::time_t time_start,
             const std::string& audio_path_utf8);

 private:
  std::string data_root_;
};

}  // namespace capswriter
