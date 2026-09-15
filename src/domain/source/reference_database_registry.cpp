#include "biocore/domain/reference_database.hpp"

#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace biocore::domain {
namespace {

[[nodiscard]] auto database_sort_key(const ReferenceDatabaseMetadata& metadata) {
    return std::tuple{
        metadata.database_id,
        metadata.version,
        static_cast<unsigned int>(metadata.assembly.assembly),
        metadata.assembly.custom_id
    };
}

}  // namespace

void ReferenceDatabaseRegistry::register_database(ReferenceDatabase database) {
    const auto key = database_sort_key(database.metadata());
    const auto found = std::lower_bound(
        databases_.begin(),
        databases_.end(),
        key,
        [](const std::unique_ptr<ReferenceDatabase>& candidate, const auto& expected_key) {
            return database_sort_key(candidate->metadata()) < expected_key;
        }
    );
    if (found != databases_.end() && database_sort_key((*found)->metadata()) == key) {
        throw std::invalid_argument("duplicate reference database identity/version/assembly registration");
    }
    databases_.insert(found, std::make_unique<ReferenceDatabase>(std::move(database)));
}

const ReferenceDatabase* ReferenceDatabaseRegistry::find(
    const std::string_view database_id,
    const std::string_view version,
    const ReferenceAssemblyIdentity& assembly
) const noexcept {
    for (const auto& database : databases_) {
        const auto& metadata = database->metadata();
        if (metadata.database_id == database_id && metadata.version == version
            && metadata.assembly == assembly) {
            return database.get();
        }
    }
    return nullptr;
}

std::vector<ReferenceDatabaseMetadata> ReferenceDatabaseRegistry::list_metadata() const {
    std::vector<ReferenceDatabaseMetadata> metadata;
    metadata.reserve(databases_.size());
    for (const auto& database : databases_) {
        metadata.push_back(database->metadata());
    }
    return metadata;
}

}  // namespace biocore::domain
