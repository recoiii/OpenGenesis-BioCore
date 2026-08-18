#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace biocore::domain {

class Project final {
public:
    static constexpr std::size_t maximum_id_length = 128U;
    static constexpr std::size_t maximum_name_length = 200U;

    Project(
        std::string id,
        std::string name,
        std::string root_path,
        std::string created_at_utc,
        std::string updated_at_utc
    );

    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view root_path() const noexcept;
    [[nodiscard]] std::string_view created_at_utc() const noexcept;
    [[nodiscard]] std::string_view updated_at_utc() const noexcept;

private:
    std::string id_;
    std::string name_;
    std::string root_path_;
    std::string created_at_utc_;
    std::string updated_at_utc_;
};

}  // namespace biocore::domain
