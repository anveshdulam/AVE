#include <ave/proposal_inspector.hpp>

#include <cmath>
#include <iomanip>
#include <sstream>

namespace ave {

ProposalInspectionViewModel ProposalInspector::inspect(
    const EditProposalEnvelope& proposal,
    const DryRunResult& dry_run
) {
    ProposalInspectionViewModel vm;
    vm.proposal_id = proposal.proposal_id;
    vm.source = proposal.source;
    vm.operations_count = proposal.operations.size();
    vm.total_duration = dry_run.resulting_duration;
    vm.stylistic_distance = dry_run.reference_distance;
    vm.stylistic_fidelity_pct = (std::max)(0.0, (1.0 - dry_run.reference_distance) * 100.0);
    vm.warnings = proposal.warnings;
    vm.is_valid = dry_run.is_valid;

    if (!dry_run.error_message.empty()) {
        vm.warnings.push_back(dry_run.error_message);
    }

    for (size_t i = 0; i < proposal.operations.size(); ++i) {
        const auto& op = proposal.operations[i];
        ProposalDiffItem item;
        item.operation_index = i;
        item.clip_name = op.parameters.clip_name;
        item.track_id = op.target_ids.empty() ? "V1" : op.target_ids[0];
        item.timeline_in = op.parameters.timeline_in;
        item.duration = op.parameters.duration;
        item.justification = op.evidence.justified_by;
        item.confidence = op.evidence.confidence;
        vm.diff_items.push_back(std::move(item));
    }

    return vm;
}

std::string ProposalInspectionViewModel::to_json() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{\n";
    oss << "  \"proposal_id\": \"" << proposal_id << "\",\n";
    oss << "  \"source\": \"" << source << "\",\n";
    oss << "  \"is_valid\": " << (is_valid ? "true" : "false") << ",\n";
    oss << "  \"operations_count\": " << operations_count << ",\n";
    oss << "  \"total_duration_sec\": " << total_duration.to_seconds() << ",\n";
    oss << "  \"stylistic_distance\": " << stylistic_distance << ",\n";
    oss << "  \"stylistic_fidelity_pct\": " << stylistic_fidelity_pct << ",\n";
    oss << "  \"items\": [";
    for (size_t i = 0; i < diff_items.size(); ++i) {
        const auto& it = diff_items[i];
        if (i > 0) oss << ",";
        oss << "\n    {\"clip\":\"" << it.clip_name << "\",\"track\":\"" << it.track_id
            << "\",\"start\":" << it.timeline_in.to_seconds()
            << ",\"dur\":" << it.duration.to_seconds()
            << ",\"reason\":\"" << it.justification << "\"}";
    }
    if (!diff_items.empty()) oss << "\n  ";
    oss << "]\n";
    oss << "}\n";
    return oss.str();
}

std::string ProposalInspectionViewModel::to_formatted_summary() const {
    std::ostringstream oss;
    oss << "=== AI Edit Proposal Inspector [" << proposal_id << "] ===\n";
    oss << "Status: " << (is_valid ? "VALID (Approved for Dry-Run)" : "INVALID") << "\n";
    oss << "Clips Proposed: " << operations_count << " | Duration: "
        << std::fixed << std::setprecision(1) << total_duration.to_seconds() << "s\n";
    oss << "Stylistic Fidelity: " << std::fixed << std::setprecision(1) << stylistic_fidelity_pct << "%\n";
    oss << "Operations:\n";
    for (const auto& it : diff_items) {
        oss << "  - [" << it.track_id << "] " << it.clip_name << " ("
            << it.timeline_in.to_seconds() << "s -> " << (it.timeline_in + it.duration).to_seconds() << "s) "
            << "Justified by: " << it.justification << "\n";
    }
    return oss.str();
}

} // namespace ave

