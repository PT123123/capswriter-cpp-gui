#include "audio_recorder.h"

#include <windows.h>

// Note: WASAPI GUIDs (IID_IAudioClient, CLSID_MMDeviceEnumerator, ...) are
// referenced via __uuidof(), which the compiler inlines from the MIDL
// interface attributes. The 10.0.26100 Windows SDK's Uuid.Lib no longer
// ships them, so external IID_/CLSID_ symbols must be avoided.
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "resample.h"

namespace capswriter {

namespace {

constexpr REFERENCE_TIME kBufferDuration = 200 * 10000;  // 200 ms in 100 ns

struct ComGuard {
  bool initialized = false;
  ComGuard() {
    initialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  }
  ~ComGuard() {
    if (initialized) CoUninitialize();
  }
};

struct PtrReleaser {
  template <typename T>
  void operator()(T* p) const {
    if (p) p->Release();
  }
};

template <typename T>
using ComPtr = std::unique_ptr<T, PtrReleaser>;

bool IsFloatFormat(const WAVEFORMATEX* fmt) {
  if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
  if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
      fmt->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
    return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
  }
  return false;
}

bool IsPcm16Format(const WAVEFORMATEX* fmt) {
  if (fmt->wFormatTag == WAVE_FORMAT_PCM) return fmt->wBitsPerSample == 16;
  if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
      fmt->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
    return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) &&
           fmt->wBitsPerSample == 16;
  }
  return false;
}

}  // namespace

WasapiRecorder::WasapiRecorder() = default;

WasapiRecorder::~WasapiRecorder() {
  if (recording_.load()) Stop();
}

bool WasapiRecorder::Start(std::string* err) {
  if (recording_.load()) {
    if (err) *err = "recorder is already running";
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer_.clear();
    device_sample_rate_ = 0;
  }
  stop_requested_.store(false);
  {
    std::lock_guard<std::mutex> lock(start_mutex_);
    start_done_ = false;
    start_ok_ = false;
    start_error_.clear();
  }

  thread_ = std::thread(&WasapiRecorder::CaptureThread, this);

  std::unique_lock<std::mutex> lock(start_mutex_);
  start_cv_.wait(lock, [this] { return start_done_; });
  if (!start_ok_) {
    if (thread_.joinable()) thread_.join();
    if (err) *err = start_error_;
    return false;
  }
  return true;
}

std::vector<float> WasapiRecorder::Stop() {
  if (!recording_.load()) return {};

  stop_requested_.store(true);
  if (thread_.joinable()) thread_.join();

  std::vector<float> mono;
  int device_rate = 0;
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    mono = std::move(buffer_);
    buffer_.clear();
    device_rate = device_sample_rate_;
  }
  if (device_rate <= 0) return {};
  return ResampleLinear(mono, device_rate, target_sample_rate_);
}

void WasapiRecorder::CaptureThread() {
  ComGuard com;

  auto fail = [this](std::string msg) {
    {
      std::lock_guard<std::mutex> lock(start_mutex_);
      start_error_ = std::move(msg);
      start_ok_ = false;
      start_done_ = true;
    }
    start_cv_.notify_all();
    recording_.store(false);
  };

  ComPtr<IMMDeviceEnumerator> enumerator;
  {
    IMMDeviceEnumerator* raw = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&raw));
    if (FAILED(hr) || !raw) {
      fail("failed to create MMDeviceEnumerator");
      return;
    }
    enumerator.reset(raw);
  }

  ComPtr<IMMDevice> device;
  {
    IMMDevice* raw = nullptr;
    HRESULT hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &raw);
    if (FAILED(hr) || !raw) {
      fail("no default capture device found");
      return;
    }
    device.reset(raw);
  }

  ComPtr<IAudioClient> client;
  {
    IAudioClient* raw = nullptr;
    HRESULT hr =
        device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                         reinterpret_cast<void**>(&raw));
    if (FAILED(hr) || !raw) {
      fail("failed to activate IAudioClient");
      return;
    }
    client.reset(raw);
  }

  WAVEFORMATEX* mix_format = nullptr;
  if (FAILED(client->GetMixFormat(&mix_format)) || !mix_format) {
    fail("failed to get capture mix format");
    return;
  }

  const int channels = mix_format->nChannels;
  const int device_rate = mix_format->nSamplesPerSec;
  const bool float_format = IsFloatFormat(mix_format);
  const bool pcm16_format = IsPcm16Format(mix_format);
  if (channels <= 0 || device_rate <= 0 || (!float_format && !pcm16_format)) {
    CoTaskMemFree(mix_format);
    fail("unsupported capture format (need float32 or pcm16)");
    return;
  }

  HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                  AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                  kBufferDuration, 0, mix_format, nullptr);
  if (FAILED(hr)) {
    CoTaskMemFree(mix_format);
    fail("IAudioClient::Initialize failed");
    return;
  }
  CoTaskMemFree(mix_format);

  HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!event) {
    fail("CreateEvent failed");
    return;
  }
  client->SetEventHandle(event);

  ComPtr<IAudioCaptureClient> capture;
  {
    IAudioCaptureClient* raw = nullptr;
    if (FAILED(client->GetService(IID_PPV_ARGS(&raw))) || !raw) {
      CloseHandle(event);
      fail("failed to get IAudioCaptureClient");
      return;
    }
    capture.reset(raw);
  }

  if (FAILED(client->Start())) {
    CloseHandle(event);
    fail("IAudioClient::Start failed");
    return;
  }

  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    device_sample_rate_ = device_rate;
    buffer_.reserve(device_rate * 30);  // ~30 s headroom
  }
  recording_.store(true);
  {
    std::lock_guard<std::mutex> lock(start_mutex_);
    start_ok_ = true;
    start_done_ = true;
  }
  start_cv_.notify_all();

  while (!stop_requested_.load()) {
    DWORD wait = WaitForSingleObject(event, 200);
    if (wait != WAIT_OBJECT_0) continue;

    UINT32 packet_size = 0;
    while (SUCCEEDED(capture->GetNextPacketSize(&packet_size)) &&
           packet_size > 0) {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr,
                                    nullptr))) {
        break;
      }
      if (frames > 0 && !(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        const size_t old_size = buffer_.size();
        buffer_.resize(old_size + frames);
        float* dst = buffer_.data() + old_size;
        if (float_format) {
          const float* src = reinterpret_cast<const float*>(data);
          for (UINT32 f = 0; f < frames; ++f) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c) sum += src[f * channels + c];
            dst[f] = sum / channels;
          }
        } else {  // pcm16
          const int16_t* src = reinterpret_cast<const int16_t*>(data);
          for (UINT32 f = 0; f < frames; ++f) {
            float sum = 0.0f;
            for (int c = 0; c < channels; ++c)
              sum += src[f * channels + c] / 32768.0f;
            dst[f] = sum / channels;
          }
        }
      }
      capture->ReleaseBuffer(frames);
    }
  }

  client->Stop();
  CloseHandle(event);
  recording_.store(false);
}

}  // namespace capswriter
