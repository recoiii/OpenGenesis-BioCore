#include "vcf_ingestion_test_support.hpp"

#include "biocore/domain/vcf_ingestion.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
[[nodiscard]] bool require_edge(const bool condition, const char* message) {
    if (!condition) { std::cerr << "vcf_ingestion_edge_tests: " << message << '\n'; return false; }
    return true;
}
template <typename Function>
[[nodiscard]] bool throws_invalid_argument(Function&& function) {
    try { function(); } catch (const std::invalid_argument&) { return true; }
    return false;
}
}

bool run_vcf_ingestion_edge_tests(const biocore::domain::ReferenceGenome& genome) {
    using biocore::domain::ingest_vcf;
    bool ok = true;
    ok = require_edge(throws_invalid_argument([&] {
        std::istringstream mismatch{
            "##fileformat=VCFv4.3\n"
            "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
            "1\t7\t.\tA\tT\t.\tPASS\t.\n"
        };
        static_cast<void>(ingest_vcf(mismatch, genome));
    }), "REF mismatch rejected") && ok;

    ok = require_edge(throws_invalid_argument([&] {
        std::istringstream undeclared{
            "##fileformat=VCFv4.3\n"
            "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
            "1\t7\t.\tC\tT\t.\tPASS\tUNDECLARED=1\n"
        };
        static_cast<void>(ingest_vcf(undeclared, genome));
    }), "undeclared INFO rejected") && ok;


    ok = require_edge(throws_invalid_argument([&] {
        std::istringstream gt_order{
            "##fileformat=VCFv4.5\n"
            "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
            "##FORMAT=<ID=DP,Number=1,Type=Integer,Description=\"Depth\">\n"
            "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tsample-A\n"
            "1\t7\t.\tC\tT\t.\tPASS\t.\tDP:GT\t10:0/1\n"
        };
        static_cast<void>(ingest_vcf(gt_order, genome));
    }), "GT must be first FORMAT field") && ok;

    ok = require_edge(throws_invalid_argument([&] {
        std::istringstream negative_depth{
            "##fileformat=VCFv4.5\n"
            "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
            "##FORMAT=<ID=DP,Number=1,Type=Integer,Description=\"Depth\">\n"
            "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tsample-A\n"
            "1\t7\t.\tC\tT\t.\tPASS\t.\tGT:DP\t0/1:-1\n"
        };
        static_cast<void>(ingest_vcf(negative_depth, genome));
    }), "negative FORMAT depth rejected") && ok;

    ok = require_edge(throws_invalid_argument([&] {
        std::istringstream invalid_pos{
            "##fileformat=VCFv4.3\n"
            "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
            "1\t0\t.\tA\tT\t.\tPASS\t.\n"
        };
        static_cast<void>(ingest_vcf(invalid_pos, genome));
    }), "POS zero rejected") && ok;

    ok = require_edge(throws_invalid_argument([&] {
        std::istringstream invalid_alt{
            "##fileformat=VCFv4.5\n"
            "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
            "1\t7\t.\tC\tZ\t.\tPASS\t.\n"
        };
        static_cast<void>(ingest_vcf(invalid_alt, genome));
    }), "sequence ALT outside A,C,G,T,N rejected") && ok;
    return ok;
}
