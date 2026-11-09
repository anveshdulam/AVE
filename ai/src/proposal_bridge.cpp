#include <ave/proposal_bridge.hpp>

#include <sstream>

namespace ave {

namespace {

const Track* resolve_track(const Timeline& timeline, const std::string& track_id_or_name) {
    const Track* t = timeline.find_track(track_id_or_name);
    if (t) return t;
    for (const auto& tr : timeline.video_tracks()) {
        if (tr.name() == track_id_or_name) return &tr;
    }
    for (const auto& tr : timeline.audio_tracks()) {
        if (tr.name() == track_id_or_name) return &tr;
    }
    return nullptr;
}

} // namespace

bool ProposalBridge::validate(
    const EditProposalEnvelope& proposal,
    const Timeline& timeline,
    const MediaPool& pool,
    std::vector<std::string>& out_errors
) {
    if (proposal.schema_version < 1) {
        out_errors.push_back("Unsupported schema version: " + std::to_string(proposal.schema_version));
    }
    if (proposal.proposal_id.empty()) {
        out_errors.push_back("Missing proposal_id.");
    }
    if (proposal.operations.empty()) {
        out_errors.push_back("Proposal contains zero operations.");
    }

    for (size_t i = 0; i < proposal.operations.size(); ++i) {
        const auto& op = proposal.operations[i];
        if (op.target_ids.empty()) {
            out_errors.push_back("Operation " + std::to_string(i) + " has no target_ids.");
            continue;
        }

        const std::string& target = op.target_ids[0];
        const Track* track = resolve_track(timeline, target);
        if (!track) {
            out_errors.push_back("Target track '" + target + "' does not exist on timeline.");
        }

        if (op.parameters.duration.to_seconds() <= 0.0) {
            out_errors.push_back("Operation " + std::to_string(i) + " has invalid duration <= 0.");
        }
        if (op.parameters.timeline_in.to_seconds() < 0.0) {
            out_errors.push_back("Operation " + std::to_string(i) + " has negative timeline_in.");
        }

        if (!op.parameters.asset_id.empty() && !pool.find_asset(op.parameters.asset_id)) {
            out_errors.push_back("Asset '" + op.parameters.asset_id + "' not found in MediaPool.");
        }
    }

    return out_errors.empty();
}

DryRunResult ProposalBridge::dry_run(
    const EditProposalEnvelope& proposal,
    const Timeline& timeline,
    const MediaPool& pool,
    const AudioRhythmReport& rhythm,
    const ReferenceFingerprint* target_reference
) {
    DryRunResult result;
    std::vector<std::string> errors;
    if (!validate(proposal, timeline, pool, errors)) {
        result.is_valid = false;
        result.error_message = errors.empty() ? "Validation failed" : errors[0];
        return result;
    }

    // Clone timeline and pool for isolated sandbox execution
    Timeline clone = timeline;
    MediaPool pool_clone = pool;

    for (size_t i = 0; i < proposal.operations.size(); ++i) {
        const auto& op = proposal.operations[i];
        if (op.operation_type == "insert_clip") {
            const Track* target_track = resolve_track(clone, op.target_ids[0]);
            std::string resolved_track_id = target_track ? target_track->id() : op.target_ids[0];

            TimelineItem item(
                op.parameters.clip_id.empty() ? UUID::generate() : op.parameters.clip_id,
                op.parameters.clip_id,
                op.parameters.asset_id,
                op.parameters.clip_name,
                op.parameters.timeline_in,
                op.parameters.duration,
                op.parameters.source_in,
                op.parameters.speed
            );

            InsertClipCommand cmd(resolved_track_id, std::move(item), CommandOrigin::AI);
            auto val_res = cmd.validate(clone, pool_clone);
            if (!val_res.is_success()) {
                result.is_valid = false;
                result.error_message = "Dry-run validation error on op " + std::to_string(i) + ": " + val_res.message;
                return result;
            }

            auto app_res = cmd.apply(clone, pool_clone);
            if (!app_res.is_success()) {
                result.is_valid = false;
                result.error_message = "Dry-run apply error on op " + std::to_string(i) + ": " + app_res.message;
                return result;
            }
        }
    }

    result.is_valid = true;
    result.operations_count = proposal.operations.size();
    result.resulting_duration = clone.duration();
    result.resulting_fingerprint = FingerprintEngine::from_timeline(clone, rhythm);

    if (target_reference) {
        result.reference_distance = FingerprintEngine::compute_distance(result.resulting_fingerprint, *target_reference);
    }

    return result;
}

CommandResult ProposalBridge::commit(
    const EditProposalEnvelope& proposal,
    Timeline& timeline,
    MediaPool& pool,
    CommandHistory& history
) {
    std::vector<std::string> errors;
    if (!validate(proposal, timeline, pool, errors)) {
        return CommandResult::failure(
            CommandStatus::FailedPrecondition,
            errors.empty() ? "Proposal validation failed." : errors[0]
        );
    }

    std::vector<std::unique_ptr<Command>> child_commands;
    child_commands.reserve(proposal.operations.size());

    for (const auto& op : proposal.operations) {
        if (op.operation_type == "insert_clip") {
            const Track* target_track = resolve_track(timeline, op.target_ids[0]);
            std::string resolved_track_id = target_track ? target_track->id() : op.target_ids[0];

            TimelineItem item(
                op.parameters.clip_id.empty() ? UUID::generate() : op.parameters.clip_id,
                op.parameters.clip_id,
                op.parameters.asset_id,
                op.parameters.clip_name,
                op.parameters.timeline_in,
                op.parameters.duration,
                op.parameters.source_in,
                op.parameters.speed
            );
            child_commands.push_back(std::make_unique<InsertClipCommand>(resolved_track_id, std::move(item), CommandOrigin::AI));
        }
    }

    auto composite = std::make_unique<CompositeCommand>(
        "Apply AI Proposal " + proposal.proposal_id,
        std::move(child_commands),
        CommandOrigin::AI
    );

    return history.execute(std::move(composite), timeline, pool);
}

std::string ProposalBridge::generate_diff_summary(
    const EditProposalEnvelope& proposal,
    const Timeline& /*timeline*/
) {
    std::ostringstream oss;
    oss << "Edit Proposal [" << proposal.proposal_id << "] from " << proposal.source << ":\n";
    oss << "Operations: " << proposal.operations.size() << "\n";
    for (size_t i = 0; i < proposal.operations.size(); ++i) {
        const auto& op = proposal.operations[i];
        oss << "  " << (i + 1) << ". " << op.operation_type << " on "
            << (op.target_ids.empty() ? "unknown" : op.target_ids[0])
            << " at " << op.parameters.timeline_in.to_seconds() << "s "
            << "(duration " << op.parameters.duration.to_seconds() << "s, "
            << "reason: " << op.evidence.justified_by << ")\n";
    }
    return oss.str();
}

} // namespace ave
