#include "lumaforge/audio/AudioOutput.h"

#include "lumaforge/core/Log.h"
#include "lumaforge/core/StringUtil.h"

#include <windows.h>
#include <initguid.h>   // define the GUIDs/PROPERTYKEYs used below in this TU
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>   // needs propkeydef.h from mmdeviceapi.h first
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <wrl/client.h>

#include <cstring>
#include <vector>

namespace lf::audio {

namespace {
constexpr const char* kLog = "audio";
using Microsoft::WRL::ComPtr;

Error comError(const char* what, HRESULT hr) { return Error{"The audio device could not be opened.", std::string(what) + ": " + hresultToString(hr), hr}; }
}  // namespace

struct AudioOutput::Impl {
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> renderClient;
    HANDLE event = nullptr;
    bool comInitialised = false;

    ~Impl() {
        renderClient.Reset();
        client.Reset();
        device.Reset();
        if (event) CloseHandle(event);
    }
};

AudioOutput::~AudioOutput() {
    stop();
    m_impl.reset();
}

Result<std::unique_ptr<AudioOutput>> AudioOutput::create(const Options& options) {
    std::unique_ptr<AudioOutput> out(new AudioOutput());
    out->m_impl = std::make_unique<Impl>();
    auto& i = *out->m_impl;
    out->m_sampleRate = options.sampleRate;
    out->m_channels = options.channels;

    // COM may already be initialised on this thread (Qt does); either result is fine.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) hr = S_OK;
    if (FAILED(hr)) return comError("CoInitializeEx", hr);

    ComPtr<IMMDeviceEnumerator> enumerator;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return comError("CoCreateInstance(MMDeviceEnumerator)", hr);
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &i.device);
    if (FAILED(hr)) return Error{"No audio output device is available.", "GetDefaultAudioEndpoint: " + hresultToString(hr), hr};

    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(i.device->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT name;
        PropVariantInit(&name);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) out->m_deviceName = wideToUtf8(name.pwszVal);
        PropVariantClear(&name);
    }

    hr = i.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &i.client);
    if (FAILED(hr)) return comError("IMMDevice::Activate", hr);

    // Ask for our mixer format and let WASAPI convert to the device's; this
    // keeps the whole engine at one rate regardless of the sound card.
    WAVEFORMATEXTENSIBLE fmt{};
    fmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    fmt.Format.nChannels = static_cast<WORD>(options.channels);
    fmt.Format.nSamplesPerSec = static_cast<DWORD>(options.sampleRate);
    fmt.Format.wBitsPerSample = 32;
    fmt.Format.nBlockAlign = static_cast<WORD>(options.channels * 4);
    fmt.Format.nAvgBytesPerSec = fmt.Format.nSamplesPerSec * fmt.Format.nBlockAlign;
    fmt.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    fmt.Samples.wValidBitsPerSample = 32;
    fmt.dwChannelMask = options.channels == 1 ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    fmt.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    const REFERENCE_TIME bufferDuration = static_cast<REFERENCE_TIME>(options.bufferMilliseconds) * 10'000;
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = i.client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, bufferDuration, 0, &fmt.Format, nullptr);
    if (FAILED(hr)) return comError("IAudioClient::Initialize", hr);

    hr = i.client->GetBufferSize(&out->m_bufferFrames);
    if (FAILED(hr)) return comError("GetBufferSize", hr);
    i.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!i.event) return Error{"Could not create the audio event.", "CreateEvent failed"};
    hr = i.client->SetEventHandle(i.event);
    if (FAILED(hr)) return comError("SetEventHandle", hr);
    hr = i.client->GetService(IID_PPV_ARGS(&i.renderClient));
    if (FAILED(hr)) return comError("GetService(IAudioRenderClient)", hr);

    LF_LOG_INFO(kLog, "audio output: {} | {} Hz, {} ch, buffer {} frames ({} ms)", out->m_deviceName, out->m_sampleRate, out->m_channels,
                out->m_bufferFrames, out->m_bufferFrames * 1000 / out->m_sampleRate);
    return out;
}

Status AudioOutput::start(RenderCallback render) {
    if (m_running.load()) return Status::success();
    if (!render) return Error{"Audio render callback is required."};
    m_render = std::move(render);
    m_stopRequested = false;
    auto& i = *m_impl;

    // Pre-fill with silence so the first event has data to play.
    BYTE* data = nullptr;
    if (SUCCEEDED(i.renderClient->GetBuffer(m_bufferFrames, &data))) i.renderClient->ReleaseBuffer(m_bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
    HRESULT hr = i.client->Start();
    if (FAILED(hr)) return comError("IAudioClient::Start", hr);
    m_running = true;
    m_thread = std::thread([this] { renderThread(); });
    return Status::success();
}

void AudioOutput::stop() {
    if (!m_running.load()) return;
    m_stopRequested = true;
    if (m_impl && m_impl->event) SetEvent(m_impl->event);
    if (m_thread.joinable()) m_thread.join();
    if (m_impl && m_impl->client) {
        m_impl->client->Stop();
        m_impl->client->Reset();
    }
    m_running = false;
}

void AudioOutput::renderThread() {
    auto& i = *m_impl;
    // Pro Audio scheduling category: keeps the callback from being starved by the UI or decoders.
    DWORD taskIndex = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    std::vector<float> scratch;

    while (!m_stopRequested.load()) {
        const DWORD wait = WaitForSingleObject(i.event, 500);
        if (m_stopRequested.load()) break;
        if (wait != WAIT_OBJECT_0) continue;

        UINT32 padding = 0;
        if (FAILED(i.client->GetCurrentPadding(&padding))) continue;
        const UINT32 frames = m_bufferFrames > padding ? m_bufferFrames - padding : 0;
        if (frames == 0) continue;

        BYTE* data = nullptr;
        if (FAILED(i.renderClient->GetBuffer(frames, &data))) continue;
        m_render(reinterpret_cast<float*>(data), static_cast<int>(frames));
        i.renderClient->ReleaseBuffer(frames, 0);
    }
    if (task) AvRevertMmThreadCharacteristics(task);
}

}  // namespace lf::audio
