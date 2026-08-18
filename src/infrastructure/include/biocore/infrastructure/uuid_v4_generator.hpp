#pragma once

#include "biocore/application/i_id_generator.hpp"

namespace biocore::infrastructure {

class UuidV4Generator final : public application::IIdGenerator {
public:
    [[nodiscard]] std::string generate() override;
};

}  // namespace biocore::infrastructure
