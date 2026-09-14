#include "biocore/domain/vcf_ingestion.hpp"
#include "vcf_ingestion_test_support.hpp"

#include <iostream>
#include <sstream>
#include <variant>

namespace {
[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) { std::cerr << "vcf_ingestion_info_tests: " << message << '\n'; return false; }
    return true;
}
}

bool run_vcf_ingestion_info_tests(const biocore::domain::ReferenceGenome& genome) {
    using biocore::domain::NullableVariantList;
    using biocore::domain::VariantType;
    using biocore::domain::ingest_vcf;
    bool ok = true;
    std::istringstream input{
        "##fileformat=VCFv4.5\n"
        "##INFO=<ID=AF,Number=A,Type=Float,Description=\"Allele frequencies\">\n"
        "##INFO=<ID=XI,Number=3,Type=Integer,Description=\"Nullable list\">\n"
        "##INFO=<ID=FLAG,Number=0,Type=Flag,Description=\"A flag\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
        "chr1\t7\trs-test\tC\tT,G\t50\tPASS\tAF=0.25,0.10;XI=1,.,3;FLAG\n"
    };
    const auto parsed = ingest_vcf(input, genome);
    ok = require(parsed.header.file_format == "VCFv4.5", "VCF version") && ok;
    ok = require(parsed.records.size() == 1U, "record count") && ok;
    if (parsed.records.empty()) return false;
    const auto& record = parsed.records.front();
    ok = require(record.variant.locus.start == 6U && record.variant.locus.end == 7U, "coordinate conversion") && ok;
    ok = require(record.variant.alternates.size() == 2U, "multi-allelic ALT count") && ok;
    ok = require(record.variant.alternates[0U].type == VariantType::snv && record.variant.alternates[1U].type == VariantType::snv, "ALT types") && ok;
    ok = require(record.variant.info.allele_frequencies.has_value(), "AF fast path") && ok;
    ok = require(record.variant.info.extra_fields.size() == 2U, "typed extra INFO fields") && ok;
    if (record.variant.info.extra_fields.size() == 2U) {
        const auto* values = std::get_if<NullableVariantList<std::int32_t>>(&record.variant.info.extra_fields[0U].value);
        ok = require(values != nullptr && values->size() == 3U && (*values)[0U] == 1
                && !(*values)[1U].has_value() && (*values)[2U] == 3, "XI preserves 1,.,3") && ok;
        ok = require(std::get<bool>(record.variant.info.extra_fields[1U].value), "Flag true") && ok;
    }
    return ok;
}
