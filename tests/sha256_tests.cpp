#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/infrastructure/sha256.hpp"

namespace {

[[nodiscard]] std::span<const std::byte> bytes_of(const std::string& value) {
    return {
        reinterpret_cast<const std::byte*>(value.data()),
        value.size(),
    };
}

[[nodiscard]] bool known_vectors() {
    const std::string empty;
    const std::string abc{"abc"};
    std::string million_a(1'000'000U, 'a');
    return biocore::infrastructure::sha256_hex(bytes_of(empty)) ==
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" &&
           biocore::infrastructure::sha256_hex(bytes_of(abc)) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" &&
           biocore::infrastructure::sha256_hex(bytes_of(million_a)) ==
               "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
}

[[nodiscard]] bool file_contract() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("biocore-sha256-" + std::to_string(
                           std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto path = root / "artifact.bin";
    {
        std::ofstream stream{path, std::ios::binary};
        const char payload[]{'a', 'l', 'p', 'h', 'a', '\n', '\0', static_cast<char>(0xff)};
        stream.write(payload, static_cast<std::streamsize>(sizeof(payload)));
    }
    const bool correct = biocore::infrastructure::sha256_file_hex(path) ==
                         "a3e7b82bcd4d32afe5fac0a921fc897f337c0fe5972b63f073f1b5b9124c3727";
    bool missing_rejected = false;
    try {
        static_cast<void>(biocore::infrastructure::sha256_file_hex(root / "missing"));
    } catch (const std::runtime_error&) {
        missing_rejected = true;
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
    return correct && missing_rejected;
}

}  // namespace

int main() {
    if (!known_vectors() || !file_contract()) {
        std::cerr << "SHA-256 tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
