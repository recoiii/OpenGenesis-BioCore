#include "biocore/infrastructure/uuid_v4_generator.hpp"

#include <array>
#include <cstdint>
#include <random>
#include <string>

namespace biocore::infrastructure {
namespace {

[[nodiscard]] std::mt19937_64 make_engine() {
    std::random_device source;
    std::array<std::uint32_t, 8> seed_material{};
    for (auto& value : seed_material) {
        value = source();
    }
    std::seed_seq seed{seed_material.begin(), seed_material.end()};
    return std::mt19937_64{seed};
}

[[nodiscard]] std::mt19937_64& engine() {
    thread_local std::mt19937_64 generator = make_engine();
    return generator;
}

}  // namespace

std::string UuidV4Generator::generate() {
    std::uniform_int_distribution<unsigned int> byte_distribution{0U, 255U};
    std::array<std::uint8_t, 16> bytes{};
    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(byte_distribution(engine()));
    }

    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);

    constexpr char hexadecimal[] = "0123456789abcdef";
    std::string uuid;
    uuid.reserve(36U);

    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4U || index == 6U || index == 8U || index == 10U) {
            uuid.push_back('-');
        }
        uuid.push_back(hexadecimal[(bytes[index] >> 4U) & 0x0FU]);
        uuid.push_back(hexadecimal[bytes[index] & 0x0FU]);
    }

    return uuid;
}

}  // namespace biocore::infrastructure
