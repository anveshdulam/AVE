#include "ave/wasapi_audio_device.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <functiondiscoverykeys_devpkey.h>
#endif

namespace ave {
namespace {

#ifdef _WIN32

std::string wide_to_utf8(const wchar_t* wstr) {
    if (!wstr || *wstr == L'\0') {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

std::string format_hresult(HRESULT hr, const char* context) {
    wchar_t* msg_buf = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = FormatMessageW(flags, nullptr, static_cast<DWORD>(hr),
                                     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                     reinterpret_cast<LPWSTR>(&msg_buf), 0, nullptr);
    std::string sys_msg;
    if (len > 0 && msg_buf) {
        sys_msg = wide_to_utf8(msg_buf);
        while (!sys_msg.empty() &&
               (sys_msg.back() == '\r' || sys_msg.back() == '\n' || sys_msg.back() == ' ')) {
            sys_msg.pop_back();
        }
    }
    if (msg_buf) {
        LocalFree(msg_buf);
    }

    std::ostringstream oss;
    oss << context << " (0x" << std::hex << std::uppercase
        << static_cast<unsigned long>(hr) << std::dec;
    if (!sys_msg.empty()) {
        oss << ": " << sys_msg;
    }
    oss << ")";
    return oss.str();
}

bool is_device_invalidation_hr(HRESULT hr) noexcept {
    return hr == AUDCLNT_E_DEVICE_INVALIDATED ||
           hr == AUDCLNT_E_SERVICE_NOT_RUNNING ||
           hr == AUDCLNT_E_RESOURCES_INVALIDATED;
}

WasapiEndpointFormat wave_format_to_endpoint_format(const WAVEFORMATEX* wfx) noexcept {
    WasapiEndpointFormat fmt;
    if (!wfx) {
        fmt.sample_format = AudioSampleFormat::Unknown;
        return fmt;
    }

    fmt.sample_rate = static_cast<int>(wfx->nSamplesPerSec);
    fmt.channels = static_cast<int>(wfx->nChannels);

    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && wfx->wBitsPerSample == 32) {
        fmt.sample_format = AudioSampleFormat::Float32;
        fmt.valid_bits_per_sample = 32;
        return fmt;
    }

    if (wfx->wFormatTag == WAVE_FORMAT_PCM) {
        if (wfx->wBitsPerSample == 16) {
            fmt.sample_format = AudioSampleFormat::Int16;
            fmt.valid_bits_per_sample = 16;
        } else if (wfx->wBitsPerSample == 24) {
            fmt.sample_format = AudioSampleFormat::Int24;
            fmt.valid_bits_per_sample = 24;
        } else if (wfx->wBitsPerSample == 32) {
            fmt.sample_format = AudioSampleFormat::Int32;
            fmt.valid_bits_per_sample = 32;
        } else {
            fmt.sample_format = AudioSampleFormat::Unknown;
        }
        return fmt;
    }

    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        wfx->cbSize >= (sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        fmt.is_extensible = true;
        fmt.channel_mask = ext->dwChannelMask;
        const int valid_bits = ext->Samples.wValidBitsPerSample > 0
                                   ? static_cast<int>(ext->Samples.wValidBitsPerSample)
                                   : static_cast<int>(wfx->wBitsPerSample);
        fmt.valid_bits_per_sample = valid_bits;

        if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) &&
            wfx->wBitsPerSample == 32) {
            fmt.sample_format = AudioSampleFormat::Float32;
        } else if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            if (wfx->wBitsPerSample == 16) {
                fmt.sample_format = AudioSampleFormat::Int16;
            } else if (wfx->wBitsPerSample == 24) {
                fmt.sample_format = AudioSampleFormat::Int24;
            } else if (wfx->wBitsPerSample == 32) {
                fmt.sample_format = AudioSampleFormat::Int32;
            } else {
                fmt.sample_format = AudioSampleFormat::Unknown;
            }
        } else {
            fmt.sample_format = AudioSampleFormat::Unknown;
        }
        return fmt;
    }

    fmt.sample_format = AudioSampleFormat::Unknown;
    return fmt;
}

DWORD default_channel_mask(int channels, DWORD preferred) noexcept {
    if (preferred != 0) {
        return preferred;
    }
    switch (channels) {
        case 1: return KSAUDIO_SPEAKER_MONO;
        case 2: return KSAUDIO_SPEAKER_STEREO;
        case 4: return KSAUDIO_SPEAKER_QUAD;
        case 6: return KSAUDIO_SPEAKER_5POINT1;
        case 8: return KSAUDIO_SPEAKER_7POINT1_SURROUND;
        default: {
            DWORD mask = 0;
            for (int i = 0; i < std::min(channels, 16); ++i) {
                mask |= (1u << static_cast<unsigned>(i));
            }
            return mask;
        }
    }
}

