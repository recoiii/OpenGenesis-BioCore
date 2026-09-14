#include "biocore/domain/vcf_ingestion.hpp"
#include "vcf_ingestion_test_support.hpp"
#include <iostream>
#include <sstream>
namespace {
[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) { std::cerr << "vcf_ingestion_genotype_tests: " << message << '\n'; return false; }
    return true;
}
}
bool run_vcf_ingestion_genotype_tests(const biocore::domain::ReferenceGenome& genome) {
    using biocore::domain::ingest_vcf;
    bool ok = true;
    std::istringstream input{
        "##fileformat=VCFv4.5\n"
        "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
        "##FORMAT=<ID=AD,Number=R,Type=Integer,Description=\"Allele depth\">\n"
        "##FORMAT=<ID=DP,Number=1,Type=Integer,Description=\"Depth\">\n"
        "##FORMAT=<ID=GQ,Number=1,Type=Integer,Description=\"Genotype quality\">\n"
        "##FORMAT=<ID=PL,Number=G,Type=Integer,Description=\"Likelihoods\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tsample-A\n"
        "chr1\t7\t.\tC\tT,G\t.\tPASS\t.\tGT:AD:DP:GQ:PL\t1|2:5,3,2:10:42:70,60,50,40,30,0\n"
    };
    const auto parsed = ingest_vcf(input, genome);
    ok = require(parsed.header.sample_names.size() == 1U && parsed.header.sample_names[0] == "sample-A", "sample header") && ok;
    ok = require(parsed.records.size() == 1U && parsed.records[0U].samples.size() == 1U, "sample record") && ok;
    if (parsed.records.empty() || parsed.records[0U].samples.empty()) return false;
    const auto& call = parsed.records[0U].samples[0U].genotype;
    ok = require(call.ploidy() == 2U, "diploid GT") && ok;
    ok = require(call.allele_indices[0U] == 1 && call.allele_indices[1U] == 2, "GT allele order") && ok;
    ok = require(call.phased_separators.size() == 1U && call.phased_separators[0U] == 1U, "phased GT") && ok;
    ok = require(call.allele_depths.size() == 3U && call.allele_depths[2U] == 2U, "AD Number=R") && ok;
    ok = require(call.depth == 10U && call.genotype_quality == 42U, "DP/GQ fast path") && ok;
    ok = require(call.phred_likelihoods.size() == 6U && call.phred_likelihoods[5U] == 0U, "PL Number=G") && ok;
    return ok;
}
