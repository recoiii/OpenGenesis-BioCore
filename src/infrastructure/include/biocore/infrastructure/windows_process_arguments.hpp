#pragma once

#include <span>
#include <string>
#include <string_view>

namespace biocore::infrastructure {

// Quotes one argument according to the CommandLineToArgvW / Microsoft C runtime
// backslash-and-quote rules. Quoting every argument avoids empty-argument ambiguity.
[[nodiscard]] std::wstring quote_windows_process_argument(std::wstring_view argument);

[[nodiscard]] std::wstring make_windows_process_command_line(
    std::span<const std::wstring> arguments
);

}  // namespace biocore::infrastructure
