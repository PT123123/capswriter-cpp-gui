#pragma once

#include <cstddef>
#include <vector>

namespace capswriter {

// Linear-interpolation resampler. Quality is sufficient for feeding 16 kHz
// speech into the ASR engine; it is not meant for music.
inline std::vector<float> ResampleLinear(const std::vector<float>& in,
                                         int in_rate, int out_rate) {
  if (in.empty() || in_rate <= 0 || out_rate <= 0) return {};
  if (in_rate == out_rate) return in;

  const double ratio = static_cast<double>(in_rate) / out_rate;
  const std::size_t out_len =
      static_cast<std::size_t>((in.size() - 1) / ratio) + 1;

  std::vector<float> out(out_len);
  for (std::size_t i = 0; i < out_len; ++i) {
    const double pos = i * ratio;
    const std::size_t idx = static_cast<std::size_t>(pos);
    const double frac = pos - idx;
    const float a = in[idx];
    const float b = (idx + 1 < in.size()) ? in[idx + 1] : a;
    out[i] = static_cast<float>(a + (b - a) * frac);
  }
  return out;
}

}  // namespace capswriter
