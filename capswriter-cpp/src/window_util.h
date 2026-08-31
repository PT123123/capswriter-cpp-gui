#pragma once

#include <string>

namespace capswriter {

// Lower-cased executable name of the foreground window, e.g. "weixin.exe".
// Returns an empty string on failure.
std::string GetForegroundProcessName();

// Semantic unit count, mirroring count_semantic_units() in
// core/client/output/text_output.py: one CJK char = 1, one run of ASCII
// letters = 1, one run of digits = 1.
int CountSemanticUnits(const std::string& utf8);

// Removes the last character of `text` when it is contained in the UTF-8
// character set `punc_set` (e.g. "，。,.").
std::string StripTrailingPunc(const std::string& text,
                              const std::string& punc_set);

}  // namespace capswriter
