#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "biocore/presentation/cohort_workspace_json.hpp"

namespace {
using namespace biocore;

void check(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

application::CohortDefinition fixture() {
    application::CohortDefinition cohort;
    cohort.project_id = "project-097";
    cohort.cohort_id = "cohort-097";
    cohort.name = "Known \"case/control\"";
    cohort.current_revision = 2U;
    cohort.created_at_utc = "2026-10-02T20:00:00Z";
    cohort.updated_at_utc = "2026-10-02T20:10:00Z";
    cohort.revision.project_id = cohort.project_id;
    cohort.revision.cohort_id = cohort.cohort_id;
    cohort.revision.revision = 2U;
    cohort.revision.parent_revision = 1U;
    cohort.revision.created_at_utc = cohort.updated_at_utc;
    cohort.revision.members = {
        {
            .sample_id = "case-1",
            .sample_display_name = "Case 1",
            .sample_group_metadata = "case",
            .biological_unit_id = "person-1",
            .group = application::CohortGroup::case_group,
            .disposition = application::CohortMemberDisposition::included,
            .exclusion_reason = std::nullopt,
        },
        {
            .sample_id = "control-1",
            .sample_display_name = "Control\n1",
            .sample_group_metadata = "control",
            .biological_unit_id = "person-2",
            .group = application::CohortGroup::control,
            .disposition = application::CohortMemberDisposition::excluded,
            .exclusion_reason = std::string{"QC"},
        },
    };
    return cohort;
}

}  // namespace

int main() {
    try {
        const auto cohort = fixture();
        const auto json = presentation::render_cohort_definition(cohort);
        check(json.find("\"projectId\":\"project-097\"") != std::string::npos,
              "project identity missing");
        check(json.find("\"currentRevision\":2") != std::string::npos,
              "revision missing");
        check(json.find("\"parentRevision\":1") != std::string::npos,
              "parent revision missing");
        check(json.find("Known \\\"case/control\\\"") != std::string::npos,
              "quote escaping missing");
        check(json.find("Control\\n1") != std::string::npos,
              "newline escaping missing");
        check(json.find("\"exclusionReason\":\"QC\"") != std::string::npos,
              "exclusion provenance missing");

        const auto list = presentation::render_cohort_list({cohort});
        check(list.starts_with("{\"cohorts\":["), "cohort list envelope missing");
        check(list.find("\"cohortId\":\"cohort-097\"") != std::string::npos,
              "cohort list identity missing");

        const auto create = presentation::parse_create_cohort_request(
            R"({"name":"AD cohort","members":[{"sampleId":"case-1","biologicalUnitId":"person-1","group":"case"},{"sampleId":"control-1","biologicalUnitId":"person-2","group":"control","disposition":"excluded","exclusionReason":"QC"}]})"
        );
        check(create.name == "AD cohort" && create.members.size() == 2U,
              "create cohort parser lost identity");
        check(create.members[0].group == application::CohortGroup::case_group &&
              create.members[0].disposition == application::CohortMemberDisposition::included,
              "create cohort parser lost default inclusion");
        check(create.members[1].disposition == application::CohortMemberDisposition::excluded &&
              create.members[1].exclusion_reason == std::optional<std::string>{"QC"},
              "create cohort parser lost explicit exclusion");

        const auto revise = presentation::parse_revise_cohort_request(
            R"({"expectedRevision":2,"members":[{"sampleId":"case-1","biologicalUnitId":"person-1","group":"case"}]})"
        );
        check(revise.expected_revision == 2U && revise.members.size() == 1U,
              "revision parser lost optimistic concurrency token");

        bool rejected = false;
        try {
            static_cast<void>(presentation::parse_create_cohort_request(
                R"({"name":"bad","members":[{"sampleId":"case-1","biologicalUnitId":"person-1","group":"case","unknown":1}]})"
            ));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "unknown cohort JSON field must be rejected");
        std::cout << "PASS cohort workspace json\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
