#include <chrono>
#include <cstdlib>
#include <string_view>
#include <thread>

int main(const int argc, const char* const argv[]) {
    if (argc != 7 || std::string_view{argv[1]} != "--module-id" ||
        std::string_view{argv[3]} != "--step-id" ||
        std::string_view{argv[5]} != "--invocation") {
        return 2;
    }
    if (std::string_view{argv[2]} != "org.biocore.test.slow.run" ||
        std::string_view{argv[4]} != "slow" || std::string_view{argv[6]}.empty()) {
        return 3;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{2600});
    return EXIT_SUCCESS;
}
