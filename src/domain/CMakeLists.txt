add_library(biocore-domain STATIC)
add_library(BioCore::domain ALIAS biocore-domain)

target_sources(
    biocore-domain
    PRIVATE
        source/component_identity.cpp
        source/contig_table.cpp
        source/genotype.cpp
        source/indel_candidate.cpp
        source/indel_candidate_alignment.cpp
        source/indel_candidate_seeding.cpp
        source/local_realigner.cpp
        source/job.cpp
        source/job_failure.cpp
        source/job_priority.cpp
        source/job_status.cpp
        source/managed_file.cpp
        source/pipeline_definition.cpp
        source/plugin_io_contract.cpp
        source/plugin_manifest.cpp
        source/plugin_module_definition.cpp
        source/plugin_platform.cpp
        source/pipeline_step.cpp
        source/probabilistic_genotyping.cpp
        source/project.cpp
        source/reference_genome.cpp
        source/vcf_ingestion_attributes.cpp
        source/storage_mode.cpp
        source/variant_filtering.cpp
        source/variant_filtering_adapters.cpp
        source/variant_filtering_regions.cpp
        source/variant_index.cpp
        source/variant_normalization.cpp
        source/variant_record.cpp
        source/variant_types.cpp
        source/vcf_ingestion.cpp
        source/vcf_ingestion_header.cpp
        source/vcf_ingestion_info.cpp
        source/vcf_ingestion_record.cpp
        source/vcf_ingestion_values.cpp
        source/vcf_ingestion_fields.cpp
        source/vcf_ingestion_support.cpp
        source/vcf_ingestion_types.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/biocore/domain/component_identity.hpp
            include/biocore/domain/contig_table.hpp
            include/biocore/domain/genotype.hpp
            include/biocore/domain/inline_values.hpp
            include/biocore/domain/indel_candidate.hpp
            include/biocore/domain/job.hpp
            include/biocore/domain/job_failure.hpp
            include/biocore/domain/job_priority.hpp
            include/biocore/domain/job_status.hpp
            include/biocore/domain/managed_file.hpp
            include/biocore/domain/pipeline_definition.hpp
            include/biocore/domain/plugin_io_contract.hpp
            include/biocore/domain/plugin_manifest.hpp
            include/biocore/domain/plugin_module_definition.hpp
            include/biocore/domain/plugin_platform.hpp
            include/biocore/domain/pipeline_step.hpp
            include/biocore/domain/probabilistic_genotyping.hpp
            include/biocore/domain/project.hpp
            include/biocore/domain/reference_genome.hpp
            include/biocore/domain/storage_mode.hpp
            include/biocore/domain/variant_filtering.hpp
            include/biocore/domain/variant_index.hpp
            include/biocore/domain/variant_normalization.hpp
            include/biocore/domain/variant_record.hpp
            include/biocore/domain/variant_types.hpp
            include/biocore/domain/vcf_ingestion.hpp
)

target_link_libraries(
    biocore-domain
    PUBLIC BioCore::project_options
    PRIVATE BioCore::project_warnings BioCore::sanitizers
)

