#pragma once

#include <ctime>
#include <string>

namespace capswriter {

// UTF-8 <-> UTF-16 conversions (Windows).
std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);

// Local time formatted like strftime("%Y", ...); uses `t` when non-zero,
// otherwise the current time.
std::string FormatLocalTime(const std::string& format,
                            std::time_t t = 0);

// Reads a whole file as bytes; false when it cannot be opened.
bool ReadFileBytes(const std::string& utf8_path, std::string* out);

}  // namespace capswriter