WAVEFORMATEXTENSIBLE make_wave_format_extensible(const WasapiEndpointFormat& fmt) noexcept {
    WAVEFORMATEXTENSIBLE ext{};
    int bits_per_sample = 32;
    switch (fmt.sample_format) {
        case AudioSampleFormat::Int16:   bits_per_sample = 16; break;
        case AudioSampleFormat::Int24:   bits_per_sample = 24; break;
        case AudioSampleFormat::Int32:   bits_per_sample = 32; break;
        case AudioSampleFormat::Float32:
        default:                         bits_per_sample = 32; break;
    }

    ext.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    ext.Format.nChannels = static_cast<WORD>(fmt.channels);
    ext.Format.nSamplesPerSec = static_cast<DWORD>(fmt.sample_rate);
    ext.Format.wBitsPerSample = static_cast<WORD>(bits_per_sample);
    ext.Format.nBlockAlign = static_cast<WORD>(fmt.channels * (bits_per_sample / 8));
    ext.Format.nAvgBytesPerSec = ext.Format.nSamplesPerSec * ext.Format.nBlockAlign;
    ext.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);

    ext.Samples.wValidBitsPerSample = static_cast<WORD>(
        fmt.valid_bits_per_sample > 0 ? fmt.valid_bits_per_sample : bits_per_sample);
    ext.dwChannelMask = default_channel_mask(fmt.channels, fmt.channel_mask);
    ext.SubFormat = (fmt.sample_format == AudioSampleFormat::Float32)
                        ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
                        : KSDATAFORMAT_SUBTYPE_PCM;
    return ext;
}

class RealWasapiEndpoint final : public IWasapiEndpoint {
public:
    ~RealWasapiEndpoint() override {
        close();
    }

