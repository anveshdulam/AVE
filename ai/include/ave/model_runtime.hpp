#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ave {

enum class ExecutionProvider {
    Cpu,
    Cuda,
    TensorRt,
    DirectML,
    WindowsML,
    Mock
};

[[nodiscard]] std::string execution_provider_to_string(ExecutionProvider provider);
[[nodiscard]] ExecutionProvider string_to_execution_provider(const std::string& str);

struct ModelManifest {
    std::string name;
    std::string version;
    std::string sha256;
    std::string license; // e.g. "MIT", "Apache-2.0"
    std::vector<int64_t> input_shape;
    std::vector<int64_t> output_shape;
};

/**
 * @brief Abstract interface for local neural network inference runners.
 *
 * Wraps ONNX Runtime / local execution behind a strict boundary.
 * DirectML is kept replaceable per ADR (Windows ML / CPU fallback).
 * Thread safety: one session per worker with fixed static shapes.
 */
class IModelRunner {
public:
    virtual ~IModelRunner() = default;

    [[nodiscard]] virtual bool is_loaded() const = 0;
    [[nodiscard]] virtual ExecutionProvider provider() const = 0;
    [[nodiscard]] virtual const ModelManifest& manifest() const = 0;

    virtual bool load(const std::filesystem::path& model_path, ExecutionProvider preferred = ExecutionProvider::Cpu) = 0;
    virtual bool run(const std::vector<float>& input_tensor, std::vector<float>& output_tensor) = 0;
};

/**
 * @brief Factory and management layer for local AI models.
 */
class ModelRuntime {
public:
    /**
     * @brief Creates a model runner configured for TransNetV2 shot detection.
     * Input shape: [1, 100, 27, 48, 3] (100 frames, 27x48 RGB).
     */
    [[nodiscard]] static std::unique_ptr<IModelRunner> create_transnet_runner();

    /**
     * @brief Creates a lightweight mock model runner for testing and verification.
     */
    [[nodiscard]] static std::unique_ptr<IModelRunner> create_mock_runner(ModelManifest manifest);
};

} // namespace ave
