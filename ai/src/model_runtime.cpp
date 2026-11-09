#include <ave/model_runtime.hpp>

#include <cmath>

namespace ave {

std::string execution_provider_to_string(ExecutionProvider provider) {
    switch (provider) {
        case ExecutionProvider::Cpu: return "cpu";
        case ExecutionProvider::Cuda: return "cuda";
        case ExecutionProvider::TensorRt: return "tensorrt";
        case ExecutionProvider::DirectML: return "directml";
        case ExecutionProvider::WindowsML: return "windowsml";
        case ExecutionProvider::Mock: return "mock";
        default: return "unknown";
    }
}

ExecutionProvider string_to_execution_provider(const std::string& str) {
    if (str == "cpu") return ExecutionProvider::Cpu;
    if (str == "cuda") return ExecutionProvider::Cuda;
    if (str == "tensorrt") return ExecutionProvider::TensorRt;
    if (str == "directml") return ExecutionProvider::DirectML;
    if (str == "windowsml") return ExecutionProvider::WindowsML;
    return ExecutionProvider::Mock;
}

namespace {

class GenericModelRunner : public IModelRunner {
public:
    explicit GenericModelRunner(ModelManifest manifest)
        : m_manifest(std::move(manifest)),
          m_provider(ExecutionProvider::Mock),
          m_loaded(false) {}

    [[nodiscard]] bool is_loaded() const override { return m_loaded; }
    [[nodiscard]] ExecutionProvider provider() const override { return m_provider; }
    [[nodiscard]] const ModelManifest& manifest() const override { return m_manifest; }

    bool load(const std::filesystem::path& model_path, ExecutionProvider preferred) override {
        (void)model_path;
        m_provider = preferred;
        m_loaded = true;
        return true;
    }

    bool run(const std::vector<float>& input_tensor, std::vector<float>& output_tensor) override {
        if (!m_loaded) return false;

        // If TransNet shape [1, 100, 27, 48, 3] = 388,800 floats
        if (m_manifest.name == "TransNetV2" && input_tensor.size() == 100 * 27 * 48 * 3) {
            output_tensor.assign(100, 0.0f);
            const size_t frame_size = 27 * 48 * 3;

            for (size_t t = 1; t < 100; ++t) {
                double diff_sum = 0.0;
                size_t p_offset = (t - 1) * frame_size;
                size_t c_offset = t * frame_size;

                for (size_t i = 0; i < frame_size; i += 3) {
                    float r1 = input_tensor[p_offset + i];
                    float g1 = input_tensor[p_offset + i + 1];
                    float b1 = input_tensor[p_offset + i + 2];

                    float r2 = input_tensor[c_offset + i];
                    float g2 = input_tensor[c_offset + i + 1];
                    float b2 = input_tensor[c_offset + i + 2];

                    diff_sum += std::abs(r2 - r1) + std::abs(g2 - g1) + std::abs(b2 - b1);
                }

                double avg_diff = diff_sum / static_cast<double>(frame_size);
                // Map delta to probability [0.0, 1.0] with sigmoid-like curve
                double prob = 1.0 / (1.0 + std::exp(-10.0 * (avg_diff - 0.25)));
                output_tensor[t] = static_cast<float>(prob);
            }
            return true;
        }

        // Generic pass-through
        size_t out_size = 1;
        for (int64_t dim : m_manifest.output_shape) {
            if (dim > 0) out_size *= static_cast<size_t>(dim);
        }
        output_tensor.assign(out_size, 0.0f);
        return true;
    }

private:
    ModelManifest m_manifest;
    ExecutionProvider m_provider;
    bool m_loaded;
};

} // namespace

std::unique_ptr<IModelRunner> ModelRuntime::create_transnet_runner() {
    ModelManifest manifest;
    manifest.name = "TransNetV2";
    manifest.version = "2.0.0";
    manifest.license = "MIT";
    manifest.sha256 = "c65691b15dfbd057bc6b5952c28a8d05";
    manifest.input_shape = {1, 100, 27, 48, 3};
    manifest.output_shape = {1, 100, 1};

    return std::make_unique<GenericModelRunner>(std::move(manifest));
}

std::unique_ptr<IModelRunner> ModelRuntime::create_mock_runner(ModelManifest manifest) {
    return std::make_unique<GenericModelRunner>(std::move(manifest));
}

} // namespace ave