    WasapiStatus open_default_render_device(std::string& out_device_name) override {
        close();
        out_device_name.clear();

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                      nullptr,
                                      CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator),
                                      reinterpret_cast<void**>(&enumerator_));
        if (FAILED(hr) || !enumerator_) {
            return make_error(hr, "Failed to create MMDeviceEnumerator");
        }

        hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
        if (FAILED(hr) || !device_) {
            hr = enumerator_->GetDefaultAudioEndpoint(eRender, eMultimedia, &device_);
        }
        if (FAILED(hr) || !device_) {
            close();
            return make_error(hr, "No default Windows audio output endpoint available");
        }

        IPropertyStore* props = nullptr;
        if (SUCCEEDED(device_->OpenPropertyStore(STGM_READ, &props)) && props) {
            PROPVARIANT var_name;
            PropVariantInit(&var_name);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var_name)) &&
                var_name.vt == VT_LPWSTR && var_name.pwszVal) {
                out_device_name = wide_to_utf8(var_name.pwszVal);
            }
            PropVariantClear(&var_name);
            props->Release();
        }
        if (out_device_name.empty()) {
            out_device_name = "Default Windows Audio Device";
        }

        hr = device_->Activate(__uuidof(IAudioClient),
                               CLSCTX_ALL,
                               nullptr,
                               reinterpret_cast<void**>(&audio_client_));
        if (FAILED(hr) || !audio_client_) {
            close();
            return make_error(hr, "Failed to activate WASAPI IAudioClient");
        }

        return WasapiStatus{};
    }

    WasapiStatus get_mix_format(WasapiEndpointFormat& out_format) override {
        if (!audio_client_) {
            return make_error(E_POINTER, "IAudioClient not initialized");
        }
        WAVEFORMATEX* mix = nullptr;
        const HRESULT hr = audio_client_->GetMixFormat(&mix);
        if (FAILED(hr) || !mix) {
            return make_error(hr, "IAudioClient::GetMixFormat failed");
        }
        out_format = wave_format_to_endpoint_format(mix);
        CoTaskMemFree(mix);
        return WasapiStatus{};
    }

    bool is_format_supported_shared(const WasapiEndpointFormat& desired,
                                    WasapiEndpointFormat& out_closest,
                                    bool& out_has_closest) override {
        out_has_closest = false;
        if (!audio_client_) {
            return false;
        }
        WAVEFORMATEXTENSIBLE ext = make_wave_format_extensible(desired);
        WAVEFORMATEX* closest = nullptr;
        const HRESULT hr = audio_client_->IsFormatSupported(
            AUDCLNT_SHAREMODE_SHARED, &ext.Format, &closest);
        if (hr == S_OK) {
            if (closest) {
                CoTaskMemFree(closest);
            }
            out_closest = desired;
            return true;
        }
        if (hr == S_FALSE && closest) {
            out_closest = wave_format_to_endpoint_format(closest);
            out_has_closest = (out_closest.sample_format != AudioSampleFormat::Unknown);
            CoTaskMemFree(closest);
            return false;
        }
        if (closest) {
            CoTaskMemFree(closest);
        }
        return false;
    }

    WasapiStatus initialize_client(const WasapiEndpointFormat& format,
                                   size_t requested_buffer_frames,
                                   bool prefer_event_callback,
                                   size_t& out_buffer_frames,
                                   bool& out_event_mode_active) override {
        if (!audio_client_ || !device_) {
            return make_error(E_POINTER, "WASAPI endpoint not opened");
        }

        WAVEFORMATEXTENSIBLE wf = make_wave_format_extensible(format);
        REFERENCE_TIME default_period = 100000; // 10ms
        REFERENCE_TIME min_period = 30000;
        (void)audio_client_->GetDevicePeriod(&default_period, &min_period);

        const REFERENCE_TIME req_rt = static_cast<REFERENCE_TIME>(
            (static_cast<uint64_t>(requested_buffer_frames) * 10000000ULL) /
            static_cast<uint64_t>(std::max(8000, format.sample_rate)));
        REFERENCE_TIME buf_duration = (std::max)(default_period * 2, req_rt);

        bool using_event = prefer_event_callback;
        DWORD flags = using_event ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0u;
        HRESULT hr = audio_client_->Initialize(
            AUDCLNT_SHAREMODE_SHARED, flags, buf_duration, 0, &wf.Format, nullptr);

        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            UINT32 aligned_frames = 0;
            if (SUCCEEDED(audio_client_->GetBufferSize(&aligned_frames)) && aligned_frames > 0) {
                buf_duration = static_cast<REFERENCE_TIME>(
                    std::ceil(10000000.0 * static_cast<double>(aligned_frames) /
                              static_cast<double>(format.sample_rate)));
                audio_client_->Release();
                audio_client_ = nullptr;
                hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(&audio_client_));
                if (SUCCEEDED(hr) && audio_client_) {
                    hr = audio_client_->Initialize(
                        AUDCLNT_SHAREMODE_SHARED, flags, buf_duration, 0, &wf.Format, nullptr);
                }
            }
        }

        if (FAILED(hr) && using_event) {
            // Re-activate client cleanly and retry without EVENTCALLBACK for exotic virtual drivers.
            using_event = false;
            audio_client_->Release();
            audio_client_ = nullptr;
            hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(&audio_client_));
            if (SUCCEEDED(hr) && audio_client_) {
                hr = audio_client_->Initialize(
                    AUDCLNT_SHAREMODE_SHARED, 0u, buf_duration, 0, &wf.Format, nullptr);
            }
        }

        if (FAILED(hr) || !audio_client_) {
            return make_error(hr, "IAudioClient::Initialize failed");
        }

        if (using_event) {
            event_handle_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!event_handle_) {
                return make_error(HRESULT_FROM_WIN32(GetLastError()),
                                  "Failed to create WASAPI render event handle");
            }
            hr = audio_client_->SetEventHandle(event_handle_);
            if (FAILED(hr)) {
                CloseHandle(event_handle_);
                event_handle_ = nullptr;
                return make_error(hr, "IAudioClient::SetEventHandle failed");
            }
        }

        UINT32 hw_frames = 0;
        hr = audio_client_->GetBufferSize(&hw_frames);
        if (FAILED(hr) || hw_frames == 0) {
            return make_error(hr, "IAudioClient::GetBufferSize failed");
        }

        hr = audio_client_->GetService(__uuidof(IAudioRenderClient),
                                       reinterpret_cast<void**>(&render_client_));
        if (FAILED(hr) || !render_client_) {
            return make_error(hr, "IAudioClient::GetService(IAudioRenderClient) failed");
        }

        out_buffer_frames = static_cast<size_t>(hw_frames);
        out_event_mode_active = using_event;
        return WasapiStatus{};
    }

    [[nodiscard]] void* native_wait_handle() const noexcept override {
        return static_cast<void*>(event_handle_);
    }

    WasapiStatus get_current_padding(size_t& out_padding_frames) override {
        if (!audio_client_) {
            return make_error(E_POINTER, "IAudioClient not initialized");
        }
        UINT32 padding = 0;
        const HRESULT hr = audio_client_->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            return make_error(hr, "IAudioClient::GetCurrentPadding failed");
        }
        out_padding_frames = static_cast<size_t>(padding);
        return WasapiStatus{};
    }

    WasapiStatus start() override {
        if (!audio_client_) {
            return make_error(E_POINTER, "IAudioClient not initialized");
        }
        if (!mmcss_handle_) {
            DWORD task_idx = 0;
            mmcss_handle_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_idx);
        }
        const HRESULT hr = audio_client_->Start();
        if (FAILED(hr)) {
            return make_error(hr, "IAudioClient::Start failed");
        }
        return WasapiStatus{};
    }

    WasapiStatus stop() override {
        if (!audio_client_) {
            return WasapiStatus{};
        }
        const HRESULT hr = audio_client_->Stop();
        if (mmcss_handle_) {
            AvRevertMmThreadCharacteristics(mmcss_handle_);
            mmcss_handle_ = nullptr;
        }
        if (FAILED(hr)) {
            return make_error(hr, "IAudioClient::Stop failed");
        }
        return WasapiStatus{};
    }

    WasapiStatus reset() override {
        if (!audio_client_) {
            return WasapiStatus{};
        }
        const HRESULT hr = audio_client_->Reset();
        if (FAILED(hr)) {
            return make_error(hr, "IAudioClient::Reset failed");
        }
        return WasapiStatus{};
    }

    WasapiStatus get_buffer(size_t frames_to_write, uint8_t** out_data) override {
        if (!render_client_ || !out_data) {
            return make_error(E_POINTER, "IAudioRenderClient not initialized");
        }
        BYTE* data = nullptr;
        const HRESULT hr = render_client_->GetBuffer(static_cast<UINT32>(frames_to_write), &data);
        if (FAILED(hr)) {
            return make_error(hr, "IAudioRenderClient::GetBuffer failed");
        }
        *out_data = reinterpret_cast<uint8_t*>(data);
        return WasapiStatus{};
    }

    WasapiStatus release_buffer(size_t frames_written, bool silent) override {
        if (!render_client_) {
            return make_error(E_POINTER, "IAudioRenderClient not initialized");
        }
        const DWORD flags = silent ? AUDCLNT_BUFFERFLAGS_SILENT : 0u;
        const HRESULT hr = render_client_->ReleaseBuffer(static_cast<UINT32>(frames_written), flags);
        if (FAILED(hr)) {
            return make_error(hr, "IAudioRenderClient::ReleaseBuffer failed");
        }
        return WasapiStatus{};
    }

    void close() override {
        if (audio_client_) {
            (void)audio_client_->Stop();
        }
        if (mmcss_handle_) {
            AvRevertMmThreadCharacteristics(mmcss_handle_);
            mmcss_handle_ = nullptr;
        }
        if (render_client_) {
            render_client_->Release();
            render_client_ = nullptr;
        }
        if (audio_client_) {
            audio_client_->Release();
            audio_client_ = nullptr;
        }
        if (device_) {
            device_->Release();
            device_ = nullptr;
        }
        if (enumerator_) {
            enumerator_->Release();
            enumerator_ = nullptr;
        }
        if (event_handle_) {
            CloseHandle(event_handle_);
            event_handle_ = nullptr;
        }
    }

