#include "biocore/domain/vcf_ingestion.hpp"
#include "vcf_ingestion_test_support.hpp"

#include <iostream>
#include <sstream>
#include <variant>

namespace {

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "vcf_ingestion_missing_tests: " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

bool run_vcf_ingestion_missing_tests(const biocore::domain::ReferenceGenome& genome) {
    using biocore::domain::NullableVariantList;
    using biocore::domain::ingest_vcf;

    bool ok = true;
    std::istringstream input{
        "##fileformat=VCFv4.5\n"
        "##INFO=<ID=AF,Number=A,Type=Float,Description=\"Allele frequencies\">\n"
        "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
        "##FORMAT=<ID=AD,Number=R,Type=Integer,Description=\"Allele depth\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tsample-A\n"
        "1\t7\t.\tC\tT,G\t.\t.\tAF=0.2,.\tGT:AD\t0/1:5,.,2\n"
    };
    const auto parsed = ingest_vcf(input, genome);
    ok = require(parsed.records.size() == 1U, "record count") && ok;
    if (parsed.records.empty()) return false;

    const auto& record = parsed.records.front();
    ok = require(!record.filters_applied && record.filters.empty(), "FILTER dot preserved") && ok;
    ok = require(!record.variant.info.allele_frequencies.has_value(), "partial AF avoids lossy fast path") && ok;
    ok = require(record.variant.info.extra_fields.size() == 1U, "partial AF retained as typed extra") && ok;
    if (!record.variant.info.extra_fields.empty()) {
        const auto* values = std::get_if<NullableVariantList<float>>(&record.variant.info.extra_fields[0U].value);
        ok = require(values != nullptr && values->size() == 2U && (*values)[0U].has_value()
                && !(*values)[1U].has_value(), "partial AF missing element preserved") && ok;
    }
    ok = require(record.samples.size() == 1U && record.samples[0U].genotype.allele_depths.empty(), "partial AD avoids lossy fast path") && ok;
    ok = require(record.samples[0U].extra_format_fields.size() == 1U, "partial AD retained as typed extra") && ok;
    return ok;
}
