#include "biocore/worker_protocol/ndjson_framer.hpp"

#include <stdexcept>
#include <utility>

namespace biocore::worker_protocol {

NdjsonFramer::NdjsonFramer(const std::size_t maximum_line_bytes)
    : maximum_line_bytes_{maximum_line_bytes} {
    if (maximum_line_bytes_ == 0U) {
        throw std::invalid_argument("NDJSON maximum line size must be positive");
    }
}

std::vector<std::string> NdjsonFramer::feed(const std::string_view bytes) {
    std::vector<std::string> lines;
    for (const char byte : bytes) {
        if (byte == '\0') {
            throw std::invalid_argument("NDJSON stream must not contain NUL characters");
        }
        if (byte == '\n') {
            std::string line = std::move(buffer_);
            buffer_.clear();
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
            continue;
        }
        if (buffer_.size() >= maximum_line_bytes_) {
            throw std::invalid_argument("NDJSON line exceeds the maximum size");
        }
        buffer_.push_back(byte);
    }
    return lines;
}

std::optional<std::string> NdjsonFramer::finish() {
    if (buffer_.empty()) {
        return std::nullopt;
    }
    validate_size();
    std::string line = std::move(buffer_);
    buffer_.clear();
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

std::size_t NdjsonFramer::buffered_bytes() const noexcept {
    return buffer_.size();
}

std::string NdjsonFramer::take_line(const std::size_t newline_position) {
    std::string line = buffer_.substr(0U, newline_position);
    buffer_.erase(0U, newline_position + 1U);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

void NdjsonFramer::validate_size() const {
    if (buffer_.size() > maximum_line_bytes_) {
        throw std::invalid_argument("NDJSON line exceeds the maximum size");
    }
}

}  // namespace biocore::worker_protocol