private:
    IMMDeviceEnumerator* enumerator_{nullptr};
    IMMDevice* device_{nullptr};
    IAudioClient* audio_client_{nullptr};
    IAudioRenderClient* render_client_{nullptr};
    HANDLE event_handle_{nullptr};
    HANDLE mmcss_handle_{nullptr};

    static WasapiStatus make_error(HRESULT hr, const char* context) {
        WasapiStatus s;
        s.ok = false;
        s.hresult = static_cast<long>(hr);
        s.device_invalidated = is_device_invalidation_hr(hr);
        s.unsupported_format = (hr == AUDCLNT_E_UNSUPPORTED_FORMAT);
        s.error_message = format_hresult(hr, context);
        return s;
    }
};

#endif // _WIN32

std::unique_ptr<IWasapiEndpoint> create_default_wasapi_endpoint() {
#ifdef _WIN32
    return std::make_unique<RealWasapiEndpoint>();
#else
    return nullptr;
#endif
}

} // namespace

WasapiAudioDevice::WasapiAudioDevice()
    : endpoint_factory_([]() { return create_default_wasapi_endpoint(); }) {
#ifdef _WIN32
    control_event_ = static_cast<void*>(CreateEventW(nullptr, FALSE, FALSE, nullptr));
#endif
}

WasapiAudioDevice::WasapiAudioDevice(EndpointFactory endpoint_factory)
    : endpoint_factory_(endpoint_factory
                            ? std::move(endpoint_factory)
                            : EndpointFactory([]() { return create_default_wasapi_endpoint(); })) {
#ifdef _WIN32
    control_event_ = static_cast<void*>(CreateEventW(nullptr, FALSE, FALSE, nullptr));
#endif
}

WasapiAudioDevice::~WasapiAudioDevice() {
    shutdown();
#ifdef _WIN32
    if (control_event_) {
        CloseHandle(static_cast<HANDLE>(control_event_));
        control_event_ = nullptr;
    }
#endif
}

void WasapiAudioDevice::signal_control_event() noexcept {
#ifdef _WIN32
    if (control_event_) {
        SetEvent(static_cast<HANDLE>(control_event_));
    }
#else
    cmd_ack_cv_.notify_all();
#endif
}

void WasapiAudioDevice::ensure_worker_started() {
    if (!worker_thread_.joinable()) {
        shutting_down_.store(false, std::memory_order_release);
        worker_thread_ = std::thread(&WasapiAudioDevice::worker_loop, this);
    }
}

bool WasapiAudioDevice::send_command_and_wait(WorkerCmdType type,
                                              const AudioDeviceConfig* cfg,
                                              AudioRenderCallback cb) {
    ensure_worker_started();

    auto cmd = std::make_shared<PendingCmd>();
    cmd->type = type;
    if (cfg) {
        cmd->config = *cfg;
    }
    if (cb) {
        cmd->callback = std::move(cb);
    }

    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        cmd_queue_.push_back(cmd);
    }
    signal_control_event();

    std::unique_lock<std::mutex> lock(cmd_mutex_);
    cmd_ack_cv_.wait(lock, [&cmd]() { return cmd->done; });
    return cmd->result;
}

bool WasapiAudioDevice::initialize(const AudioDeviceConfig& config) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return false;
    }
    return send_command_and_wait(WorkerCmdType::Initialize, &config, nullptr);
}

bool WasapiAudioDevice::start(AudioRenderCallback callback) {
    if (shutting_down_.load(std::memory_order_acquire)) {
        return false;
    }
    if (state_.load(std::memory_order_acquire) == AudioDeviceState::Uninitialized) {
        AudioDeviceConfig def;
        if (!initialize(def)) {
            return false;
        }
    }
    return send_command_and_wait(WorkerCmdType::Start, nullptr, std::move(callback));
}

bool WasapiAudioDevice::pause() {
    if (!worker_thread_.joinable() ||
        state_.load(std::memory_order_acquire) != AudioDeviceState::Running) {
        return false;
    }
    return send_command_and_wait(WorkerCmdType::Pause);
}

bool WasapiAudioDevice::resume() {
    if (!worker_thread_.joinable() ||
        state_.load(std::memory_order_acquire) != AudioDeviceState::Paused) {
        return false;
    }
    return send_command_and_wait(WorkerCmdType::Resume);
}

void WasapiAudioDevice::flush() {
    if (!worker_thread_.joinable()) {
        return;
    }
    (void)send_command_and_wait(WorkerCmdType::Flush);
}

void WasapiAudioDevice::stop() {
    if (!worker_thread_.joinable()) {
        return;
    }
    (void)send_command_and_wait(WorkerCmdType::Stop);
}

