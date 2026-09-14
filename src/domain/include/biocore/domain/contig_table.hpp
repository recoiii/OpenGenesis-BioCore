#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace biocore::domain {

using ContigId = std::uint32_t;

enum class ReferenceAssembly : std::uint8_t {
    unspecified,
    grch37,
    grch38,
    custom
};

struct ContigDefinition final {
    std::string canonical_name;
    std::vector<std::string> aliases;
};

struct TransparentStringHash final {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept;
};

class ContigTable final {
public:
    ContigTable() = default;

    [[nodiscard]] ReferenceAssembly assembly() const noexcept { return assembly_; }
    [[nodiscard]] std::size_t size() const noexcept { return canonical_names_.size(); }
    [[nodiscard]] std::optional<ContigId> resolve(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::string_view> canonical_name(ContigId id) const noexcept;

private:
    friend class ContigTableBuilder;

    ReferenceAssembly assembly_{ReferenceAssembly::unspecified};
    std::vector<std::string> canonical_names_;
    std::unordered_map<std::string, ContigId, TransparentStringHash, std::equal_to<>> aliases_;
};

class ContigTableBuilder final {
public:
    explicit ContigTableBuilder(ReferenceAssembly assembly = ReferenceAssembly::unspecified) noexcept
        : assembly_(assembly) {}

    void add_contig(std::string canonical_name, std::vector<std::string> aliases = {});
    void add_unknown_contig(std::string name);
    [[nodiscard]] ContigTable build() const;

private:
    ReferenceAssembly assembly_;
    std::vector<ContigDefinition> definitions_;
};

}  // namespace biocore::domain
