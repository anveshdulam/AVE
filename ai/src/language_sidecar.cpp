#include <ave/language_sidecar.hpp>

#include <chrono>

namespace ave {

LanguageSidecar::LanguageSidecar(SidecarConfig config)
    : m_config(std::move(config)) {
    if (m_config.autostart) {
        start();
    }
}

LanguageSidecar::~LanguageSidecar() {
    stop();
}

bool LanguageSidecar::start() {
    m_state = SidecarState::Ready;
    return true;
}

void LanguageSidecar::stop() {
    m_state = SidecarState::Stopped;
}

bool LanguageSidecar::ping() {
    return (m_state == SidecarState::Ready);
}

SidecarResponse LanguageSidecar::query(const std::string& request_type, const std::string& payload_json) {
    SidecarResponse resp;
    auto start_time = std::chrono::steady_clock::now();

    if (m_state != SidecarState::Ready) {
        resp.success = false;
        resp.error_message = "Sidecar process is not running or has crashed.";
        return resp;
    }

    if (m_mock_mode) {
        resp.success = true;
        resp.response_payload = "{\"status\":\"ok\",\"type\":\"" + request_type + "\",\"echo\":\"" + payload_json + "\"}";
        auto end_time = std::chrono::steady_clock::now();
        resp.latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        return resp;
    }

    // In native production, IPC is transmitted over local named pipe
    resp.success = false;
    resp.error_message = "Native named pipe transport uninitialized; fallback to local parser.";
    return resp;
}

void LanguageSidecar::simulate_crash_for_testing() {
    m_state = SidecarState::Crashed;
}

} // namespace ave

