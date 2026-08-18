#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "fasta_invocation_contract.hpp"

namespace {
using biocore::fasta_qc::InvocationInput;
using biocore::fasta_qc::InvocationOutput;
using biocore::fasta_qc::validate_invocation_contract;

template <typename Function>
void expect_invalid(Function&& function, const std::string_view message) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return;
    }
    std::cerr << "FAIL: expected invalid invocation: " << message << '\n';
    std::exit(1);
}
}  // namespace

int main() {
    const std::vector<InvocationInput> valid_inputs{
        InvocationInput{"source", "managedFile", "fasta"}
    };
    const std::vector<InvocationOutput> valid_outputs{
        InvocationOutput{"summary", "json"},
        InvocationOutput{"table", "tsv"},
    };

    validate_invocation_contract(
        "org.biocore.fastaqc.stats", 0U, valid_inputs, valid_outputs
    );

    expect_invalid([&] {
        validate_invocation_contract("org.biocore.other", 0U, valid_inputs, valid_outputs);
    }, "module identity");
    expect_invalid([&] {
        validate_invocation_contract("org.biocore.fastaqc.stats", 1U, valid_inputs, valid_outputs);
    }, "parameters forbidden");
    expect_invalid([&] {
        validate_invocation_contract(
            "org.biocore.fastaqc.stats", 0U,
            {InvocationInput{"source", "step_output", "fasta"}}, valid_outputs
        );
    }, "managed-file source kind");
    expect_invalid([&] {
        validate_invocation_contract(
            "org.biocore.fastaqc.stats", 0U,
            {InvocationInput{"source", "managedFile", "fastq"}}, valid_outputs
        );
    }, "FASTA input file type");
    expect_invalid([&] {
        validate_invocation_contract(
            "org.biocore.fastaqc.stats", 0U, valid_inputs,
            {InvocationOutput{"summary", "json"}}
        );
    }, "two outputs required");
    expect_invalid([&] {
        validate_invocation_contract(
            "org.biocore.fastaqc.stats", 0U, valid_inputs,
            {InvocationOutput{"summary", "json"}, InvocationOutput{"table", "json"}}
        );
    }, "TSV output type");
    expect_invalid([&] {
        validate_invocation_contract(
            "org.biocore.fastaqc.stats", 0U, valid_inputs,
            {InvocationOutput{"summary", "json"}, InvocationOutput{"summary", "json"}}
        );
    }, "duplicate summary output");

    std::cout << "FASTA invocation contract harness PASS\n";
    return 0;
}
