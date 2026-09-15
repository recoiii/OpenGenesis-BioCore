#include "biocore/domain/reference_database.hpp"
#include "biocore/domain/variant_types.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace biocore::domain;

void require(const bool condition) {
    if (!condition) {
        throw std::runtime_error("reference database test assertion failed");
    }
}

[[nodiscard]] ContigTable contigs(const ReferenceAssembly assembly) {
    ContigTableBuilder builder{assembly};
    builder.add_contig("1", {"chr1"});
    return builder.build();
}

[[nodiscard]] ReferenceDatabase database(
    std::string id,
    std::string version,
    const ReferenceAssembly assembly,
    std::string custom_id = {}
) {
    const auto table = contigs(assembly);
    const auto chr1 = *table.resolve("1");
    ReferenceDatabaseRecord record{
        .locus = GenomicInterval{chr1, 0U, 1U},
        .reference = Allele{"A", VariantType::unknown, false},
        .alternate = Allele{"G", VariantType::snv, false},
        .record_id = "r",
        .attributes = {}
    };
    ReferenceDatabaseMetadata metadata{
        .database_id = std::move(id),
        .display_name = "Database",
        .version = std::move(version),
        .schema_version = 1U,
        .assembly = ReferenceAssemblyIdentity{assembly, std::move(custom_id)},
        .source_uri = "file:///db.tsv",
        .source_sha256 = std::string(64U, 'c')
    };
    return ReferenceDatabase::build(std::move(metadata), table, {record});
}
}  // namespace

int main() {
    ReferenceDatabaseRegistry registry;
    registry.register_database(database("db", "2.0", ReferenceAssembly::grch38));
    const auto* stable = registry.find("db", "2.0", ReferenceAssemblyIdentity{ReferenceAssembly::grch38, {}});
    require(stable != nullptr);
    registry.register_database(database("db", "1.0", ReferenceAssembly::grch38));
    registry.register_database(database("other", "1.0", ReferenceAssembly::custom, "custom-a"));
    require(registry.size() == 3U);
    require(stable->metadata().version == "2.0");

    const auto listed = registry.list_metadata();
    require(listed.size() == 3U);
    require(listed[0U].database_id == "db" && listed[0U].version == "1.0");
    require(listed[1U].database_id == "db" && listed[1U].version == "2.0");
    require(listed[2U].database_id == "other");

    require(registry.find("missing", "1.0", ReferenceAssemblyIdentity{ReferenceAssembly::grch38, {}}) == nullptr);
    require(registry.find("db", "2.0", ReferenceAssemblyIdentity{ReferenceAssembly::grch37, {}}) == nullptr);

    bool duplicate = false;
    try {
        registry.register_database(database("db", "2.0", ReferenceAssembly::grch38));
    } catch (const std::invalid_argument&) {
        duplicate = true;
    }
    require(duplicate);
}
