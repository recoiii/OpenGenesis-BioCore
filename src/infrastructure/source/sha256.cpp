#include "biocore/infrastructure/sha256.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>

namespace biocore::infrastructure {
namespace {

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

class Sha256 final {
public:
    void update(const std::span<const std::byte> input) {
        for (const std::byte value : input) {
            buffer_[buffer_size_++] = std::to_integer<std::uint8_t>(value);
            ++total_bytes_;
            if (buffer_size_ == buffer_.size()) {
                transform(buffer_);
                buffer_size_ = 0U;
            }
        }
    }

    [[nodiscard]] std::array<std::uint8_t, 32> finish() const {
        Sha256 copy = *this;
        const std::uint64_t bit_length = copy.total_bytes_ * 8U;
        copy.buffer_[copy.buffer_size_++] = 0x80U;
        if (copy.buffer_size_ > 56U) {
            while (copy.buffer_size_ < copy.buffer_.size()) {
                copy.buffer_[copy.buffer_size_++] = 0U;
            }
            copy.transform(copy.buffer_);
            copy.buffer_size_ = 0U;
        }
        while (copy.buffer_size_ < 56U) {
            copy.buffer_[copy.buffer_size_++] = 0U;
        }
        for (int shift = 56; shift >= 0; shift -= 8) {
            copy.buffer_[copy.buffer_size_++] =
                static_cast<std::uint8_t>((bit_length >> static_cast<unsigned int>(shift)) & 0xffU);
        }
        copy.transform(copy.buffer_);

        std::array<std::uint8_t, 32> digest{};
        for (std::size_t index = 0U; index < copy.state_.size(); ++index) {
            const std::uint32_t value = copy.state_[index];
            digest[index * 4U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
            digest[index * 4U + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
            digest[index * 4U + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
            digest[index * 4U + 3U] = static_cast<std::uint8_t>(value & 0xffU);
        }
        return digest;
    }

private:
    void transform(const std::array<std::uint8_t, 64>& block) {
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t index = 0U; index < 16U; ++index) {
            const std::size_t offset = index * 4U;
            schedule[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                              (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                              (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                              static_cast<std::uint32_t>(block[offset + 3U]);
        }
        for (std::size_t index = 16U; index < schedule.size(); ++index) {
            const std::uint32_t s0 = std::rotr(schedule[index - 15U], 7) ^
                                     std::rotr(schedule[index - 15U], 18) ^
                                     (schedule[index - 15U] >> 3U);
            const std::uint32_t s1 = std::rotr(schedule[index - 2U], 17) ^
                                     std::rotr(schedule[index - 2U], 19) ^
                                     (schedule[index - 2U] >> 10U);
            schedule[index] = schedule[index - 16U] + s0 + schedule[index - 7U] + s1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];

        for (std::size_t index = 0U; index < schedule.size(); ++index) {
            const std::uint32_t sigma1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choose = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + sigma1 + choose + round_constants[index] + schedule[index];
            const std::uint32_t sigma0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sigma0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_size_{0U};
    std::uint64_t total_bytes_{0U};
};

[[nodiscard]] std::string digest_to_hex(const std::array<std::uint8_t, 32>& digest) {
    constexpr char alphabet[] = "0123456789abcdef";
    std::string result;
    result.resize(64U);
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        result[index * 2U] = alphabet[(digest[index] >> 4U) & 0x0fU];
        result[index * 2U + 1U] = alphabet[digest[index] & 0x0fU];
    }
    return result;
}

}  // namespace

std::string sha256_hex(const std::span<const std::byte> bytes) {
    Sha256 sha;
    sha.update(bytes);
    return digest_to_hex(sha.finish());
}

std::string sha256_file_hex(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream.is_open()) {
        throw std::runtime_error("Unable to open file for SHA-256 calculation");
    }

    Sha256 sha;
    std::array<char, 64U * 1024U> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = stream.gcount();
        if (count > 0) {
            sha.update(std::span<const std::byte>{
                reinterpret_cast<const std::byte*>(buffer.data()),
                static_cast<std::size_t>(count),
            });
        }
    }
    if (!stream.eof()) {
        throw std::runtime_error("Unable to read file during SHA-256 calculation");
    }
    return digest_to_hex(sha.finish());
}

}  // namespace biocore::infrastructure
