#include "biocore/application/build_info.hpp"

#include "biocore/build_config.hpp"

namespace biocore::application {

std::string_view BuildInfo::version() noexcept {
    return BIOCORE_VERSION_STRING;
}

}  // namespace biocore::application