if(BIOCORE_BUILD_TESTS)
    add_executable(biocore-variant-types-tests ${PROJECT_SOURCE_DIR}/tests/variant_types_tests.cpp)
    target_link_libraries(
        biocore-variant-types-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.variant_types COMMAND biocore-variant-types-tests)

    add_executable(
        biocore-genotype-representation-tests
        ${PROJECT_SOURCE_DIR}/tests/genotype_representation_tests.cpp
    )
    target_link_libraries(
        biocore-genotype-representation-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.genotype_representation COMMAND biocore-genotype-representation-tests)

    add_executable(
        biocore-contig-normalization-tests
        ${PROJECT_SOURCE_DIR}/tests/contig_normalization_tests.cpp
    )
    target_link_libraries(
        biocore-contig-normalization-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.contig_normalization COMMAND biocore-contig-normalization-tests)

    add_executable(
        biocore-vcf-ingestion-tests
        ${PROJECT_SOURCE_DIR}/tests/vcf_ingestion_tests.cpp
        ${PROJECT_SOURCE_DIR}/tests/vcf_ingestion_info_tests.cpp
        ${PROJECT_SOURCE_DIR}/tests/vcf_ingestion_genotype_tests.cpp
        ${PROJECT_SOURCE_DIR}/tests/vcf_ingestion_missing_tests.cpp
        ${PROJECT_SOURCE_DIR}/tests/vcf_ingestion_edge_tests.cpp
    )
    target_link_libraries(
        biocore-vcf-ingestion-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.vcf_ingestion COMMAND biocore-vcf-ingestion-tests)

    add_executable(
        biocore-variant-normalization-tests
        ${PROJECT_SOURCE_DIR}/tests/variant_normalization_tests.cpp
    )
    target_link_libraries(
        biocore-variant-normalization-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.variant_normalization COMMAND biocore-variant-normalization-tests)

    add_executable(biocore-variant-index-tests ${PROJECT_SOURCE_DIR}/tests/variant_index_tests.cpp)
    target_link_libraries(
        biocore-variant-index-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.variant_index COMMAND biocore-variant-index-tests)

    add_executable(
        biocore-variant-index-benchmark
        ${PROJECT_SOURCE_DIR}/tests/variant_index_benchmark.cpp
    )
    target_link_libraries(
        biocore-variant-index-benchmark
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )

    add_executable(biocore-indel-candidate-tests ${PROJECT_SOURCE_DIR}/tests/indel_candidate_tests.cpp)
    target_link_libraries(
        biocore-indel-candidate-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.indel_candidate_calling COMMAND biocore-indel-candidate-tests)

    add_executable(biocore-local-realigner-tests ${PROJECT_SOURCE_DIR}/tests/local_realigner_tests.cpp)
    target_link_libraries(
        biocore-local-realigner-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.local_realigner COMMAND biocore-local-realigner-tests)

    add_executable(
        biocore-indel-candidate-boundary-tests
        ${PROJECT_SOURCE_DIR}/tests/indel_candidate_boundary_tests.cpp
    )
    target_link_libraries(
        biocore-indel-candidate-boundary-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.indel_candidate_canonical_boundary COMMAND biocore-indel-candidate-boundary-tests)

    add_executable(
        biocore-indel-realignment-benchmark
        ${PROJECT_SOURCE_DIR}/tests/indel_realignment_benchmark.cpp
    )
    target_link_libraries(
        biocore-indel-realignment-benchmark
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )


    add_executable(
        biocore-probabilistic-genotyping-tests
        ${PROJECT_SOURCE_DIR}/tests/probabilistic_genotyping_tests.cpp
    )
    target_link_libraries(
        biocore-probabilistic-genotyping-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.probabilistic_genotyping COMMAND biocore-probabilistic-genotyping-tests)

    add_executable(
        biocore-probabilistic-genotyping-indel-tests
        ${PROJECT_SOURCE_DIR}/tests/probabilistic_genotyping_indel_tests.cpp
    )
    target_link_libraries(
        biocore-probabilistic-genotyping-indel-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.probabilistic_genotyping_indel COMMAND biocore-probabilistic-genotyping-indel-tests)

    add_executable(
        biocore-probabilistic-genotyping-prior-tests
        ${PROJECT_SOURCE_DIR}/tests/probabilistic_genotyping_prior_tests.cpp
    )
    target_link_libraries(
        biocore-probabilistic-genotyping-prior-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.probabilistic_genotyping_priors COMMAND biocore-probabilistic-genotyping-prior-tests)

    add_executable(
        biocore-probabilistic-genotyping-benchmark
        ${PROJECT_SOURCE_DIR}/tests/probabilistic_genotyping_benchmark.cpp
    )
    target_link_libraries(
        biocore-probabilistic-genotyping-benchmark
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )

    add_executable(
        biocore-advanced-variant-filtering-tests
        ${PROJECT_SOURCE_DIR}/tests/advanced_variant_filtering_tests.cpp
    )
    target_link_libraries(
        biocore-advanced-variant-filtering-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.advanced_variant_filtering COMMAND biocore-advanced-variant-filtering-tests)

    add_executable(
        biocore-variant-filtering-context-tests
        ${PROJECT_SOURCE_DIR}/tests/variant_filtering_context_tests.cpp
    )
    target_link_libraries(
        biocore-variant-filtering-context-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.variant_filtering_context COMMAND biocore-variant-filtering-context-tests)

    add_executable(
        biocore-variant-filtering-region-tests
        ${PROJECT_SOURCE_DIR}/tests/variant_filtering_region_tests.cpp
    )
    target_link_libraries(
        biocore-variant-filtering-region-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.variant_filtering_regions COMMAND biocore-variant-filtering-region-tests)

    add_executable(
        biocore-variant-filtering-integration-tests
        ${PROJECT_SOURCE_DIR}/tests/variant_filtering_integration_tests.cpp
    )
    target_link_libraries(
        biocore-variant-filtering-integration-tests
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
    add_test(NAME domain.variant_filtering_integration COMMAND biocore-variant-filtering-integration-tests)

    add_executable(
        biocore-variant-filtering-benchmark
        ${PROJECT_SOURCE_DIR}/tests/variant_filtering_benchmark.cpp
    )
    target_link_libraries(
        biocore-variant-filtering-benchmark
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )

    add_executable(
        biocore-variant-model-memory-benchmark
        ${PROJECT_SOURCE_DIR}/tests/variant_model_memory_benchmark.cpp
    )
    target_link_libraries(
        biocore-variant-model-memory-benchmark
        PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
    )
endif()
