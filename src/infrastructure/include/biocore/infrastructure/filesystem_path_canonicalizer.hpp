#pragma once

#include "biocore/application/i_path_canonicalizer.hpp"

namespace biocore::infrastructure {

class FilesystemPathCanonicalizer final : public application::IPathCanonicalizer {
public:
    [[nodiscard]] std::string canonicalize_existing_directory(std::string_view path) override;
};

}  // namespace biocore::infrastructure
