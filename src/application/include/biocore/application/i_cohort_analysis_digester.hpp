#pragma once

#include <string>
#include <string_view>

namespace biocore::application {

class ICohortAnalysisDigester {
public:
    virtual ~ICohortAnalysisDigester() = default;
    [[nodiscard]] virtual std::string sha256(std::string_view canonical_bytes) = 0;
};

}  // namespace biocore::application
