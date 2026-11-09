#pragma once

#include <ave/edit_proposal.hpp>
#include <ave/proposal_bridge.hpp>
#include <ave/time.hpp>

#include <string>
#include <vector>

namespace ave {

struct ProposalDiffItem {
    size_t operation_index = 0;
    std::string clip_name;
    std::string track_id;
    Rational timeline_in;
    Rational duration;
    std::string justification;
    double confidence = 1.0;
};

/**
 * @brief ViewModel for the interactive AI proposal review and approval UI.
 */
struct ProposalInspectionViewModel {
    std::string proposal_id;
    std::string source;
    size_t operations_count = 0;
    Rational total_duration;
    double stylistic_distance = 0.0; // Distance to reference target [0.0, 1.0]
    double stylistic_fidelity_pct = 100.0; // 100 * (1.0 - distance)
    std::vector<ProposalDiffItem> diff_items;
    std::vector<std::string> warnings;
    bool is_valid = false;

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] std::string to_formatted_summary() const;
};

/**
 * @brief Prepares interactive proposal preview, diff metrics, and approval states for UI presentation.
 */
class ProposalInspector {
public:
    ProposalInspector() = default;

    /**
     * @brief Builds a presentation ViewModel from a proposal and its sandbox dry-run result.
     */
    [[nodiscard]] static ProposalInspectionViewModel inspect(
        const EditProposalEnvelope& proposal,
        const DryRunResult& dry_run
    );
};

} // namespace ave
