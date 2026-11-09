#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ave {

enum class SidecarState {
    Stopped,
    Starting,
    Ready,
    Busy,
    Crashed,
    Error
};

struct SidecarConfig {
    std::string sidecar_executable;
    std::string model_identifier = "qwen3-vl-4b-gguf";
    int timeout_ms = 5000;
    bool autostart = true;
};

struct SidecarResponse {
    bool success = false;
    std::string response_payload;
    std::string error_message;
    int64_t latency_ms = 0;
};

/**
 * @brief Out-of-process AI sidecar manager with crash isolation and timeout protection.
 *
 * Ensures that heavy LLM / VLM inference runs in an isolated external worker process.
 * If the sidecar exhausts VRAM, SegFaults, or hangs, the native video editor remains
 * 100% stable and gracefully degrades to the local rule-based language parser.
 */
class LanguageSidecar {
public:
    explicit LanguageSidecar(SidecarConfig config = {});
    virtual ~LanguageSidecar();

    [[nodiscard]] SidecarState state() const noexcept { return m_state; }
    [[nodiscard]] bool is_ready() const noexcept { return m_state == SidecarState::Ready; }
    [[nodiscard]] const SidecarConfig& config() const noexcept { return m_config; }

    /**
     * @brief Starts the sidecar process and initiates local IPC handshake.
     */
    bool start();

    /**
     * @brief Stops the sidecar process cleanly.
     */
    void stop();

    /**
     * @brief Sends a health check ping to verify sidecar responsiveness.
     */
    bool ping();

    /**
     * @brief Dispatches a prompt or inference query to the sidecar with timeout guard.
     */
    [[nodiscard]] SidecarResponse query(const std::string& request_type, const std::string& payload_json);

    /**
     * @brief Simulates an isolated worker crash for resilience testing.
     */
    void simulate_crash_for_testing();

private:
    SidecarConfig m_config;
    SidecarState m_state{SidecarState::Stopped};
    bool m_mock_mode{true}; // Local deterministic mock for headless testing
};

} // namespace ave
