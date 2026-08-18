#include "biocore/infrastructure/windows_process_arguments.hpp"

#include <cstddef>

namespace biocore::infrastructure {

std::wstring quote_windows_process_argument(const std::wstring_view argument) {
    std::wstring quoted;
    quoted.push_back(L'"');

    std::size_t backslashes = 0U;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append((backslashes * 2U) + 1U, L'\\');
            quoted.push_back(L'"');
            backslashes = 0U;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0U;
        quoted.push_back(character);
    }

    quoted.append(backslashes * 2U, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring make_windows_process_command_line(
    const std::span<const std::wstring> arguments
) {
    std::wstring command_line;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index != 0U) {
            command_line.push_back(L' ');
        }
        command_line += quote_windows_process_argument(arguments[index]);
    }
    return command_line;
}

}  // namespace biocore::infrastructure
