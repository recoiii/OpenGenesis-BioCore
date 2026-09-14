#include "biocore/domain/contig_table.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "contig_normalization_tests: " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    using biocore::domain::ContigTableBuilder;
    using biocore::domain::ReferenceAssembly;

    bool ok = true;

    ContigTableBuilder first(ReferenceAssembly::grch38);
    first.add_contig("2", {"chr2", "NC_000002.12"});
    first.add_contig("1", {"chr1", "NC_000001.11"});
    first.add_unknown_contig("GL000220.1");
    const auto table_a = first.build();

    ContigTableBuilder second(ReferenceAssembly::grch38);
    second.add_unknown_contig("GL000220.1");
    second.add_contig("1", {"NC_000001.11", "chr1"});
    second.add_contig("2", {"NC_000002.12", "chr2"});
    const auto table_b = second.build();

    ok = require(table_a.size() == 3U, "contig count") && ok;
    ok = require(table_a.resolve("1") == table_a.resolve("chr1"), "chr alias equivalence") && ok;
    ok = require(
        table_a.resolve("1") == table_a.resolve("NC_000001.11"),
        "GRCh38 accession alias equivalence"
    ) && ok;
    ok = require(table_a.resolve("1") == table_b.resolve("1"), "deterministic ID for contig 1") && ok;
    ok = require(table_a.resolve("2") == table_b.resolve("2"), "deterministic ID for contig 2") && ok;
    ok = require(
        table_a.resolve("GL000220.1") == table_b.resolve("GL000220.1"),
        "deterministic ID for unknown contig"
    ) && ok;
    const auto chr1 = table_a.resolve("chr1");
    ok = require(chr1.has_value(), "chr1 alias resolves") && ok;
    if (chr1.has_value()) {
        ok = require(table_a.canonical_name(*chr1) == "1", "canonical contig name") && ok;
    }

    ContigTableBuilder grch37(ReferenceAssembly::grch37);
    grch37.add_contig("1", {"chr1", "NC_000001.10"});
    const auto table37 = grch37.build();
    ok = require(table37.resolve("NC_000001.10").has_value(), "GRCh37 accession resolves") && ok;
    ok = require(!table37.resolve("NC_000001.11").has_value(), "GRCh38 accession rejected in GRCh37 table") && ok;

    bool rejected_ambiguous_alias = false;
    try {
        ContigTableBuilder bad(ReferenceAssembly::custom);
        bad.add_contig("alpha", {"shared"});
        bad.add_contig("beta", {"shared"});
        static_cast<void>(bad.build());
    } catch (const std::invalid_argument&) {
        rejected_ambiguous_alias = true;
    }
    ok = require(rejected_ambiguous_alias, "ambiguous alias rejected") && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
