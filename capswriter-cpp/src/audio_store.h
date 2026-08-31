#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace capswriter {

// Saves dictation recordings as 16-bit PCM wav files under
// <data_root>/YYYY/MM/assets/ and renames them with the recognized text,
// mirroring AudioFileManager of the original project (which writes 48 kHz
// MP3/WAV; here the 16 kHz mono resampled stream used for recognition is
// stored, in wav form, to avoid an FFmpeg dependency).
class AudioStore {
 public:
  explicit AudioStore(const std::string& data_root_utf8);

  // Writes `samples` (mono float32 at `sample_rate`) to
  // YYYY/MM/assets/(YYYYMMDD-HHMMSS).wav. Returns the file path or "".
  std::string Save(const float* samples, size_t n, int sample_rate,
                   std::time_t time_start);

  // Renames the saved file to (YYYYMMDD-HHMMSS)<text prefix><suffix>.wav;
  // mirrors AudioFileManager.rename. Returns the final path (unchanged on
  // failure).
  std::string Rename(const std::string& saved_path, const std::string& text,
                     std::time_t time_start, int name_len);

 private:
  std::string data_root_;
};

}  // namespace capswriter
