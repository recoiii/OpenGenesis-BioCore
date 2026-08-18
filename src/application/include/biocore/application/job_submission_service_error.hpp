#pragma once

#include <stdexcept>

namespace biocore::application {

enum class JobSubmissionErrorCode {
    pipeline_not_found,
    identifier_generation_exhausted,
    persistence_failed
};

class JobSubmissionError final : public std::runtime_error {
public:
    JobSubmissionError(JobSubmissionErrorCode code, std::string message);
    [[nodiscard]] JobSubmissionErrorCode code() const noexcept;
private:
    JobSubmissionErrorCode code_;
};

}  // namespace biocore::application
