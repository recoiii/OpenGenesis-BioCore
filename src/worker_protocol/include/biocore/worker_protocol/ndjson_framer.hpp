#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::worker_protocol {

class NdjsonFramer final {
public:
    explicit NdjsonFramer(std::size_t maximum_line_bytes);

    // Accepts arbitrary chunks and returns complete lines without LF/CRLF terminators.
    // Throws std::invalid_argument on NUL input or a line exceeding the configured limit.
    [[nodiscard]] std::vector<std::string> feed(std::string_view bytes);

    // Returns the final unterminated line at EOF, if present.
    [[nodiscard]] std::optional<std::string> finish();
    [[nodiscard]] std::size_t buffered_bytes() const noexcept;

private:
    [[nodiscard]] std::string take_line(std::size_t newline_position);
    void validate_size() const;

    std::size_t maximum_line_bytes_;
    std::string buffer_;
};

}  // namespace biocore::worker_protocol
