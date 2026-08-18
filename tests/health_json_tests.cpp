#include <cstdlib>
#include <iostream>
#include <string>

#include "biocore/application/health_snapshot.hpp"
#include "biocore/presentation/health_json.hpp"

int main() {
    const biocore::application::HealthSnapshot snapshot{
        .status = "healthy",
        .component = "test\"component\\\b\f\n\r\t\x01",
        .version = "0.1.0-dev",
        .timestamp_utc = "2026-08-06T16:44:00Z",
    };

    const std::string expected =
        "{\"status\":\"healthy\",\"component\":\"test\\\"component\\\\\\b\\f\\n\\r\\t\\u0001\","
        "\"version\":\"0.1.0-dev\",\"timestampUtc\":\"2026-08-06T16:44:00Z\"}";
    const std::string actual = biocore::presentation::render_health_json(snapshot);

    if (actual != expected) {
        std::cerr << "Unexpected health JSON:\n" << actual << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
