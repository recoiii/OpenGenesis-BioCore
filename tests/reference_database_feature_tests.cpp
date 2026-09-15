#include "biocore/domain/reference_database.hpp"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace biocore::domain;

void require(const bool condition) {
    if (!condition) throw std::runtime_error("reference feature test assertion failed");
}

[[nodiscard]] ContigTable contigs() {
    ContigTableBuilder builder{ReferenceAssembly::grch38};
    builder.add_contig("1", {"chr1"});
    builder.add_contig("2", {"chr2"});
    return builder.build();
}

[[nodiscard]] ReferenceDatabaseMetadata metadata(std::string id) {
    return ReferenceDatabaseMetadata{
        .database_id = std::move(id),
        .display_name = "Feature fixture",
        .version = "1.0",
        .schema_version = 1U,
        .assembly = ReferenceAssemblyIdentity{ReferenceAssembly::grch38, {}},
        .source_uri = "file:///features",
        .source_sha256 = std::string(64U, 'e')
    };
}

template <typename Callable>
void expect_invalid(Callable&& callable) {
    bool thrown = false;
    try { callable(); } catch (const std::invalid_argument&) { thrown = true; }
    require(thrown);
}
}  // namespace

int main() {
    std::istringstream gff{
        "##gff-version 3\n"
        "chr1\tsrc\tgene\t11\t20\t.\t+\t.\tID=gene1;Name=GENE1\n"
        "1\tsrc\texon\t15\t18\t3.5\t+\t0\tParent=gene1\n"
    };
    auto gff_db = ReferenceDatabase::from_gff3(gff, metadata("gff"), contigs());
    require(gff_db.variant_size() == 0U);
    require(gff_db.feature_size() == 2U);
    require(gff_db.size() == 2U);
    const auto table = contigs();
    const auto chr1 = *table.resolve("1");
    const ReferenceAssemblyIdentity grch38{ReferenceAssembly::grch38, {}};
    const auto hits = gff_db.query_feature_overlap(grch38, table, chr1, 14U, 16U);
    require((hits == std::vector<std::size_t>{0U, 1U}));
    require(gff_db.feature(0U).locus.start == 10U && gff_db.feature(0U).locus.end == 20U);
    require(gff_db.feature(0U).feature_type == "gene");
    require(gff_db.feature(0U).attributes.at(0U).key == "ID");
    require(*gff_db.feature(0U).attributes.at(0U).value == "gene1");

    std::istringstream gtf{
        "chr1\tens\ttranscript\t101\t200\t.\t-\t.\tgene_id \"G1\"; transcript_id \"T1\"; tag \"basic\"; tag \"CCDS\";\n"
    };
    auto gtf_db = ReferenceDatabase::from_gtf(gtf, metadata("gtf"), table);
    require(gtf_db.feature_size() == 1U);
    require(gtf_db.feature(0U).locus.start == 100U && gtf_db.feature(0U).locus.end == 200U);
    require(gtf_db.feature(0U).strand == '-');
    require(gtf_db.feature(0U).attributes.size() == 4U);

    std::istringstream bad_coord{
        "chr1\tsrc\tgene\t0\t10\t.\t+\t.\tID=x\n"
    };
    expect_invalid([&] { (void)ReferenceDatabase::from_gff3(bad_coord, metadata("bad1"), table); });

    std::istringstream bad_phase{
        "chr1\tsrc\tCDS\t1\t10\t.\t+\t3\tID=x\n"
    };
    expect_invalid([&] { (void)ReferenceDatabase::from_gff3(bad_phase, metadata("bad2"), table); });

    std::istringstream bad_gtf{
        "chr1\tsrc\tgene\t1\t10\t.\t+\t.\tgene_id G1;\n"
    };
    expect_invalid([&] { (void)ReferenceDatabase::from_gtf(bad_gtf, metadata("bad3"), table); });
}
