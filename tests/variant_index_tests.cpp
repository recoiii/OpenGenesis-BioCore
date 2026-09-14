#include "biocore/domain/variant_index.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "variant_index_tests: " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    bool ok = true;

    const std::vector<VariantIndexEntry> entries{
        {{1U, 100U, 101U}, 4U},
        {{0U, 20U, 30U}, 2U},
        {{0U, 10U, 11U}, 0U},
        {{0U, 25U, 26U}, 3U},
        {{0U, 10U, 15U}, 1U},
    };
    const auto index = VariantCoordinateIndex::build(entries);
    ok = require(index.size() == 5U, "index size") && ok;

    const auto point = index.query(0U, 10U, 11U);
    ok = require(point.size() == 2U && point[0U] == 0U && point[1U] == 1U, "deterministic same-start query") && ok;

    const auto overlap = index.query(0U, 24U, 27U);
    ok = require(overlap.size() == 2U && overlap[0U] == 2U && overlap[1U] == 3U, "overlap query") && ok;

    const auto other = index.query(1U, 99U, 102U);
    ok = require(other.size() == 1U && other[0U] == 4U, "contig bounded query") && ok;
    ok = require(index.query(2U, 0U, 100U).empty(), "unknown contig query") && ok;
    ok = require(index.query(0U, 20U, 20U).empty(), "empty range query") && ok;

    bool rejected = false;
    try {
        static_cast<void>(VariantCoordinateIndex::build(std::vector<VariantIndexEntry>{{{0U, 5U, 5U}, 0U}}));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    ok = require(rejected, "empty indexed interval rejected") && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
