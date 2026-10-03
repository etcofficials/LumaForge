#include "lumaforge/audio/AudioRecorder.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace lf::audio {

namespace {
constexpr const char* kLog = "audio";
using Microsoft::WRL::ComPtr;

Error comError(const char* what, HRESULT hr) { return Error{"The microphone could not be opened.", std::string(what) + ": " + hresultToString(hr), hr}; }

void writeLe32(std::FILE* f, std::uint32_t v) { std::fwrite(&v, 4, 1, f); }
void writeLe16(std::FILE* f, std::uint16_t v) { std::fwrite(&v, 2, 1, f); }

void writeWavHeader(std::FILE* f, std::uint32_t dataBytes) {
    const int rate = AudioRecorder::kRate, channels = AudioRecorder::kChannels;
    std::fseek(f, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, f);
    writeLe32(f, 36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    writeLe32(f, 16);
    writeLe16(f, 1);   // PCM
    writeLe16(f, static_cast<std::uint16_t>(channels));
    writeLe32(f, static_cast<std::uint32_t>(rate));
    writeLe32(f, static_cast<std::uint32_t>(rate * channels * 2));
    writeLe16(f, static_cast<std::uint16_t>(channels * 2));
    writeLe16(f, 16);
    std::fwrite("data", 1, 4, f);
    writeLe32(f, dataBytes);
}
}  // namespace

struct AudioRecorder::Impl {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    HANDLE event = nullptr;
    std::FILE* file = nullptr;
    std::uint32_t dataBytes = 0;
    ~Impl() {
        capture.Reset();
        client.Reset();
        device.Reset();
        if (event) CloseHandle(event);
        if (file) std::fclose(file);
    }
};

AudioRecorder::~AudioRecorder() {
    if (m_running.load()) (void)stop();
}

Result<std::unique_ptr<AudioRecorder>> AudioRecorder::create() {
    std::unique_ptr<AudioRecorder> r(new AudioRecorder());
    r->m_impl = std::make_unique<Impl>();
    auto& i = *r->m_impl;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) hr = S_OK;
    if (FAILED(hr)) return comError("CoInitializeEx", hr);
    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return comError("CoCreateInstance(MMDeviceEnumerator)", hr);
    hr = enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &i.device);
    if (FAILED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &i.device);
    if (FAILED(hr)) return Error{"No microphone is connected (no default recording device).", "GetDefaultAudioEndpoint(eCapture): " + hresultToString(hr), hr};
    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(i.device->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT name;
        PropVariantInit(&name);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) r->m_deviceName = wideToUtf8(name.pwszVal);
        PropVariantClear(&name);
    }
    hr = i.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &i.client);
    if (FAILED(hr)) return comError("IMMDevice::Activate", hr);

    WAVEFORMATEXTENSIBLE fmt{};
    fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    fmt.Format.nChannels = kChannels;
    fmt.Format.nSamplesPerSec = kRate;
    fmt.Format.wBitsPerSample = 32;
    fmt.Format.nBlockAlign = kChannels * 4;
    fmt.Format.nAvgBytesPerSec = kRate * fmt.Format.nBlockAlign;
    fmt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    fmt.Samples.wValidBitsPerSample = 32;
    fmt.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    fmt.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = i.client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200 * 10'000, 0, &fmt.Format, nullptr);   // 200 ms buffer
    if (FAILED(hr)) return comError("IAudioClient::Initialize", hr);
    i.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!i.event) return Error{"Could not create the audio event."};
    if (FAILED(hr = i.client->SetEventHandle(i.event))) return comError("SetEventHandle", hr);
    if (FAILED(hr = i.client->GetService(IID_PPV_ARGS(&i.capture)))) return comError("GetService(IAudioCaptureClient)", hr);
    LF_LOG_INFO(kLog, "microphone: {}", r->m_deviceName);
    return r;
}

Status AudioRecorder::start(const std::string& path) {
    if (m_running.load()) return Status::success();
    auto& i = *m_impl;
    if (_wfopen_s(&i.file, utf8ToWide(path).c_str(), L"wb") != 0 || !i.file) return Error{"The recording file could not be created.", path};
    i.dataBytes = 0;
    writeWavHeader(i.file, 0);
    m_frames = 0;
    m_stopRequested = false;
    HRESULT hr = i.client->Start();
    if (FAILED(hr)) return comError("IAudioClient::Start", hr);
    m_running = true;
    m_thread = std::thread([this] { captureThread(); });
    LF_LOG_INFO(kLog, "recording to {}", path);
    return Status::success();
}

Result<Duration> AudioRecorder::stop() {
    if (!m_running.load()) return Error{"Not recording."};
    m_stopRequested = true;
    SetEvent(m_impl->event);
    if (m_thread.joinable()) m_thread.join();
    m_impl->client->Stop();
    m_impl->client->Reset();
    m_running = false;
    auto& i = *m_impl;
    if (!i.file) return Error{"The recording file was lost."};
    writeWavHeader(i.file, i.dataBytes);
    std::fclose(i.file);
    i.file = nullptr;
    if (!m_error.empty()) return Error{"Recording failed.", m_error};
    LF_LOG_INFO(kLog, "recording stopped: {:.2f} s", recorded().toSeconds());
    return recorded();
}

void AudioRecorder::captureThread() {
    auto& i = *m_impl;
    DWORD taskIndex = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    std::vector<std::int16_t> pcm;
    while (!m_stopRequested.load()) {
        if (WaitForSingleObject(i.event, 500) != WAIT_OBJECT_0) continue;
        UINT32 packet = 0;
        while (SUCCEEDED(i.capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(i.capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            pcm.assign(static_cast<std::size_t>(frames) * kChannels, 0);
            float peak = 0.0f;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                const float* f = reinterpret_cast<const float*>(data);
                for (std::size_t s = 0; s < pcm.size(); ++s) {
                    const float v = std::clamp(f[s], -1.0f, 1.0f);
                    peak = std::max(peak, std::fabs(v));
                    pcm[s] = static_cast<std::int16_t>(std::lround(v * 32767.0f));
                }
            }
            i.capture->ReleaseBuffer(frames);
            if (std::fwrite(pcm.data(), sizeof(std::int16_t), pcm.size(), i.file) != pcm.size()) {
                m_error = "Writing the recording failed (is the disk full?).";
                m_stopRequested = true;
                break;
            }
            i.dataBytes += static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
            m_frames += frames;
            float prev = m_peak.load();
            while (peak > prev && !m_peak.compare_exchange_weak(prev, peak)) {}
        }
    }
    if (task) AvRevertMmThreadCharacteristics(task);
}

}  // namespace lf::audio