void WasapiAudioDevice::shutdown() {
    if (shutting_down_.exchange(true, std::memory_order_acq_rel)) {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        return;
    }

    if (worker_thread_.joinable()) {
        (void)send_command_and_wait(WorkerCmdType::Shutdown);
        worker_thread_.join();
    }

    available_.store(false, std::memory_order_release);
    if (state_.load(std::memory_order_acquire) != AudioDeviceState::Error) {
        state_.store(AudioDeviceState::Uninitialized, std::memory_order_release);
    }
}

size_t WasapiAudioDevice::submit_pcm(const float* interleaved_frames, size_t frame_count) {
    const AudioDeviceState cur = state_.load(std::memory_order_acquire);
    if (!interleaved_frames || frame_count == 0 ||
        !available_.load(std::memory_order_acquire) ||
        cur == AudioDeviceState::Uninitialized ||
        cur == AudioDeviceState::Error ||
        !queued_pcm_) {
        return 0;
    }

    int ch = 2;
    {
        std::lock_guard<std::mutex> lock(diag_mutex_);
        ch = config_.channels > 0 ? config_.channels : 2;
    }

    const size_t writable = queued_pcm_->available_write_frames();
    const size_t accepted = std::min(frame_count, writable);
    if (accepted == 0) {
        return 0;
    }

    const size_t max_scratch = sanitize_scratch_.size() / static_cast<size_t>(ch);
    const size_t bounded = std::min(accepted, max_scratch);
    if (bounded == 0) {
        return 0;
    }

    AudioFormatConverter::sanitize_floats(
        interleaved_frames, sanitize_scratch_.data(), bounded * static_cast<size_t>(ch));
    const size_t written = queued_pcm_->write(sanitize_scratch_.data(), bounded);
    if (written > 0) {
        push_had_data_.store(true, std::memory_order_release);
    }
    return written;
}

bool WasapiAudioDevice::is_available() const noexcept {
    return available_.load(std::memory_order_acquire);
}

bool WasapiAudioDevice::is_running() const noexcept {
    return state_.load(std::memory_order_acquire) == AudioDeviceState::Running;
}

bool WasapiAudioDevice::is_paused() const noexcept {
    return state_.load(std::memory_order_acquire) == AudioDeviceState::Paused;
}

AudioDeviceState WasapiAudioDevice::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

int WasapiAudioDevice::sample_rate() const noexcept {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    return config_.sample_rate;
}

int WasapiAudioDevice::channels() const noexcept {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    return config_.channels;
}

size_t WasapiAudioDevice::buffer_frames() const noexcept {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    return config_.buffer_frames;
}

AudioSampleFormat WasapiAudioDevice::sample_format() const noexcept {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    return config_.sample_format;
}

std::string WasapiAudioDevice::device_name() const {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    return device_name_;
}

std::string WasapiAudioDevice::last_error() const {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    return last_error_;
}

AudioDeviceDiagnostics WasapiAudioDevice::diagnostics() const {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    AudioDeviceDiagnostics d;
    d.available = available_.load(std::memory_order_acquire);
    d.is_physical = d.available;
    d.backend_name = "WASAPI";
    d.device_name = device_name_;
    d.sample_rate = config_.sample_rate;
    d.channels = config_.channels;
    d.sample_format = config_.sample_format;
    d.buffer_frames = config_.buffer_frames;
    d.hardware_sample_rate = negotiated_hw_.sample_rate;
    d.hardware_channels = negotiated_hw_.channels;
    d.hardware_sample_format = negotiated_hw_.sample_format;
    d.state = state_.load(std::memory_order_acquire);
    d.underrun_count = underruns_.load(std::memory_order_relaxed);
    d.frames_submitted = frames_submitted_.load(std::memory_order_relaxed);
    d.last_error = last_error_;
    return d;
}

std::thread::id WasapiAudioDevice::worker_thread_id() const noexcept {
    std::lock_guard<std::mutex> lock(diag_mutex_);
    return worker_thread_id_;
}

bool WasapiAudioDevice::is_event_driven() const noexcept {
    return event_driven_.load(std::memory_order_acquire);
}

size_t WasapiAudioDevice::queued_frames() const noexcept {
    return queued_pcm_ ? queued_pcm_->available_read_frames() : 0u;
}

size_t WasapiAudioDevice::queue_capacity_frames() const noexcept {
    return queued_pcm_ ? (queued_pcm_->frame_capacity() > 0 ? queued_pcm_->frame_capacity() - 1u : 0u) : 0u;
}

