#pragma once

#include <ave/command.hpp>
#include <ave/edit_proposal.hpp>
#include <ave/media_pool.hpp>
#include <ave/reference_fingerprint.hpp>
#include <ave/timeline.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ave {

/**
 * @brief Diagnostic outcome of a non-destructive dry-run simulation on a cloned timeline.
 */
struct DryRunResult {
    bool is_valid = false;
    std::string error_message;
    size_t operations_count = 0;
    Rational resulting_duration{0, 1};
    ReferenceFingerprint resulting_fingerprint;
    double reference_distance = 0.0;
};

/**
 * @brief Hardened security and validation bridge connecting AI proposals to the native Command Bus.
 *
 * Enforces:
 * 1. Floats stop at the boundary: all timestamps strictly snap to exact ave::Rational.
 * 2. Schema check and semantic validation against MediaPool and Timeline models.
 * 3. Dry-run execution on cloned timeline state using Phase 4 transactions.
 * 4. Human-in-the-loop diff preview and approval.
 * 5. Single atomic CompositeCommand execution on CommandHistory for 1-step undo/redo.
 */
class ProposalBridge {
public:
    ProposalBridge() = default;

    /**
     * @brief Performs semantic validation on an edit proposal envelope.
     */
    [[nodiscard]] static bool validate(
        const EditProposalEnvelope& proposal,
        const Timeline& timeline,
        const MediaPool& pool,
        std::vector<std::string>& out_errors
    );

    /**
     * @brief Simulates proposal application on an isolated cloned timeline without mutating project state.
     */
    [[nodiscard]] static DryRunResult dry_run(
        const EditProposalEnvelope& proposal,
        const Timeline& timeline,
        const MediaPool& pool,
        const AudioRhythmReport& rhythm,
        const ReferenceFingerprint* target_reference = nullptr
    );

    /**
     * @brief Converts an approved proposal into a single atomic CompositeCommand and executes it.
     */
    static CommandResult commit(
        const EditProposalEnvelope& proposal,
        Timeline& timeline,
        MediaPool& pool,
        CommandHistory& history
    );

    /**
     * @brief Generates a readable human-facing diff summary of the proposed operations.
     */
    [[nodiscard]] static std::string generate_diff_summary(
        const EditProposalEnvelope& proposal,
        const Timeline& timeline
    );
};

} // namespace ave
