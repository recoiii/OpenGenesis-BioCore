#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "biocore/infrastructure/secure_token.hpp"

namespace {
void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    const std::string first = biocore::infrastructure::generate_secure_token_hex();
    const std::string second = biocore::infrastructure::generate_secure_token_hex();
    require(first.size() == 64U && second.size() == 64U, "default token must be 256-bit hex");
    require(first != second, "consecutive CSPRNG tokens must differ");
    for (const char c : first) require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'), "token must be lowercase hex");
    bool rejected = false;
    try { static_cast<void>(biocore::infrastructure::generate_secure_token_hex(8U)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "short secure tokens must be rejected");
    std::cout << "Secure token tests passed\n";
    return 0;
}
