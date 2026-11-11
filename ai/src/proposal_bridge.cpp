#include <ave/proposal_bridge.hpp>

#include <ave/uuid.hpp>

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

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

bool is_valid_source(const std::string& source) noexcept {
    return source == "ai" || source == "user" || source == "import" || source == "script";
}

bool is_supported_operation_type(const std::string& op_type) noexcept {
    // Only operations implemented atomically by dry_run()/commit() are accepted.
    return op_type == "insert_clip";
}

bool is_supported_blend_mode(const std::string& blend_mode) noexcept {
    return blend_mode.empty() ||
           blend_mode == "Over" ||
           blend_mode == "Multiply" ||
           blend_mode == "Screen" ||
           blend_mode == "Add";
}

TimelineItem build_timeline_item_for_op(const EditOperation& op,
                                        const Timeline& timeline,
                                        std::unordered_set<std::string>& used_item_ids) {
    std::string instance_id = op.parameters.clip_id;
    if (instance_id.empty() ||
        used_item_ids.find(instance_id) != used_item_ids.end() ||
        timeline.find_item(instance_id) != nullptr) {
        instance_id = "clip-" + UUID::generate();
    }
    used_item_ids.insert(instance_id);

    TimelineItem item(
        std::move(instance_id),
        op.parameters.clip_id,
        op.parameters.asset_id,
        op.parameters.clip_name,
        op.parameters.timeline_in,
        op.parameters.duration,
        op.parameters.source_in,
        op.parameters.speed
    );
    item.set_opacity(op.parameters.opacity);
    item.set_blend_mode(op.parameters.blend_mode.empty() ? "Over" : op.parameters.blend_mode);
    return item;
}

} // namespace

bool ProposalBridge::validate(
    const EditProposalEnvelope& proposal,
    const Timeline& timeline,
    const MediaPool& pool,
    std::vector<std::string>& out_errors
) {
    if (proposal.schema_version != 1) {
        out_errors.push_back("Unsupported schema version: " + std::to_string(proposal.schema_version));
    }
    if (proposal.proposal_id.empty()) {
        out_errors.push_back("Missing proposal_id.");
    }
    if (!is_valid_source(proposal.source)) {
        out_errors.push_back("Unsupported proposal source: '" + proposal.source + "'.");
    }
    if (proposal.operations.empty()) {
        out_errors.push_back("Proposal contains zero operations.");
    }

    for (size_t i = 0; i < proposal.operations.size(); ++i) {
        const auto& op = proposal.operations[i];
        const std::string op_prefix = "Operation " + std::to_string(i);

        if (op.operation_type.empty()) {
            out_errors.push_back(op_prefix + " missing operation_type.");
        } else if (!is_supported_operation_type(op.operation_type)) {
            out_errors.push_back(op_prefix + " has unsupported operation_type '" + op.operation_type + "'.");
        }

        if (op.target_ids.empty() || op.target_ids[0].empty()) {
            out_errors.push_back(op_prefix + " has no target_ids.");
        } else {
            const std::string& target = op.target_ids[0];
            const Track* track = resolve_track(timeline, target);
            if (!track) {
                out_errors.push_back("Target track '" + target + "' does not exist on timeline.");
            }
        }

        bool times_valid = true;
        if (op.parameters.timeline_in.denominator() <= 0 || op.parameters.timeline_in.numerator() < 0) {
            out_errors.push_back(op_prefix + " has invalid or negative timeline_in.");
            times_valid = false;
        }
        if (op.parameters.duration.denominator() <= 0 || op.parameters.duration.numerator() <= 0) {
            out_errors.push_back(op_prefix + " has invalid duration <= 0.");
            times_valid = false;
        }
        if (op.parameters.source_in.denominator() <= 0 || op.parameters.source_in.numerator() < 0) {
            out_errors.push_back(op_prefix + " has invalid or negative source_in.");
            times_valid = false;
        }
        if (op.parameters.speed.denominator() <= 0 || op.parameters.speed.numerator() <= 0) {
            out_errors.push_back(op_prefix + " has invalid speed <= 0.");
            times_valid = false;
        }
        if (op.evidence.timestamp.denominator() <= 0) {
            out_errors.push_back(op_prefix + " has invalid evidence timestamp denominator.");
        }

        if (!std::isfinite(op.parameters.opacity) ||
            op.parameters.opacity < 0.0f || op.parameters.opacity > 1.0f) {
            out_errors.push_back(op_prefix + " has non-finite or out-of-range opacity.");
        }
        if (!std::isfinite(op.evidence.confidence) ||
            op.evidence.confidence < 0.0 || op.evidence.confidence > 1.0) {
            out_errors.push_back(op_prefix + " has non-finite or out-of-range confidence.");
        }
        if (!is_supported_blend_mode(op.parameters.blend_mode)) {
            out_errors.push_back(op_prefix + " has unsupported blend_mode '" + op.parameters.blend_mode + "'.");
        }

        Rational computed_source_out{0, 1};
        if (times_valid) {
            try {
                (void)(op.parameters.timeline_in + op.parameters.duration);
                computed_source_out = op.parameters.source_in + (op.parameters.duration * op.parameters.speed);
            } catch (const std::overflow_error&) {
                out_errors.push_back(op_prefix + " rational time range overflow.");
                times_valid = false;
            }
        }

        if (op.parameters.asset_id.empty()) {
            out_errors.push_back(op_prefix + " missing asset_id.");
        } else {
            const MediaAsset* asset = pool.find_asset(op.parameters.asset_id);
            if (!asset) {
                out_errors.push_back("Asset '" + op.parameters.asset_id + "' not found in MediaPool.");
            } else if (times_valid &&
                       asset->duration().denominator() > 0 &&
                       asset->duration().numerator() > 0 &&
                       computed_source_out > asset->duration()) {
                out_errors.push_back(op_prefix + " source range exceeds asset duration.");
            }
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
    std::unordered_set<std::string> used_item_ids;

    for (size_t i = 0; i < proposal.operations.size(); ++i) {
        const auto& op = proposal.operations[i];
        if (op.operation_type == "insert_clip") {
            const Track* target_track = resolve_track(clone, op.target_ids[0]);
            std::string resolved_track_id = target_track ? target_track->id() : op.target_ids[0];
            TimelineItem item = build_timeline_item_for_op(op, clone, used_item_ids);

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
        const FingerprintDistanceBreakdown bd =
            FingerprintEngine::compute_distance_detailed(result.resulting_fingerprint, *target_reference);
        if (!bd.valid || !std::isfinite(bd.total_distance)) {
            result.is_valid = false;
            result.error_message = "Non-finite reference fingerprint distance during dry-run.";
            return result;
        }
        result.reference_distance = bd.total_distance;
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
    std::unordered_set<std::string> used_item_ids;

    for (const auto& op : proposal.operations) {
        if (op.operation_type == "insert_clip") {
            const Track* target_track = resolve_track(timeline, op.target_ids[0]);
            std::string resolved_track_id = target_track ? target_track->id() : op.target_ids[0];
            TimelineItem item = build_timeline_item_for_op(op, timeline, used_item_ids);
            child_commands.push_back(std::make_unique<InsertClipCommand>(resolved_track_id, std::move(item), CommandOrigin::AI));
        }
    }

    if (child_commands.empty()) {
        return CommandResult::failure(
            CommandStatus::FailedPrecondition,
            "Proposal produced zero executable commands."
        );
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
