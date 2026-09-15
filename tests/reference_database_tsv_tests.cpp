#include "biocore/domain/reference_database.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace biocore::domain;

void require(const bool condition) {
    if (!condition) {
        throw std::runtime_error("reference database test assertion failed");
    }
}

[[nodiscard]] ContigTable contigs() {
    ContigTableBuilder builder{ReferenceAssembly::grch38};
    builder.add_contig("1", {"chr1"});
    return builder.build();
}

[[nodiscard]] ReferenceDatabaseMetadata metadata() {
    return ReferenceDatabaseMetadata{
        .database_id = "fixture",
        .display_name = "Fixture",
        .version = "2026.09",
        .schema_version = 1U,
        .assembly = ReferenceAssemblyIdentity{ReferenceAssembly::grch38, {}},
        .source_uri = "file:///fixture.tsv",
        .source_sha256 = std::string(64U, 'b')
    };
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
    std::istringstream input{
        "#OpenGenesis-BioCore-ReferenceDB\t1\n"
        "contig\tstart\tend\tref\talt\trecord_id\tattr:gene\tattr:clinical\n"
        "chr1\t10\t11\tA\tG\trs1\tGENE1\tpathogenic\n"
        "1\t20\t21\tC\tT\trs2\tGENE2\t.\n"
    };
    auto database = ReferenceDatabase::from_tsv(input, metadata(), contigs());
    require(database.size() == 2U);
    require(database.record(0U).record_id == "rs1");
    require(database.record(0U).attributes.at(0U).key == "clinical");
    require(*database.record(0U).attributes.at(0U).value == "pathogenic");
    require(database.record(1U).attributes.at(0U).value == std::nullopt);
    require(database.record(1U).attributes.at(1U).key == "gene");
    require(*database.record(1U).attributes.at(1U).value == "GENE2");

    std::istringstream bad_magic{
        "#wrong\ncontig\tstart\tend\tref\talt\trecord_id\n1\t0\t1\tA\tG\tr\n"
    };
    expect_invalid([&] { (void)ReferenceDatabase::from_tsv(bad_magic, metadata(), contigs()); });

    std::istringstream unknown_contig{
        "#OpenGenesis-BioCore-ReferenceDB\t1\n"
        "contig\tstart\tend\tref\talt\trecord_id\n"
        "2\t0\t1\tA\tG\tr\n"
    };
    expect_invalid([&] { (void)ReferenceDatabase::from_tsv(unknown_contig, metadata(), contigs()); });

    std::istringstream bad_coordinate{
        "#OpenGenesis-BioCore-ReferenceDB\t1\n"
        "contig\tstart\tend\tref\talt\trecord_id\n"
        "1\tx\t1\tA\tG\tr\n"
    };
    expect_invalid([&] { (void)ReferenceDatabase::from_tsv(bad_coordinate, metadata(), contigs()); });

    std::istringstream duplicate_attributes{
        "#OpenGenesis-BioCore-ReferenceDB\t1\n"
        "contig\tstart\tend\tref\talt\trecord_id\tattr:gene\tattr:gene\n"
        "1\t0\t1\tA\tG\tr\tx\ty\n"
    };
    expect_invalid([&] { (void)ReferenceDatabase::from_tsv(duplicate_attributes, metadata(), contigs()); });

    std::istringstream lowercase_allele{
        "#OpenGenesis-BioCore-ReferenceDB\t1\n"
        "contig\tstart\tend\tref\talt\trecord_id\n"
        "1\t0\t1\ta\tG\tr\n"
    };
    expect_invalid([&] { (void)ReferenceDatabase::from_tsv(lowercase_allele, metadata(), contigs()); });
}
