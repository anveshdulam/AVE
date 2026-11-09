#pragma once

#include <ave/time.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ave {

/**
 * @brief Provenance evidence justifying why an edit operation was proposed.
 */
struct ProposalEvidence {
    std::string justified_by;         // "beat", "downbeat", "speech_pause", "reference_match"
    Rational timestamp;               // Exact rational time of trigger
    std::string reference_shot_id;    // Reference shot matched, if any
    double confidence = 1.0;
    std::string model_version = "v1.0.0";
};

/**
 * @brief Parameters for an individual edit operation.
 */
struct EditOperationParams {
    std::string clip_id;
    std::string asset_id;
    std::string clip_name;
    Rational timeline_in{0, 1};
    Rational duration{0, 1};
    Rational source_in{0, 1};
    Rational speed{1, 1};
    float opacity{1.0f};
    std::string blend_mode{"Over"};
};

/**
 * @brief A single typed atomic edit operation within an edit proposal envelope.
 */
struct EditOperation {
    std::string operation_type;       // "insert_clip", "delete_clip", "split_clip", "transform_clip"
    std::vector<std::string> target_ids; // e.g. track_id or item_id
    EditOperationParams parameters;
    bool requires_approval = true;
    ProposalEvidence evidence;
};

/**
 * @brief Envelope containing an AI or script-generated edit proposal.
 * Matches schemas/edit-proposal-envelope.schema.json.
 */
struct EditProposalEnvelope {
    int schema_version = 1;
    std::string proposal_id;
    std::string project_id;
    std::string source = "ai";        // "ai", "user", "import", "script"
    std::vector<EditOperation> operations;
    std::vector<std::string> warnings;

    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static EditProposalEnvelope from_json(const std::string& json_str);
};

} // namespace ave
