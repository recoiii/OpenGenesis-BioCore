#pragma once

#include "biocore/domain/reference_genome.hpp"

[[nodiscard]] bool run_vcf_ingestion_info_tests(const biocore::domain::ReferenceGenome& genome);
[[nodiscard]] bool run_vcf_ingestion_genotype_tests(const biocore::domain::ReferenceGenome& genome);
[[nodiscard]] bool run_vcf_ingestion_missing_tests(const biocore::domain::ReferenceGenome& genome);
[[nodiscard]] bool run_vcf_ingestion_edge_tests(const biocore::domain::ReferenceGenome& genome);
