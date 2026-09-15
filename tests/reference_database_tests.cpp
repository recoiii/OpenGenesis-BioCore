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

[[nodiscard]] ContigTable make_contigs() {
    ContigTableBuilder builder{ReferenceAssembly::grch38};
    builder.add_contig("1", {"chr1"});
    builder.add_contig("2", {"chr2"});
    return builder.build();
}

[[nodiscard]] ReferenceDatabaseMetadata make_metadata() {
    return ReferenceDatabaseMetadata{
        .database_id = "test.db",
        .display_name = "Test reference database",
        .version = "1.0.0",
        .schema_version = 1U,
        .assembly = ReferenceAssemblyIdentity{ReferenceAssembly::grch38, {}},
        .source_uri = "https://example.invalid/test.tsv",
        .source_sha256 = std::string(64U, 'a')
    };
}

[[nodiscard]] ReferenceDatabaseRecord make_record(
    const ContigId contig,
    const std::uint64_t start,
    std::string ref,
    std::string alt,
    std::string id
) {
    const std::uint64_t end = start + ref.size();
    return ReferenceDatabaseRecord{
        .locus = GenomicInterval{contig, start, end},
        .reference = Allele{ref, VariantType::unknown, false},
        .alternate = Allele{alt, classify_variant(ref, alt, false), false},
        .record_id = std::move(id),
        .attributes = {
            ReferenceDatabaseAttribute{"zeta", std::string{"last"}},
            ReferenceDatabaseAttribute{"alpha", std::string{"first"}}
        }
    };
}

[[nodiscard]] VariantRecord make_query(const ContigId contig, const std::uint64_t start) {
    VariantRecord variant;
    variant.locus = GenomicInterval{contig, start, start + 1U};
    variant.reference = Allele{"A", VariantType::unknown, false};
    variant.alternates.push_back(Allele{"G", VariantType::snv, false});
    return variant;
}

template <typename Callable>
void expect_invalid(Callable&& callable) {
    bool thrown = false;
    try {
        callable();
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    require(thrown);
}

}  // namespace

int main() {
    const auto contigs = make_contigs();
    const auto chr1 = *contigs.resolve("1");
    const auto chr2 = *contigs.resolve("2");

    auto database = ReferenceDatabase::build(
        make_metadata(),
        contigs,
        {
            make_record(chr2, 50U, "A", "G", "r3"),
            make_record(chr1, 20U, "A", "G", "r2"),
            make_record(chr1, 10U, "A", "G", "r1")
        }
    );
    require(database.size() == 3U);
    require(database.record(0U).record_id == "r1");
    require(database.record(1U).record_id == "r2");
    require(database.record(2U).record_id == "r3");
    require(database.record(0U).attributes.at(0U).key == "alpha");
    require(database.record(0U).attributes.at(1U).key == "zeta");

    const ReferenceAssemblyIdentity grch38{ReferenceAssembly::grch38, {}};
    const auto overlap = database.query_overlap(grch38, contigs, chr1, 9U, 21U);
    require((overlap == std::vector<std::size_t>{0U, 1U}));

    const auto query = make_query(chr1, 10U);
    const auto exact = database.query_exact(grch38, contigs, query, 0U);
    require((exact == std::vector<std::size_t>{0U}));

    VariantRecord absent = query;
    absent.alternates[0U] = Allele{"T", VariantType::snv, false};
    require(database.query_exact(grch38, contigs, absent, 0U).empty());

    expect_invalid([&] {
        (void)database.query_overlap(ReferenceAssemblyIdentity{ReferenceAssembly::grch37, {}}, contigs, chr1, 0U, 100U);
    });

    ContigTableBuilder subset_builder{ReferenceAssembly::grch38};
    subset_builder.add_contig("2", {"chr2"});
    const ContigTable subset_contigs = subset_builder.build();
    const ContigId subset_chr2 = *subset_contigs.resolve("2");
    auto subset_database = ReferenceDatabase::build(
        make_metadata(),
        subset_contigs,
        {make_record(subset_chr2, 100U, "A", "G", "subset")}
    );
    VariantRecord cross_table_query = make_query(chr2, 100U);
    const auto cross_table_exact = subset_database.query_exact(grch38, contigs, cross_table_query, 0U);
    require((cross_table_exact == std::vector<std::size_t>{0U}));

    auto duplicate_records = std::vector<ReferenceDatabaseRecord>{
        make_record(chr1, 1U, "A", "C", "dup"),
        make_record(chr1, 2U, "A", "G", "dup")
    };
    expect_invalid([&] { (void)ReferenceDatabase::build(make_metadata(), contigs, duplicate_records); });

    auto bad_metadata = make_metadata();
    bad_metadata.source_sha256 = std::string(64U, 'A');
    expect_invalid([&] { validate_reference_database_metadata(bad_metadata); });

    bad_metadata = make_metadata();
    bad_metadata.assembly = ReferenceAssemblyIdentity{ReferenceAssembly::custom, {}};
    expect_invalid([&] { validate_reference_database_metadata(bad_metadata); });

    auto bad_record = make_record(chr1, 3U, "A", "G", "bad");
    bad_record.attributes.push_back(ReferenceDatabaseAttribute{"alpha", std::string{"duplicate"}});
    expect_invalid([&] { (void)ReferenceDatabase::build(make_metadata(), contigs, {bad_record}); });

    bool out_of_range = false;
    try {
        (void)database.record(3U);
    } catch (const std::out_of_range&) {
        out_of_range = true;
    }
    require(out_of_range);
}