void WasapiAudioDevice::worker_loop() {
#ifdef _WIN32
    const HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    {
        std::lock_guard<std::mutex> lock(diag_mutex_);
        worker_thread_id_ = std::this_thread::get_id();
    }

    bool exit_requested = false;
    while (!exit_requested) {
        std::deque<std::shared_ptr<PendingCmd>> local_cmds;
        {
            std::lock_guard<std::mutex> lock(cmd_mutex_);
            local_cmds.swap(cmd_queue_);
        }

        for (auto& cmd : local_cmds) {
            if (!cmd) {
                continue;
            }
            if (cmd->type == WorkerCmdType::Shutdown) {
                handle_stop_on_worker();
                cleanup_endpoint_on_worker();
                {
                    std::lock_guard<std::mutex> lock(cmd_mutex_);
                    cmd->result = true;
                    cmd->done = true;
                }
                cmd_ack_cv_.notify_all();
                exit_requested = true;
                break;
            }
            handle_command(*cmd);
        }
        if (exit_requested) {
            break;
        }

        const AudioDeviceState cur = state_.load(std::memory_order_acquire);
        if (cur == AudioDeviceState::Running && endpoint_) {
#ifdef _WIN32
            void* wasapi_wait = endpoint_->native_wait_handle();
            if (event_driven_.load(std::memory_order_acquire) && wasapi_wait && control_event_) {
                HANDLE handles[2] = {
                    static_cast<HANDLE>(control_event_),
                    static_cast<HANDLE>(wasapi_wait)
                };
                const DWORD wait_res = WaitForMultipleObjects(2, handles, FALSE, 200);
                if (wait_res == WAIT_OBJECT_0) {
                    continue; // Control command arrived
                }
                if (wait_res == WAIT_OBJECT_0 + 1 || wait_res == WAIT_TIMEOUT) {
                    render_available_frames();
                } else {
                    fail_device_on_worker("WaitForMultipleObjects failed in WASAPI worker");
                }
            } else if (control_event_) {
                const DWORD period_ms = static_cast<DWORD>(std::clamp<size_t>(
                    (hw_buffer_frames_ * 500u) / static_cast<size_t>(std::max(8000, negotiated_hw_.sample_rate)),
                    2u, 20u));
                const DWORD wait_res = WaitForSingleObject(static_cast<HANDLE>(control_event_), period_ms);
                if (wait_res == WAIT_OBJECT_0) {
                    continue;
                }
                render_available_frames();
            }
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
#endif
        } else {
#ifdef _WIN32
            if (control_event_) {
                WaitForSingleObject(static_cast<HANDLE>(control_event_), INFINITE);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
#else
            std::unique_lock<std::mutex> lock(cmd_mutex_);
            cmd_ack_cv_.wait_for(lock, std::chrono::milliseconds(20), [this]() {
                return !cmd_queue_.empty() || shutting_down_.load(std::memory_order_acquire);
            });
#endif
        }
    }

    cleanup_endpoint_on_worker();
#ifdef _WIN32
    if (SUCCEEDED(com_hr)) {
        CoUninitialize();
    }
#endif
}

void WasapiAudioDevice::handle_command(PendingCmd& cmd) {
    bool ok = false;
    switch (cmd.type) {
        case WorkerCmdType::Initialize:
            ok = handle_initialize_on_worker(cmd.config);
            break;
        case WorkerCmdType::Start:
            ok = handle_start_on_worker(std::move(cmd.callback));
            break;
        case WorkerCmdType::Pause:
            ok = handle_pause_on_worker();
            break;
        case WorkerCmdType::Resume:
            ok = handle_resume_on_worker();
            break;
        case WorkerCmdType::Flush:
            handle_flush_on_worker();
            ok = true;
            break;
        case WorkerCmdType::Stop:
            handle_stop_on_worker();
            ok = true;
            break;
        case WorkerCmdType::Shutdown:
            ok = true;
            break;
    }
    {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        cmd.result = ok;
        cmd.done = true;
    }
    cmd_ack_cv_.notify_all();
}

void WasapiAudioDevice::cleanup_endpoint_on_worker() {
    if (endpoint_) {
        endpoint_->close();
        endpoint_.reset();
    }
    available_.store(false, std::memory_order_release);
    event_driven_.store(false, std::memory_order_release);
    hw_buffer_frames_ = 0;
    has_fed_initial_block_ = false;
}

bool WasapiAudioDevice::handle_initialize_on_worker(const AudioDeviceConfig& config) {
    handle_stop_on_worker();
    cleanup_endpoint_on_worker();

    if (!config.is_valid()) {
        std::lock_guard<std::mutex> lock(diag_mutex_);
        last_error_ = "Invalid AudioDeviceConfig";
        state_.store(AudioDeviceState::Error, std::memory_order_release);
        available_.store(false, std::memory_order_release);
        return false;
    }

    std::unique_ptr<IWasapiEndpoint> ep = endpoint_factory_ ? endpoint_factory_() : nullptr;
    if (!ep) {
        std::lock_guard<std::mutex> lock(diag_mutex_);
        last_error_ = "WASAPI backend unavailable on this platform";
        state_.store(AudioDeviceState::Error, std::memory_order_release);
        available_.store(false, std::memory_order_release);
        return false;
    }

    std::string opened_name;
    WasapiStatus st = ep->open_default_render_device(opened_name);
    if (!st.ok) {
        std::lock_guard<std::mutex> lock(diag_mutex_);
        device_name_.clear();
        last_error_ = st.error_message.empty() ? "Failed to open WASAPI audio endpoint" : st.error_message;
        state_.store(AudioDeviceState::Error, std::memory_order_release);
        available_.store(false, std::memory_order_release);
        return false;
    }

    WasapiEndpointFormat mix_fmt{};
    st = ep->get_mix_format(mix_fmt);
    if (!st.ok || mix_fmt.sample_format == AudioSampleFormat::Unknown) {
        ep->close();
        std::lock_guard<std::mutex> lock(diag_mutex_);
        device_name_ = opened_name;
        last_error_ = st.error_message.empty() ? "Unsupported WASAPI mix format" : st.error_message;
        state_.store(AudioDeviceState::Error, std::memory_order_release);
        available_.store(false, std::memory_order_release);
        return false;
    }

    WasapiEndpointFormat desired{};
    desired.sample_rate = config.sample_rate;
    desired.channels = config.channels;
    desired.sample_format = config.sample_format;
    desired.valid_bits_per_sample = (config.sample_format == AudioSampleFormat::Int16) ? 16 : 32;

    WasapiEndpointFormat chosen_hw = mix_fmt;
    WasapiEndpointFormat closest{};
    bool has_closest = false;
    if (ep->is_format_supported_shared(desired, closest, has_closest)) {
        chosen_hw = desired;
    } else if (has_closest && closest.sample_format != AudioSampleFormat::Unknown) {
        chosen_hw = closest;
    } else {
        WasapiEndpointFormat mix_float = mix_fmt;
        mix_float.sample_format = AudioSampleFormat::Float32;
        mix_float.valid_bits_per_sample = 32;
        WasapiEndpointFormat closest_float{};
        bool has_closest_float = false;
        if (ep->is_format_supported_shared(mix_float, closest_float, has_closest_float)) {
            chosen_hw = mix_float;
        } else {
            chosen_hw = mix_fmt;
        }
    }

    size_t actual_hw_frames = 0;
    bool actual_event_mode = false;
    st = ep->initialize_client(chosen_hw, config.buffer_frames, true,
                               actual_hw_frames, actual_event_mode);
    if (!st.ok || actual_hw_frames == 0) {
        ep->close();
        std::lock_guard<std::mutex> lock(diag_mutex_);
        device_name_ = opened_name;
        last_error_ = st.error_message.empty() ? "WASAPI initialize_client failed" : st.error_message;
        state_.store(AudioDeviceState::Error, std::memory_order_release);
        available_.store(false, std::memory_order_release);
        return false;
    }

    if (!converter_.configure(config.sample_rate,
                              config.channels,
                              chosen_hw.sample_rate,
                              chosen_hw.channels,
                              chosen_hw.sample_format,
                              actual_hw_frames,
                              chosen_hw.valid_bits_per_sample)) {
        ep->close();
        std::lock_guard<std::mutex> lock(diag_mutex_);
        device_name_ = opened_name;
        last_error_ = "Audio format converter configuration failed";
        state_.store(AudioDeviceState::Error, std::memory_order_release);
        available_.store(false, std::memory_order_release);
        return false;
    }

    const size_t ring_cap = std::max<size_t>(8192u, config.buffer_frames * 8u);
    queued_pcm_ = std::make_unique<AudioRingBuffer>(ring_cap, config.channels);
    sanitize_scratch_.assign(ring_cap * static_cast<size_t>(config.channels), 0.0f);
    engine_scratch_.assign(converter_.max_src_frames() * static_cast<size_t>(config.channels), 0.0f);

    hw_buffer_frames_ = actual_hw_frames;
    has_fed_initial_block_ = false;
    push_had_data_.store(false, std::memory_order_release);
    endpoint_ = std::move(ep);
    event_driven_.store(actual_event_mode, std::memory_order_release);
    available_.store(true, std::memory_order_release);

    {
        std::lock_guard<std::mutex> lock(diag_mutex_);
        config_ = config;
        config_.buffer_frames = actual_hw_frames;
        negotiated_hw_.sample_rate = chosen_hw.sample_rate;
        negotiated_hw_.channels = chosen_hw.channels;
        negotiated_hw_.sample_format = chosen_hw.sample_format;
        negotiated_hw_.buffer_frames = actual_hw_frames;
        hardware_valid_bits_ = chosen_hw.valid_bits_per_sample;
        device_name_ = opened_name;
        last_error_.clear();
    }
    state_.store(AudioDeviceState::Stopped, std::memory_order_release);
    return true;
}

bool WasapiAudioDevice::handle_start_on_worker(AudioRenderCallback callback) {
    if (!endpoint_ || !available_.load(std::memory_order_acquire) ||
        state_.load(std::memory_order_acquire) == AudioDeviceState::Error) {
        return false;
    }
    if (state_.load(std::memory_order_acquire) == AudioDeviceState::Running) {
        return false;
    }
    if (callback) {
        callback_ = std::move(callback);
    }
    converter_.reset_state();
    has_fed_initial_block_ = false;

    const WasapiStatus st = endpoint_->start();
    if (!st.ok) {
        fail_device_on_worker(st.error_message.empty() ? "WASAPI start failed" : st.error_message);
        return false;
    }
    state_.store(AudioDeviceState::Running, std::memory_order_release);
    render_available_frames();
    return state_.load(std::memory_order_acquire) == AudioDeviceState::Running;
}

bool WasapiAudioDevice::handle_pause_on_worker() {
    if (!endpoint_ || state_.load(std::memory_order_acquire) != AudioDeviceState::Running) {
        return false;
    }
    const WasapiStatus st = endpoint_->stop();
    if (!st.ok) {
        fail_device_on_worker(st.error_message.empty() ? "WASAPI pause failed" : st.error_message);
        return false;
    }
    state_.store(AudioDeviceState::Paused, std::memory_order_release);
    return true;
}

bool WasapiAudioDevice::handle_resume_on_worker() {
    if (!endpoint_ || state_.load(std::memory_order_acquire) != AudioDeviceState::Paused) {
        return false;
    }
    has_fed_initial_block_ = false;
    const WasapiStatus st = endpoint_->start();
    if (!st.ok) {
        fail_device_on_worker(st.error_message.empty() ? "WASAPI resume failed" : st.error_message);
        return false;
    }
    state_.store(AudioDeviceState::Running, std::memory_order_release);
    render_available_frames();
    return state_.load(std::memory_order_acquire) == AudioDeviceState::Running;
}

void WasapiAudioDevice::handle_flush_on_worker() {
    const AudioDeviceState prev = state_.load(std::memory_order_acquire);
    const bool was_running = (prev == AudioDeviceState::Running);
    if (endpoint_) {
        if (was_running) {
            const WasapiStatus stop_st = endpoint_->stop();
            if (!stop_st.ok) {
                fail_device_on_worker(stop_st.error_message);
                return;
            }
        }
        const WasapiStatus reset_st = endpoint_->reset();
        if (!reset_st.ok) {
            fail_device_on_worker(reset_st.error_message);
            return;
        }
    }
    if (queued_pcm_) {
        queued_pcm_->reset();
    }
    push_had_data_.store(false, std::memory_order_release);
    converter_.reset_state();
    has_fed_initial_block_ = false;

    if (was_running && endpoint_) {
        const WasapiStatus start_st = endpoint_->start();
        if (!start_st.ok) {
            fail_device_on_worker(start_st.error_message);
            return;
        }
        state_.store(AudioDeviceState::Running, std::memory_order_release);
    } else if (prev != AudioDeviceState::Error && available_.load(std::memory_order_acquire)) {
        // Preserve Paused or Stopped cleanly.
        state_.store(prev, std::memory_order_release);
    }
}

void WasapiAudioDevice::handle_stop_on_worker() {
    if (endpoint_) {
        (void)endpoint_->stop();
        (void)endpoint_->reset();
    }
    if (queued_pcm_) {
        queued_pcm_->reset();
    }
    push_had_data_.store(false, std::memory_order_release);
    converter_.reset_state();
    has_fed_initial_block_ = false;
    if (state_.load(std::memory_order_acquire) != AudioDeviceState::Error &&
        available_.load(std::memory_order_acquire)) {
        state_.store(AudioDeviceState::Stopped, std::memory_order_release);
    }
}

void WasapiAudioDevice::fail_device_on_worker(const std::string& reason) {
    cleanup_endpoint_on_worker();
    {
        std::lock_guard<std::mutex> lock(diag_mutex_);
        last_error_ = reason.empty() ? "WASAPI audio device error" : reason;
    }
    state_.store(AudioDeviceState::Error, std::memory_order_release);
}

void WasapiAudioDevice::pull_engine_frames(float* dst, size_t frame_count) {
    if (!dst || frame_count == 0) {
        return;
    }
    const int ch = config_.channels > 0 ? config_.channels : 2;
    const size_t sch = static_cast<size_t>(ch);

    if (queued_pcm_ && queued_pcm_->available_read_frames() > 0) {
        const size_t read = queued_pcm_->read(dst, frame_count);
        if (read < frame_count) {
            if (callback_) {
                callback_(dst + read * sch, frame_count - read, ch, config_.sample_rate);
            } else if (push_had_data_.load(std::memory_order_acquire)) {
                underruns_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return;
    }

    if (callback_) {
        callback_(dst, frame_count, ch, config_.sample_rate);
        return;
    }

    if (push_had_data_.load(std::memory_order_acquire)) {
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }
    std::fill_n(dst, frame_count * sch, 0.0f);
}

void WasapiAudioDevice::render_available_frames() {
    if (!endpoint_ || state_.load(std::memory_order_acquire) != AudioDeviceState::Running) {
        return;
    }

    size_t padding = 0;
    WasapiStatus st = endpoint_->get_current_padding(padding);
    if (!st.ok) {
        fail_device_on_worker(st.error_message.empty() ? "WASAPI GetCurrentPadding failed" : st.error_message);
        return;
    }

    if (has_fed_initial_block_ && padding == 0) {
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }

    if (padding >= hw_buffer_frames_) {
        return;
    }

    const size_t frames_to_write = hw_buffer_frames_ - padding;
    uint8_t* hw_ptr = nullptr;
    st = endpoint_->get_buffer(frames_to_write, &hw_ptr);
    if (!st.ok || !hw_ptr) {
        fail_device_on_worker(st.error_message.empty() ? "WASAPI GetBuffer returned null or error" : st.error_message);
        return;
    }

    const size_t src_needed = converter_.next_src_frames_needed(frames_to_write);
    const size_t ch = static_cast<size_t>(config_.channels > 0 ? config_.channels : 2);
    const size_t max_src = engine_scratch_.size() / ch;
    const size_t bounded_src = std::min(src_needed, max_src);

    if (bounded_src > 0) {
        pull_engine_frames(engine_scratch_.data(), bounded_src);
    }

    const bool converted = converter_.process(
        engine_scratch_.data(), bounded_src, hw_ptr, frames_to_write);
    if (!converted) {
        std::memset(hw_ptr, 0, frames_to_write * converter_.bytes_per_dst_frame());
        (void)endpoint_->release_buffer(frames_to_write, true);
        fail_device_on_worker("Audio format converter overflow or invalid block");
        return;
    }

    st = endpoint_->release_buffer(frames_to_write, false);
    if (!st.ok) {
        fail_device_on_worker(st.error_message.empty() ? "WASAPI ReleaseBuffer failed" : st.error_message);
        return;
    }

    frames_submitted_.fetch_add(frames_to_write, std::memory_order_relaxed);
    has_fed_initial_block_ = true;
}

} // namespace ave
