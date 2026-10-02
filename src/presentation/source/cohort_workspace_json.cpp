#include "biocore/presentation/cohort_workspace_json.hpp"

#include <string>
#include <string_view>

namespace biocore::presentation {
namespace {

[[nodiscard]] std::string quote(const std::string_view value) {
    std::string out{"\""};
    out.reserve(value.size() + 2U);
    for (const unsigned char character : value) {
        switch (character) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (character < 0x20U) {
                    static constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(character >> 4U) & 0x0fU]);
                    out.push_back(hex[character & 0x0fU]);
                } else {
                    out.push_back(static_cast<char>(character));
                }
        }
    }
    out.push_back('"');
    return out;
}

[[nodiscard]] std::string optional_string(
    const std::optional<std::string>& value
) {
    return value.has_value() ? quote(*value) : "null";
}

[[nodiscard]] std::string render_member(
    const application::CohortMemberSnapshot& member
) {
    return "{\"sampleId\":" + quote(member.sample_id) +
           ",\"displayName\":" + quote(member.sample_display_name) +
           ",\"sampleGroupMetadata\":" + quote(member.sample_group_metadata) +
           ",\"biologicalUnitId\":" + quote(member.biological_unit_id) +
           ",\"group\":" + quote(application::to_string(member.group)) +
           ",\"disposition\":" + quote(application::to_string(member.disposition)) +
           ",\"exclusionReason\":" + optional_string(member.exclusion_reason) + "}";
}

}  // namespace

std::string render_cohort_definition(
    const application::CohortDefinition& cohort
) {
    std::string body =
        "{\"projectId\":" + quote(cohort.project_id) +
        ",\"cohortId\":" + quote(cohort.cohort_id) +
        ",\"name\":" + quote(cohort.name) +
        ",\"currentRevision\":" + std::to_string(cohort.current_revision) +
        ",\"createdAtUtc\":" + quote(cohort.created_at_utc) +
        ",\"updatedAtUtc\":" + quote(cohort.updated_at_utc) +
        ",\"revision\":{\"revision\":" +
        std::to_string(cohort.revision.revision) +
        ",\"parentRevision\":";
    if (cohort.revision.parent_revision.has_value()) {
        body += std::to_string(*cohort.revision.parent_revision);
    } else {
        body += "null";
    }
    body += ",\"createdAtUtc\":" + quote(cohort.revision.created_at_utc) +
            ",\"members\":[";
    for (std::size_t index = 0U; index < cohort.revision.members.size(); ++index) {
        if (index != 0U) body += ',';
        body += render_member(cohort.revision.members[index]);
    }
    body += "]}}";
    return body;
}

std::string render_cohort_list(
    const std::vector<application::CohortDefinition>& cohorts
) {
    std::string body{"{\"cohorts\":["};
    for (std::size_t index = 0U; index < cohorts.size(); ++index) {
        if (index != 0U) body += ',';
        body += render_cohort_definition(cohorts[index]);
    }
    body += "]}";
    return body;
}

}  // namespace biocore::presentation
