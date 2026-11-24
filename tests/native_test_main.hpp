#pragma once
#include "signalscope/backend/iq/path.hpp"
#include <string>
#include <vector>
#ifdef _WIN32
namespace foreshadow_test {
inline int native_arguments(int argc, wchar_t **argv, int (*run)(int, char **)) {
    std::vector<std::string> utf8;
    utf8.reserve(argc);
    for (int n = 0; n < argc; ++n)
        utf8.push_back(signalscope::path_to_utf8(argv[n]));
    std::vector<char *> arguments;
    arguments.reserve(argc + 1);
    for (auto &value : utf8)
        arguments.push_back(value.data());
    arguments.push_back(nullptr);
    return run(argc, arguments.data());
}
} // namespace foreshadow_test
#define FORESHADOW_NATIVE_TEST_MAIN(run)                                                                     \
    int wmain(int argc, wchar_t **argv) {                                                                    \
        return foreshadow_test::native_arguments(argc, argv, run);                                           \
    }
#else
#define FORESHADOW_NATIVE_TEST_MAIN(run)                                                                     \
    int main(int argc, char **argv) {                                                                        \
        return run(argc, argv);                                                                              \
    }
#endif
