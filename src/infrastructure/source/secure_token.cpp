#include "biocore/infrastructure/secure_token.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <sys/random.h>
#endif

namespace biocore::infrastructure {
namespace {

void fill_secure_random(std::byte* output, const std::size_t size) {
#ifdef _WIN32
    if (size > static_cast<std::size_t>(std::numeric_limits<ULONG>::max())) {
        throw std::invalid_argument("Secure token request is too large");
    }
    const NTSTATUS status = BCryptGenRandom(
        nullptr,
        reinterpret_cast<PUCHAR>(output),
        static_cast<ULONG>(size),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );
    if (status < 0) {
        throw std::runtime_error("BCryptGenRandom failed while generating local session token");
    }
#else
    std::size_t offset = 0U;
    while (offset < size) {
        const ssize_t result = ::getrandom(output + offset, size - offset, 0);
        if (result > 0) {
            offset += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        throw std::runtime_error("getrandom failed while generating local session token");
    }
#endif
}

}  // namespace

std::string generate_secure_token_hex(const std::size_t byte_count) {
    if (byte_count < 16U || byte_count > 1024U) {
        throw std::invalid_argument("Secure token byte count must be between 16 and 1024");
    }

    std::vector<std::byte> bytes(byte_count);
    fill_secure_random(bytes.data(), bytes.size());

    constexpr std::array<char, 16> hexadecimal{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string token;
    token.resize(byte_count * 2U);
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const auto value = static_cast<unsigned int>(bytes[index]);
        token[index * 2U] = hexadecimal[(value >> 4U) & 0x0fU];
        token[index * 2U + 1U] = hexadecimal[value & 0x0fU];
    }
    return token;
}

}  // namespace biocore::infrastructure
