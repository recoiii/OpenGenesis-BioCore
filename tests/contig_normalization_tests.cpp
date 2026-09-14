#include "biocore/domain/contig_table.hpp"

#include <cassert>
#include <stdexcept>

int main() {
    using biocore::domain::ContigTableBuilder;
    using biocore::domain::ReferenceAssembly;

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

    assert(table_a.size() == 3U);
    assert(table_a.resolve("1") == table_a.resolve("chr1"));
    assert(table_a.resolve("1") == table_a.resolve("NC_000001.11"));
    assert(table_a.resolve("1") == table_b.resolve("1"));
    assert(table_a.resolve("2") == table_b.resolve("2"));
    assert(table_a.resolve("GL000220.1") == table_b.resolve("GL000220.1"));
    assert(table_a.canonical_name(*table_a.resolve("chr1")) == "1");

    ContigTableBuilder grch37(ReferenceAssembly::grch37);
    grch37.add_contig("1", {"chr1", "NC_000001.10"});
    const auto table37 = grch37.build();
    assert(table37.resolve("NC_000001.10").has_value());
    assert(!table37.resolve("NC_000001.11").has_value());

    bool rejected_ambiguous_alias = false;
    try {
        ContigTableBuilder bad(ReferenceAssembly::custom);
        bad.add_contig("alpha", {"shared"});
        bad.add_contig("beta", {"shared"});
        static_cast<void>(bad.build());
    } catch (const std::invalid_argument&) {
        rejected_ambiguous_alias = true;
    }
    assert(rejected_ambiguous_alias);
    return 0;
}
