#include "biocore/domain/contig_table.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace biocore::domain {

std::size_t TransparentStringHash::operator()(const std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
}

std::optional<ContigId> ContigTable::resolve(const std::string_view name) const noexcept {
    const auto found = aliases_.find(name);
    if (found == aliases_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<std::string_view> ContigTable::canonical_name(const ContigId id) const noexcept {
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= canonical_names_.size()) {
        return std::nullopt;
    }
    return std::string_view(canonical_names_[index]);
}

void ContigTableBuilder::add_contig(std::string canonical_name, std::vector<std::string> aliases) {
    if (canonical_name.empty()) {
        throw std::invalid_argument("canonical contig name must not be empty");
    }
    definitions_.push_back(ContigDefinition{std::move(canonical_name), std::move(aliases)});
}

void ContigTableBuilder::add_unknown_contig(std::string name) {
    add_contig(std::move(name));
}

ContigTable ContigTableBuilder::build() const {
    std::vector<ContigDefinition> sorted = definitions_;
    std::sort(
        sorted.begin(),
        sorted.end(),
        [](const ContigDefinition& left, const ContigDefinition& right) {
            return left.canonical_name < right.canonical_name;
        }
    );

    if (sorted.size() > static_cast<std::size_t>(std::numeric_limits<ContigId>::max())) {
        throw std::overflow_error("contig table exceeds ContigId capacity");
    }

    ContigTable table;
    table.assembly_ = assembly_;
    table.canonical_names_.reserve(sorted.size());
    table.aliases_.reserve(sorted.size() * 3U);

    for (std::size_t index = 0U; index < sorted.size(); ++index) {
        const auto& definition = sorted[index];
        if (index > 0U && sorted[index - 1U].canonical_name == definition.canonical_name) {
            throw std::invalid_argument("duplicate canonical contig name: " + definition.canonical_name);
        }
        const ContigId id = static_cast<ContigId>(index);
        table.canonical_names_.push_back(definition.canonical_name);

        const auto register_alias = [&table, id](const std::string& alias) {
            if (alias.empty()) {
                throw std::invalid_argument("contig alias must not be empty");
            }
            const auto [iterator, inserted] = table.aliases_.emplace(alias, id);
            if (!inserted && iterator->second != id) {
                throw std::invalid_argument("contig alias maps to more than one canonical contig: " + alias);
            }
        };

        register_alias(definition.canonical_name);
        for (const std::string& alias : definition.aliases) {
            register_alias(alias);
        }
    }
    return table;
}

}  // namespace biocore::domain
